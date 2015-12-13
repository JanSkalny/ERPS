#include "stdafx.h"

#include "tick.h"
#include "ring_raps.h"
#include "ring_port.h"
#include "ring.h"

const char *RING_STATE_STR[6] = { 
	"INIT",
	"IDLE",
	"PROTECTION",
	"MANUAL_SWITCH",
	"FORCED_SWITCH",
	"PENDING",
};

const char *RING_REQUEST_STR[15] = { 
	"INVALID", 
	"RAPS_NR",
	"RAPS_NR_RB",
	"WTB_RUNNING",
	"WTB_EXPIRES",
	"WTR_RUNNING",
	"WTR_EXPIRES",
	"MS",
	"RAPS_MS",
	"RAPS_SF",
	"CLEAR_SF",
	"SF",
	"RAPS_FS",
	"FS",
	"CLEAR",
};


const char * ring_req_str(enum ring_request req) {
	//static char *s_invalid = "INVALID";
	if (req < RING_REQ_INVALID || req > RING_REQ_CLEAR) 
		die("WTH! %02xh", req);
	//	return s_invalid;
	
	return RING_REQUEST_STR[req];
}


// Ring FSM action
// ------------------------------------------

void start_wtr_timer() {
	D(" - start WTR timer");
}
void stop_wtr_timer() {
	D(" - stop WTR timer");
}
void start_wtb_timer() {
	D(" - start WTB timer");
}
void stop_wtb_timer() {
	D(" - stop WTB timer");
}
void start_guard_timer(struct ring *ring) {
	D(" - start guard timer");
	ring->guard_timer_active_since = tick_now();
	ring->guard_timer_active = true;
}
void stop_guard_timer(struct ring *ring) {
	D(" - stop guard timer");
	ring->guard_timer_active = false;
}
void block_port(struct ring_port *port) {
	D(" - block port %s", port->name);
	ring_port_block(port);
}
void unblock_port(struct ring_port *port) {
	D(" - unblock port %s", port->name);
	ring_port_unblock(port);
}
bool is_port_blocked(struct ring_port *port) {
	return ring_port_is_blocked(port);
}
void flush_fdb() {
	D(" - flush fdb");
}
struct ring_port* ring_other_port(struct ring *r, struct ring_port *port) {
	return (port == r->port1) ? r->port0 : r->port1;
}
void unblock_nonfailed_ports(struct ring *r) {
	D(" - unblock non-failed ports");

	if (!r->port0->is_failed)
		unblock_port(r->port0);
	if (!r->port1->is_failed)
		unblock_port(r->port1);
}



void start_tx(struct ring *r, enum raps_request request, uint8_t flags) {
	D(" - start Tx R-APS(%s%s%s)", RAPS_REQUEST_STR[request], \
		flags&RAPS_FLAG_DNF ? ",DNF" : "", \
		flags&RAPS_FLAG_RB ? ",RB" : "" \
	);

	// will this new R-APS frame differ from previous one?
	if (r->raps->request != request || r->raps->flags != flags) {
		// serialize new R-APS frame and burst it out
		D("   - BURST -");
		r->raps->request = request;
		r->raps->flags = flags;
		ri_raps_encode(r->raps, r->raps_frame, r->raps_frame_len);
		r->raps_bursts_remain = 3;
	}

	r->is_sending_raps = true;
}

void stop_tx(struct ring *r) {
	D(" - stop R-APS Tx");

	// stop sending R-APS, as requested
	r->is_sending_raps = false;
}



void ring_fsm(struct ring *r, enum ring_request req, struct ring_port *port, uint8_t *remote_node_id) {
	enum ring_state new_state = r->state;
	bool dupe = false;

	// request deduplication
	switch (req) {
	case RING_REQ_RAPS_NR:
	case RING_REQ_RAPS_NR_RB:
	case RING_REQ_RAPS_SF:
	case RING_REQ_RAPS_MS:
	case RING_REQ_RAPS_FS:
		// only address and request has to match
		if (req == r->prev_request && (memcmp(remote_node_id, r->prev_request_remote_node_id, 6) == 0)) 
			dupe = true;
		break;

	case RING_REQ_MS:
	case RING_REQ_SF:
	case RING_REQ_FS:
	case RING_REQ_CLEAR_SF:
		// only port and request has to match
		if (req == r->prev_request && port == r->prev_request_port) 
			dupe = true;
		break;

	case RING_REQ_WTB_RUNNING:
	case RING_REQ_WTB_EXPIRES:
	case RING_REQ_WTR_RUNNING:
	case RING_REQ_WTR_EXPIRES:
	case RING_REQ_CLEAR:
		// dupes are processed again
		if (req == r->prev_request) 
			die("impossible duplicit request received! %02xh (%s)", req, ring_req_str(req));
		break;
	}

	if (dupe) {
		//D("FSM: same request ignored");
		return;
	}

	// save request parameters, for future comparison
	r->prev_request = req;
	r->prev_request_port = port;
	if (remote_node_id)
		memcpy(r->prev_request_remote_node_id, remote_node_id, 6);

	switch(r->state) {
	case RING_STATE_INIT:
		D("FSM: execute row 01");
		stop_guard_timer(r);
		stop_wtr_timer();
		stop_wtb_timer();
		if (r->is_rpl_owner) {
			block_port(r->rpl_port);
			unblock_port(ring_other_port(r, r->rpl_port));	
			start_tx(r, RAPS_REQ_NR, 0);
			if (r->is_revertive) 
				start_wtr_timer();
		} else if (r->is_rpl_neighbour) {
			block_port(r->rpl_port);
			unblock_port(ring_other_port(r, r->rpl_port));
			start_tx(r, RAPS_REQ_NR, 0);
		} else {
			// XXX: maybe randomize between port0 and port1
			block_port(r->port0);
			unblock_port(r->port1);
			start_tx(r, RAPS_REQ_NR, 0);
		}
		new_state = RING_STATE_PENDING;
		break;

	case RING_STATE_IDLE: 
		switch(req) {
		case RING_REQ_CLEAR:
			D("FSM: execute row 02");
			break;

		case RING_REQ_FS:
			// admin wants to force-block port
			D("FSM: execute row 03");
			if (is_port_blocked(port)) {
				// requested port is already blocked.. signal FS condition to others
				start_tx(r, RAPS_REQ_FS, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				// port is active... block and notify
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_RAPS_FS:
			// remote admin wants to force-block port.. unblock ours
			D("FSM: execute row 04");
			unblock_port(r->port0);
			unblock_port(r->port1);
			stop_tx(r);
			new_state = RING_STATE_FORCED_SWITCH;	
			break;

		case RING_REQ_SF:
			// local port failed
			D("FSM: execute row 05");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_SF, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_PROTECTION;
			break;

		case RING_REQ_CLEAR_SF:
			// local port restored
			D("FSM: execute row 06");
			break;


		case RING_REQ_RAPS_SF:
			// remote port failed
			D("FSM: execute row 07");
			unblock_nonfailed_ports(r);
			stop_tx(r);
			new_state = RING_STATE_PROTECTION;
			break;	
	
		case RING_REQ_RAPS_MS:
			// remote admin wants to manual-block port
			D("FSM: execute row 08");
			unblock_nonfailed_ports(r);
			stop_tx(r);
			new_state = RING_STATE_MANUAL_SWITCH;
			break;		

		case RING_REQ_MS:
			// admin wants to manual-block port
			D("FSM: execute row 09");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_MS, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_MS, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_MANUAL_SWITCH;
			break;

		case RING_REQ_WTR_EXPIRES:
			D("FSM: execute row 10");
			break;

		case RING_REQ_WTR_RUNNING:
			D("FSM: execute row 11");
			break;

		case RING_REQ_WTB_EXPIRES:
			D("FSM: execute row 12");
			break;

		case RING_REQ_WTB_RUNNING:
			D("FSM: execute row 13");
			break;

		case RING_REQ_RAPS_NR_RB:
			// owner acknowledged errorus link restoration
			D("FSM: execute row 14");
			unblock_port(ring_other_port(r, r->rpl_port));
			if (!r->is_rpl_owner)
				stop_tx(r);
			break;

		case RING_REQ_RAPS_NR:
			D("FSM: execute row 15");
			if (!r->is_rpl_owner && !r->is_rpl_neighbour &&
				memcmp(r->node_id, remote_node_id, 6) < 0) {
				unblock_nonfailed_ports(r);
				stop_tx(r);
			}
			break;
		}
		break;	



	case RING_STATE_PROTECTION:
		switch(req) {
		case RING_REQ_CLEAR:
			D("FSM: execute row 16");
			break;

		case RING_REQ_FS:
			D("FSM: execute row 17");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_FS, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_RAPS_FS:
			D("FSM: execute row 18");
			unblock_port(r->port0);
			unblock_port(r->port1);
			stop_tx(r);
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_SF:
			D("FSM: execute row 19");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_SF, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			break;

		case RING_REQ_CLEAR_SF:
			D("FSM: execute row 20");
			start_guard_timer(r);
			start_tx(r, RAPS_REQ_NR, 0);
			if (r->is_rpl_owner && r->is_revertive)
				start_wtr_timer();
			new_state = RING_STATE_PENDING;
			break;

		case RING_REQ_RAPS_SF:
			D("FSM: execute row 21");
			break;

		case RING_REQ_RAPS_MS:
			D("FSM: execute row 22");
			break;

		case RING_REQ_MS:
			D("FSM: execute row 23");
			break;

		case RING_REQ_WTR_EXPIRES:
			D("FSM: execute row 24");
			break;

		case RING_REQ_WTR_RUNNING:
			D("FSM: execute row 25");
			break;

		case RING_REQ_WTB_EXPIRES:
			D("FSM: execute row 26");
			break;

		case RING_REQ_WTB_RUNNING:
			D("FSM: execute row 27");
			break;


		case RING_REQ_RAPS_NR_RB:
			D("FSM: execute row 28");
			// no action, but change state
			new_state = RING_STATE_PENDING;
			break;

		case RING_REQ_RAPS_NR:
			// FIXME:
			D("FSM: execute row 29");
			if (r->is_rpl_owner && r->is_revertive)
				start_wtr_timer();
			new_state = RING_STATE_PENDING; 
			break;
		}
		break;



	case RING_STATE_MANUAL_SWITCH:
		switch(req) {
		case RING_REQ_CLEAR:
			D("FSM: execute row 30");
			if (is_port_blocked(r->port1) || is_port_blocked(r->port0)) {
				start_guard_timer(r);
				start_tx(r, RAPS_REQ_NR, 0);
				if (r->is_rpl_owner && r->is_revertive)
					start_wtb_timer();
			}
			new_state = RING_STATE_PENDING;
			break;

		case RING_REQ_FS:
			D("FSM: execute row 31");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_FS, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_RAPS_FS:
			D("FSM: execute row 32");
			unblock_port(r->port0);
			unblock_port(r->port1);
			stop_tx(r);
			new_state = RING_STATE_FORCED_SWITCH;
			break;
			
		case RING_REQ_SF:
			D("FSM: execute row 33");	
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_SF, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_PROTECTION;
			break;

		case RING_REQ_CLEAR_SF:
			D("FSM: execute row 34");
			break;
			
		case RING_REQ_RAPS_SF:
			D("FSM: execute row 35");
			unblock_nonfailed_ports(r);
			stop_tx(r);
			break;

		case RING_REQ_RAPS_MS:
			D("FSM: execute row 36"); 
			// XXX: new_state depends on other information
			// maybe we can filter this in priority logic selection?? (would be wiser)
			if (is_port_blocked(r->port0) || is_port_blocked(r->port1)) {
				start_guard_timer(r);
				start_tx(r, RAPS_REQ_NR, 0);
				if (r->is_rpl_owner && r->is_revertive)
					start_wtb_timer();
				new_state = RING_STATE_PENDING;
			} else {
				new_state = RING_STATE_MANUAL_SWITCH;
			}
			break;

		case RING_REQ_MS:
			D("FSM: execute row 37");
			break;

		case RING_REQ_WTR_EXPIRES:
			D("FSM: execute row 38");
			break;

		case RING_REQ_WTR_RUNNING:
			D("FSM: execute row 39");
			break;

		case RING_REQ_WTB_EXPIRES:
			D("FSM: execute row 40");
			break;

		case RING_REQ_WTB_RUNNING:
			D("FSM: execute row 41");
			break;

		case RING_REQ_RAPS_NR_RB:
			D("FSM: execute row 42");
			// no action, but change state
			new_state = RING_STATE_PENDING;
			break;

		case RING_REQ_RAPS_NR:
			D("FSM: execute row 43");
			if (r->is_rpl_owner && r->is_revertive)
				start_wtb_timer();
			new_state = RING_STATE_PENDING;
			break;
		}
		break;



	case RING_STATE_FORCED_SWITCH:
		switch(req) {
		case RING_REQ_CLEAR:
			D("FSM: execute row 44");
			if (is_port_blocked(r->port0) || is_port_blocked(r->port1)) {
				start_guard_timer(r);
				start_tx(r, RAPS_REQ_NR, 0);
				if (r->is_rpl_owner && r->is_revertive)
					start_wtb_timer();
			}
			new_state = RING_STATE_PENDING;
			break;

		case RING_REQ_FS:
			D("FSM: execute row 45");
			block_port(port);
			start_tx(r, RAPS_REQ_FS, 0);
			flush_fdb();
			break;

		case RING_REQ_RAPS_FS:
			D("FSM: execute row 46");
			break;

		case RING_REQ_SF:
			D("FSM: execute row 47");
			break;

		case RING_REQ_CLEAR_SF:
			D("FSM: execute row 48");
			break;

		case RING_REQ_RAPS_SF:
			D("FSM: execute row 49");
			break;

		case RING_REQ_RAPS_MS:
			D("FSM: execute row 50");
			break;

		case RING_REQ_MS:
			D("FSM: execute row 51");
			break;

		case RING_REQ_WTR_EXPIRES:
			D("FSM: execute row 52");
			break;

		case RING_REQ_WTR_RUNNING:
			D("FSM: execute row 53");
			break;

		case RING_REQ_WTB_EXPIRES:
			D("FSM: execute row 54");
			break;

		case RING_REQ_WTB_RUNNING:
			D("FSM: execute row 55");
			break;

		case RING_REQ_RAPS_NR_RB:
			D("FSM: execute row 56");
			// no action, but new state
			new_state = RING_STATE_PENDING;
			break;

		case RING_REQ_RAPS_NR:
			D("FSM: execute row 57");
			if (r->is_rpl_owner && r->is_revertive)
				start_wtb_timer();
			new_state = RING_STATE_PENDING;
			break;
		}
		break;
	


	case RING_STATE_PENDING:
		switch(req) {
		case RING_REQ_CLEAR:
			D("FSM: execute row 58");
			if (r->is_rpl_owner) {
				//XXX: wrong indentaion in FSM
				stop_wtr_timer();
				stop_wtb_timer();
				if (is_port_blocked(r->rpl_port)) {
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB|RAPS_FLAG_DNF);
					unblock_port(ring_other_port(r, r->rpl_port));
				} else {
					block_port(r->rpl_port);
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB);
					unblock_port(ring_other_port(r, r->rpl_port));
					flush_fdb();
				}
			}
			new_state = RING_STATE_IDLE;
			break;

		case RING_REQ_FS:
			D("FSM: execute row 59");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_FS, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				stop_wtb_timer();
			}
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_RAPS_FS:
			D("FSM: execute row 60");
			unblock_port(r->port0);
			unblock_port(r->port1);
			stop_tx(r);
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				stop_wtb_timer();
			}
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_SF:
			D("FSM: execute row 61");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_SF, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				stop_wtb_timer();
			}
			new_state = RING_STATE_PROTECTION;
			break;

		case RING_REQ_CLEAR_SF:
			D("FSM: execute row 62");
			break;

		case RING_REQ_RAPS_SF:
			D("FSM: execute row 63");
			unblock_nonfailed_ports(r);
			stop_tx(r);
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				stop_wtb_timer();
			}
			new_state = RING_STATE_PROTECTION;
			break;

		case RING_REQ_RAPS_MS:
			D("FSM: execute row 64");
			unblock_nonfailed_ports(r);
			stop_tx(r);
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				stop_wtb_timer();
			}
			new_state = RING_STATE_MANUAL_SWITCH;
			break;

		case RING_REQ_MS:
			D("FSM: execute row 65");
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				stop_wtb_timer();
			}
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_MS, RAPS_FLAG_DNF);
				unblock_port(ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_MS, 0);
				unblock_port(ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_MANUAL_SWITCH;
			break;

		case RING_REQ_WTR_EXPIRES:
			D("FSM: execute row 66");
			if (r->is_rpl_owner) {
				stop_wtb_timer();
				if (is_port_blocked(r->rpl_port)) {
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_DNF|RAPS_FLAG_RB);
					unblock_port(ring_other_port(r, r->rpl_port));
				} else {
					block_port(r->rpl_port);
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB);	
					unblock_port(ring_other_port(r, r->rpl_port));
					flush_fdb();
				}
			}
			new_state = RING_STATE_IDLE;
			break;

		case RING_REQ_WTR_RUNNING:
			D("FSM: execute row 67");
			break;

		case RING_REQ_WTB_EXPIRES:
			D("FSM: execute row 68");
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				if (is_port_blocked(r->rpl_port)) {
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB|RAPS_FLAG_DNF);
					unblock_port(ring_other_port(r, r->rpl_port));
				} else {
					block_port(r->rpl_port);
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB);
					unblock_port(ring_other_port(r, r->rpl_port));
					flush_fdb();
				}
			}
			new_state = RING_STATE_IDLE;
			break;

		case RING_REQ_WTB_RUNNING:
			D("FSM: execute row 69");
			break;

		case RING_REQ_RAPS_NR_RB:
			D("FSM: execute row 70");
			if (r->is_rpl_owner) {
				stop_wtr_timer();
				stop_wtb_timer();
			}
			if (!r->is_rpl_owner && !r->is_rpl_neighbour) {
				unblock_port(r->port0);
				unblock_port(r->port1);
				stop_tx(r);
			}
			if (r->is_rpl_neighbour) {
				block_port(r->rpl_port);
				unblock_port(ring_other_port(r, r->rpl_port));
				stop_tx(r);
			}
			new_state = RING_STATE_IDLE;
			break;
		
		case RING_REQ_RAPS_NR:
			D("FSM: execute row 71");
			if (memcmp(r->node_id, remote_node_id, 6) < 0) {
				D(" - " MACSTR " is higher than " MACSTR, MAC2STR(remote_node_id), MAC2STR(r->node_id));
				unblock_nonfailed_ports(r);
				stop_tx(r);
			} 
			break;

		}
		break;
	}

	D("FSM: %s -> %s", RING_STATE_STR[r->state], RING_STATE_STR[new_state]);
	r->state = new_state;
}
	
struct ring *ring_create(struct ring_port *port0, struct ring_port *port1, struct ring_port *rpl_port, bool is_rpl_owner, bool is_rpl_neighbour, uint8_t *node_id) {
	struct ring *ret;


	//XXX: 
	// request enumeration
	enum ring_request req;
	for (req=RING_REQ_INVALID; req<=RING_REQ_CLEAR; req++) {
		D("request %02xh %s", req, ring_req_str(req));
	}



	ret = malloc(sizeof(struct ring));
	if (!ret)
		die("malloc failed");
	memset(ret, 0, sizeof(struct ring));

	// initialize 
	ret->ring_id = 1;
	ret->raps_vid = 4093;
	ret->port0 = port0;
	ret->port1 = port1;
	ret->rpl_port = rpl_port;
	ret->is_rpl_neighbour = is_rpl_neighbour;
	ret->is_rpl_owner = is_rpl_owner;
	memcpy(ret->node_id, node_id, 6);

	ret->local_request = RING_REQ_INVALID;

	ret->prev_request_port = 0;
	ret->prev_request = RING_REQ_INVALID;
	memset(ret->prev_request_remote_node_id, 0, 6);

	// nested raps structure and frame data
	ret->raps = ri_raps_create();
	ret->raps->vid = ret->raps_vid;
	ret->raps->ring_id = ret->ring_id;
	ret->raps->node_id = ret->node_id;
	ret->raps->subcode = 0;
	ret->raps->flags = 0xff;

	// allocate frame buffer for outgoing R-APS messages
	ret->raps_frame_len = ri_raps_get_len(ret->raps);
	ret->raps_frame = malloc(ret->raps_frame_len);
	if (!ret->raps_frame)
		die("malloc failed");


	// trigger event to get out of INIT state of our FSM
	ring_fsm(ret, RING_REQ_INVALID, 0, 0);

	return ret;
}

void ring_destroy(struct ring *r) {
	if (!r)
		return;

	free(r->raps_frame);
	ri_raps_destroy(r->raps);

	ring_port_destroy(r->port0);
	ring_port_destroy(r->port1);

	free(r);
}

void ring_send_raps(struct ring *ring) {
	D("sending R-APS(%s%s%s)", RAPS_REQUEST_STR[ring->raps->request], \
		ring->raps->flags&RAPS_FLAG_DNF ? ",DNF" : "", \
		ring->raps->flags&RAPS_FLAG_RB ? ",RB" : "" \
	);

	// 9.5 b) 
	// does not prevent R-APS messages, locally generated at the ERP control 
	// process, from being transmitted over both ring ports;
	ring_port_send(ring->port0, ring->raps_frame, ring->raps_frame_len);
	ring_port_send(ring->port1, ring->raps_frame, ring->raps_frame_len);
}

void ring_process_request(struct ring *r, enum ring_request request, struct ring_port *port, uint8_t *remote_node_id) {
	bool denied; 

	if (remote_node_id) {
		D("REQ: %s port=%s node_id=" MACSTR, \
			ring_req_str(request), \
			port ? port->name : "(null)", \
			MAC2STR(remote_node_id)
			);
	} else {
		D("REQ: %s port=%s node_id=(null)", \
			ring_req_str(request), \
			port ? port->name : "(null)" \
		);
	}


	// filter local requests based on ERPS version
	if (r->version == 1) {
		// MS and FS requests are not suported in earlier versions of ERPS
		switch(request) {
		case RING_REQ_FS:
		case RING_REQ_MS:
			E("ignored local FS/MS requests (not supported in v1)");
			return;
		default:
			// carry on
			break;
		}
	}
	
	// local SF is ignored, if ring is in FORCED_SWITCH state
	if (request == RING_REQ_SF) {
		if (r->state == RING_STATE_FORCED_SWITCH) {
			E("ignored SF signal! ring separation possible");
			return;
		}
	}

	// local CLEAR is allowed if:
	if (request == RING_REQ_CLEAR) {
		denied = true;

		//  - we are in MS or FS state, caused by local request (ring->local_request)
		if (r->local_request == RING_REQ_MS || r->local_request == RING_REQ_FS) 
			denied = false;

		//  - we are RPL owner and we are triggering reversion (either via WTR, WTB or manually)
		if (r->is_rpl_owner) {
			if (r->state != RING_STATE_FORCED_SWITCH &&
				r->state != RING_STATE_MANUAL_SWITCH &&
				r->state != RING_STATE_PROTECTION &&
				r->state != RING_STATE_IDLE) 
				denied = false;
		}

		if (denied) {
			E("nothing to be cleared -- we are not RPL owner, and we have no FS/MS active");
			return;
		}
	}

	// local request is overriden by other request?
	if (request > r->local_request) {
		// clear (overidden) local request
		r->local_request = RING_REQ_INVALID;

		// .. and retain new instead, if it is also local 
		switch (request) {
		case RING_REQ_SF:
		case RING_REQ_FS:
		case RING_REQ_MS:
			r->local_request = request;
			break;
		default:
			break;
		}
	} else if (request == r->local_request) {
		D(" - same request -- ignore (%s == %s)", ring_req_str(request), ring_req_str(r->local_request));
		return;
	} else {
		D(" - event is not severe enough (%s < %s)", ring_req_str(request), ring_req_str(r->local_request));
		return;
	}

	// top priority request
	ring_fsm(r, request, port, remote_node_id);
}



void ring_process_raps(struct ring *ring, uint8_t *data, int len, struct ring_port *origin) {
	struct ri_raps *raps;
	enum ring_request request;

	// guard timer
	if (ring->guard_timer_active) {
		if (tick_diff_msec(tick_now(), ring->guard_timer_active_since) < 500) {
			// block R-APS
			D("R-APS blocked by guard timer");
			return;
		} else {
			// guard timer expired
			D("guard timer expired");
			ring->guard_timer_active = false;
		}
	}

	raps = ri_raps_create();
	ri_raps_parse(raps, data, len);

	/*
	D("recvd R-APS(%s%s%s) from " MACSTR " on %s -- flags=%02xh", \
		RAPS_REQUEST_STR[raps->request], \
		raps->flags&RAPS_FLAG_DNF ? ",DNF" : "", \
		raps->flags&RAPS_FLAG_RB ? ",RB" : "", \
		MAC2STR(raps->node_id), \
		origin->name, raps->flags);
	*/

	switch (raps->request) {
	case RAPS_REQ_NR: request = raps->flags&RAPS_FLAG_RB ? RING_REQ_RAPS_NR_RB : RING_REQ_RAPS_NR; break;
	case RAPS_REQ_MS: request = RING_REQ_RAPS_MS; break;
	case RAPS_REQ_SF: request = RING_REQ_RAPS_SF; break;
	case RAPS_REQ_FS: request = RING_REQ_RAPS_FS; break;

	default:
		D("recv unhandled R-APS");
		return;
	}

	ring_process_request(ring, request, origin, raps->node_id);

	ri_raps_destroy(raps);
}
