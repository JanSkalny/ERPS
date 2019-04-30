#include "stdafx.h"

#include <net/erps.h>

//#include "tick.h"
#include "er_raps.h"
#include "er_port.h"
#include "er_netmap.h"

#include "er_ring.h"

//XXX: should be 300
#define WTR_TIMER 15

static struct er_ring *er_global = 0;

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

const char * ring_req_str(enum ring_request req);
void start_wtr_timer(struct er_ring *ring);
void stop_wtr_timer(struct er_ring *ring);
void start_wtb_timer(struct er_ring *ring);
void stop_wtb_timer(struct er_ring *ring);
void start_guard_timer(struct er_ring *ring);
void stop_guard_timer(struct er_ring *ring);
void block_port(struct er_port *port);
void unblock_port(struct er_port *port);
bool is_port_blocked(struct er_port *port);
void flush_fdb(void);
void unblock_nonfailed_ports(struct er_ring *r);
void start_tx(struct er_ring *r, enum raps_request request, uint8_t flags);
void stop_tx(struct er_ring *r);

void ring_fsm(struct er_ring *r, enum ring_request req, struct er_port *port, uint8_t *raps_node_id);
void ring_process_request(struct er_ring *r, enum ring_request request, struct er_port *port, uint8_t *raps_node_id);
void process_raps_frame(struct er_ring *ring, uint8_t *data, int len, struct er_port *origin);

const char * ring_req_str(enum ring_request req) {
	//static char *s_invalid = "INVALID";
	if (req < RING_REQ_INVALID || req > RING_REQ_CLEAR) {
		E("invalid request! %02xh", req);
		return "invalid!";
	}
	
	return RING_REQUEST_STR[req];
}


// Ring FSM action
// ------------------------------------------

void start_wtr_timer(struct er_ring *ring) {
	D(" - start WTR timer");
	ring->wtr_timer_active_since = TICK_NOW;
	ring->wtr_timer_active = true;
}
void stop_wtr_timer(struct er_ring *ring) {
	D(" - stop WTR timer");
	ring->wtr_timer_active = false;
}
void start_wtb_timer(struct er_ring *ring) {
	D(" - start WTB timer");
	ring->wtb_timer_active_since = TICK_NOW;
	ring->wtb_timer_active = true;
}
void stop_wtb_timer(struct er_ring *ring) {
	D(" - stop WTB timer");
	ring->wtb_timer_active = false;
}
void start_guard_timer(struct er_ring *ring) {
	D(" - start guard timer");
	ring->guard_timer_active_since = TICK_NOW;
	ring->guard_timer_active = true;
}
void stop_guard_timer(struct er_ring *ring) {
	D(" - stop guard timer");
	ring->guard_timer_active = false;
}
void block_port(struct er_port *port) {
	D(" - block port %s", port->name);
	er_port_block(port);
}
void unblock_port(struct er_port *port) {
	D(" - unblock port %s", port->name);
	er_port_unblock(port);
}
bool is_port_blocked(struct er_port *port) {
	return er_port_is_blocked(port);
}
void flush_fdb(void) {
	D(" - flush fdb");
}

struct er_port* er_ring_other_port(struct er_ring *r, struct er_port *port) {
	return (port == r->port1) ? r->port0 : r->port1;
}

void unblock_nonfailed_ports(struct er_ring *r) {
	D(" - unblock non-failed ports");

	if (!r->port0->is_failed)
		unblock_port(r->port0);
	if (!r->port1->is_failed)
		unblock_port(r->port1);
}

void start_tx(struct er_ring *r, enum raps_request request, uint8_t flags) {
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
		er_raps_encode(r->raps, r->raps_frame, r->raps_frame_len);
		r->raps_bursts_remain = 3;
	}

	r->is_sending_raps = true;

	D("start_tx: ring=%p", r);
}

void stop_tx(struct er_ring *r) {
	D(" - stop R-APS Tx");

	// stop sending R-APS, as requested
	r->is_sending_raps = false;
	D("stop_tx: ring=%p", r);
}



void ring_fsm(struct er_ring *r, enum ring_request req, struct er_port *port, uint8_t *remote_node_id) {
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
		if (req == r->prev_request) {
			E("impossible duplicit request received! %02xh (%s)", req, ring_req_str(req));
			return;
		}
		break;
	}

	if (dupe) {
		D("FSM: same request ignored");
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
		stop_wtr_timer(r);
		stop_wtb_timer(r);
		if (r->is_rpl_owner) {
			block_port(r->rpl_port);
			unblock_port(er_ring_other_port(r, r->rpl_port));	
			start_tx(r, RAPS_REQ_NR, 0);
			if (r->is_revertive) 
				start_wtr_timer(r);
		} else if (r->is_rpl_neighbour) {
			block_port(r->rpl_port);
			unblock_port(er_ring_other_port(r, r->rpl_port));
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
				unblock_port(er_ring_other_port(r, port));
			} else {
				// port is active... block and notify
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(er_ring_other_port(r, port));
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
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(er_ring_other_port(r, port));
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
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_MS, 0);
				unblock_port(er_ring_other_port(r, port));
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
			unblock_port(er_ring_other_port(r, r->rpl_port));
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
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(er_ring_other_port(r, port));
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
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(er_ring_other_port(r, port));
				flush_fdb();
			}
			break;

		case RING_REQ_CLEAR_SF:
			D("FSM: execute row 20");
			start_guard_timer(r);
			start_tx(r, RAPS_REQ_NR, 0);
			if (r->is_rpl_owner && r->is_revertive)
				start_wtr_timer(r);
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
				start_wtr_timer(r);
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
					start_wtb_timer(r);
			}
			new_state = RING_STATE_PENDING;
			break;

		case RING_REQ_FS:
			D("FSM: execute row 31");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_FS, RAPS_FLAG_DNF);
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(er_ring_other_port(r, port));
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
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(er_ring_other_port(r, port));
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
					start_wtb_timer(r);
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
				start_wtb_timer(r);
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
					start_wtb_timer(r);
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
				start_wtb_timer(r);
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
				stop_wtr_timer(r);
				stop_wtb_timer(r);
				if (is_port_blocked(r->rpl_port)) {
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB|RAPS_FLAG_DNF);
					unblock_port(er_ring_other_port(r, r->rpl_port));
				} else {
					block_port(r->rpl_port);
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB);
					unblock_port(er_ring_other_port(r, r->rpl_port));
					flush_fdb();
				}
			}
			new_state = RING_STATE_IDLE;
			break;

		case RING_REQ_FS:
			D("FSM: execute row 59");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_FS, RAPS_FLAG_DNF);
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_FS, 0);
				unblock_port(er_ring_other_port(r, port));
				flush_fdb();
			}
			if (r->is_rpl_owner) {
				stop_wtr_timer(r);
				stop_wtb_timer(r);
			}
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_RAPS_FS:
			D("FSM: execute row 60");
			unblock_port(r->port0);
			unblock_port(r->port1);
			stop_tx(r);
			if (r->is_rpl_owner) {
				stop_wtr_timer(r);
				stop_wtb_timer(r);
			}
			new_state = RING_STATE_FORCED_SWITCH;
			break;

		case RING_REQ_SF:
			D("FSM: execute row 61");
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_SF, RAPS_FLAG_DNF);
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_SF, 0);
				unblock_port(er_ring_other_port(r, port));
				flush_fdb();
			}
			if (r->is_rpl_owner) {
				stop_wtr_timer(r);
				stop_wtb_timer(r);
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
				stop_wtr_timer(r);
				stop_wtb_timer(r);
			}
			new_state = RING_STATE_PROTECTION;
			break;

		case RING_REQ_RAPS_MS:
			D("FSM: execute row 64");
			unblock_nonfailed_ports(r);
			stop_tx(r);
			if (r->is_rpl_owner) {
				stop_wtr_timer(r);
				stop_wtb_timer(r);
			}
			new_state = RING_STATE_MANUAL_SWITCH;
			break;

		case RING_REQ_MS:
			D("FSM: execute row 65");
			if (r->is_rpl_owner) {
				stop_wtr_timer(r);
				stop_wtb_timer(r);
			}
			if (is_port_blocked(port)) {
				start_tx(r, RAPS_REQ_MS, RAPS_FLAG_DNF);
				unblock_port(er_ring_other_port(r, port));
			} else {
				block_port(port);
				start_tx(r, RAPS_REQ_MS, 0);
				unblock_port(er_ring_other_port(r, port));
				flush_fdb();
			}
			new_state = RING_STATE_MANUAL_SWITCH;
			break;

		case RING_REQ_WTR_EXPIRES:
			D("FSM: execute row 66");
			if (r->is_rpl_owner) {
				stop_wtb_timer(r);
				if (is_port_blocked(r->rpl_port)) {
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_DNF|RAPS_FLAG_RB);
					unblock_port(er_ring_other_port(r, r->rpl_port));
				} else {
					block_port(r->rpl_port);
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB);	
					unblock_port(er_ring_other_port(r, r->rpl_port));
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
				stop_wtr_timer(r);
				if (is_port_blocked(r->rpl_port)) {
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB|RAPS_FLAG_DNF);
					unblock_port(er_ring_other_port(r, r->rpl_port));
				} else {
					block_port(r->rpl_port);
					start_tx(r, RAPS_REQ_NR, RAPS_FLAG_RB);
					unblock_port(er_ring_other_port(r, r->rpl_port));
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
				stop_wtr_timer(r);
				stop_wtb_timer(r);
			}
			if (!r->is_rpl_owner && !r->is_rpl_neighbour) {
				unblock_port(r->port0);
				unblock_port(r->port1);
				stop_tx(r);
			}
			if (r->is_rpl_neighbour) {
				block_port(r->rpl_port);
				unblock_port(er_ring_other_port(r, r->rpl_port));
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
	
struct er_ring *er_ring_create(uint16_t ring_id, char *port0_name, char *port1_name, uint8_t rpl_owner, uint8_t rpl_neighbour, uint8_t *node_id) {
	struct er_ring *ret;

	//XXX: 
	// request enumeration
	enum ring_request req;
	D("== START VALID RING REQUESTS ==");
	for (req=RING_REQ_INVALID; req<=RING_REQ_CLEAR; req++) {
		D("request %02xh %s", req, ring_req_str(req));
	}
	D("== END VALID RING REQUESTS ==");

	ret = (struct er_ring*) er_malloc(sizeof(struct er_ring));
	if (!ret) {
		E("malloc failed");
		return 0;
	}

	ret->active = false;
	ret->id = ring_id;
	ret->is_revertive = 0;
	ret->raps_vid = 4093;
	ret->raps = 0;

	// netmap stuff
	//XXX: should move this to er_netmap
	snprintf(ret->vale_name, sizeof(ret->vale_name)-1, "valeERPS%d:", ring_id);

#ifdef FBSD12
	ret->vale_auth_token = NULL;
#endif

#ifdef FBSD12
	int err;

	NMG_LOCK();
	ret->vale_auth_token = netmap_vale_create(ret->vale_name, &err);
	NMG_UNLOCK();
	if (err != 0 || ret->vale_auth_token==NULL) {
		E("failed to create vale bridge: err=%d", err);
		goto cleanup;
	}
#endif

	ret->port0 = er_port_create(ret, port0_name);
	if (!ret->port0) {
		E("failed to open port %s", port0_name);
		goto cleanup;
	}

	ret->port1 = er_port_create(ret, port1_name);
	if (!ret->port1) {
		E("failed to open port %s", port1_name);
		goto cleanup;
	}

	er_netmap_regops(ret);

	// set up RPL
	if (rpl_owner != ERPS_RPL_NONE) {
		ret->is_rpl_owner = 1;
		ret->rpl_port = rpl_owner == ERPS_RPL_PORT0 ? ret->port0 : ret->port1;
	}
	if (rpl_neighbour != ERPS_RPL_NONE) {
		ret->is_rpl_neighbour = 1;
		ret->rpl_port = rpl_neighbour == ERPS_RPL_PORT0 ? ret->port0 : ret->port1;
	}

	// nested raps structure and frame data
	ret->raps = er_raps_create();
	ret->raps->vid = ret->raps_vid;
	ret->raps->ring_id = ret->id;
	ret->raps->node_id = ret->node_id;
	ret->raps->subcode = 0;
	ret->raps->flags = 0xff;

	// allocate frame buffer for outgoing R-APS messages
	ret->raps_frame_len = er_raps_get_len(ret->raps);
	ret->raps_frame = er_malloc(ret->raps_frame_len);
	if (!ret->raps_frame) {
		E("failed to create raps frame");
		goto cleanup;
	}

	D("preparing fsm...");

	// prepare FSM 
	ret->local_request = RING_REQ_INVALID;
	ret->prev_request_port = 0;
	ret->prev_request = RING_REQ_INVALID;
	memset(ret->prev_request_remote_node_id, 0, 6);

	// trigger event to get out of INIT state of our FSM
	ring_fsm(ret, RING_REQ_INVALID, 0, 0);

	return ret;

cleanup:
/*
	if (ret->vale_auth_token != NULL) {
		E("netmap_vale_destroy");
		netmap_vale_destroy(vale_name, ret->vale_auth_token);
	}

*/
	er_free(ret);

	return 0;
}

void er_ring_destroy(struct er_ring *ring) {
	// deactivate ring first
	if (ring->active)
		er_deactivate_ring(ring);

	// remove all ring ports 
	if (ring->port0)
		er_port_destroy(ring, ring->port0);
	if (ring->port1)
		er_port_destroy(ring, ring->port1);
#ifdef FBSD12
	// cleanup netmap stuff
	if (ring->vale_auth_token) {
		netmap_vale_destroy(ring->vale_name, ring->vale_auth_token);
	}
#endif

	// release allocated memory
	if (ring->raps)
		er_raps_destroy(ring->raps);
	er_free(ring);
}

/*
void er_ring_send_raps(struct er_ring *ring);
void ring_send_raps(struct er_ring *ring) {
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
*/
void ring_process_request(struct er_ring *r, enum ring_request request, struct er_port *port, uint8_t *remote_node_id) {
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

	// local clear SF signal, if nothing is failed
	//TODO: check port state!
	if (r->local_request == RING_REQ_SF && request == RING_REQ_CLEAR_SF) 
		r->local_request = RING_REQ_INVALID;

	// local request is overriden by other request?
	if (request > r->local_request) {
		// clear (overidden) local request
		r->local_request = RING_REQ_INVALID;

		// .. and retain new instead, if it is also local 
		switch (request) {
		case RING_REQ_SF:
		case RING_REQ_FS:
		case RING_REQ_MS:
			D("local_request %s -> %s", ring_req_str(r->local_request), ring_req_str(request));
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


struct er_port *er_lookup_ring_port(struct er_ring *ring, char *name) {
        if (strcmp(ring->port0->name, name) == 0)
                return ring->port0;
        if (strcmp(ring->port1->name, name) == 0)
                return ring->port1;

        return 0;
}

/*
XXX:
void ring_timer(struct er_ring *ring) {
	// WTR timer
	if (ring->wtr_timer_active) {
		if (tick_diff_msec(tick_now(), ring->wtr_timer_active_since) >= WTR_TIMER * 1000) {
			// WTR expired
			D("[local] WTR expired");
			ring_process_request(ring, RING_REQ_WTR_EXPIRES, 0, 0);
			ring->wtr_timer_active = 0;
		}
	}
}
*/

void process_raps_frame(struct er_ring *ring, uint8_t *data, int len, struct er_port *origin) {
	struct er_raps *raps;
	enum ring_request request;

	D("processing frame!");

	// guard timer
	if (ring->guard_timer_active) {
		if (TICK_DIFF_MSEC(TICK_NOW, ring->guard_timer_active_since) < 500) {
			// block R-APS
			D("R-APS blocked by guard timer");
			return;
		} else {
			// guard timer expired
			D("guard timer expired");
			ring->guard_timer_active = false;
		}
	}

	raps = er_raps_create();
	if (!er_raps_parse(raps, data, len)) {
		D("ignored invalid R-APS");
		return;
	}

	D("recvd R-APS(%s%s%s) from " MACSTR " on %s -- flags=%02xh", \
		RAPS_REQUEST_STR[raps->request], \
		raps->flags&RAPS_FLAG_DNF ? ",DNF" : "", \
		raps->flags&RAPS_FLAG_RB ? ",RB" : "", \
		MAC2STR(raps->node_id), \
		origin->name, raps->flags);

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

	er_raps_destroy(raps);
}

bool er_activate_ring(struct er_ring *ring) {
	//XXX: start FSM thread
	D("activating ERPS%d", ring->id);

	ER_RING_LOCK_INIT(ring);
	callout_init_mtx(&ring->callout, &ring->mtx, 0);

	// start sending R-APS messages
	ER_RING_LOCK(ring);
	//XXX:callout_reset(&ring->callout, 10*hz, er_send_raps, ring);
	ER_RING_UNLOCK(ring);

	ring->active = true;

	return true;
}

bool er_deactivate_ring(struct er_ring *ring) {
	//XXX: stop FSM thread
	D("deactivating ERPS%d", ring->id);

	ring->active = false;

	// destroy callouts
	callout_drain(&ring->callout);
	ER_RING_LOCK_DESTROY(ring);

	return true;
}

bool er_forward_lookup(struct er_ring *ring, struct er_port *recv_port, uint8_t *data, int len) {
	struct er_port *other_port;

	if (er_is_keepalive_frame(data, len)) {
		// send keepalive reply and drop original request
		//XXX:er_send_keepalive_reply(recv_port);
		return false;

	} else if (er_is_raps_frame(data, len)) {
		// drop all R-APS messages from non-"ring link" ports
		if (!recv_port) {
			E("R-APS on non-ring link");
			return false;
		}

		D("recvd R-APS on %d (%s)", recv_port->idx, recv_port->name);

		int xlen = 20, i=0;;
		uint8_t *xdata = data;
		while (xlen-- > 0) {
			printf("%02x", *(xdata++));
			if (i++%4 == 3)
				printf(" ");
		}


		// validate R-APS before processing/forwarding
		if (er_is_valid_raps_frame(ring, data, len)) {
			//XXX: ignore localy originated R-APS frames
			//TODO: find this in the spec!
			if (er_is_local_raps_frame(ring, data, len)) {
				D("drop local R-APS");
				return false;
			}

			// ref. G.8032 9.5 c)
			process_raps_frame(ring, data, len, recv_port);

			if ((other_port = er_ring_other_port(ring, recv_port))) {
				// don't forward R-APS messages to/from blocked ports
				// ref: G.8032 9.5 a)
				if (!recv_port->is_blocked && !other_port->is_blocked) {
					// forward to other port
					D("forward R-APS to %d (%s)", other_port->idx, other_port->name);
					return other_port->idx;
				}
			}
		}

		// drop
		return false;
	}

	// if source port is blocked by ERPS, drop frame
	// destination port is blocked via VALE and NAF_SW_ONLY flag
	if (recv_port && recv_port->is_blocked) 
		return false;

	return true;
}


// ring list
void er_destroy_all_rings(void) {
        //XXX: add multi-ring support
        if (er_global) {
		D("destroy global ring");
                er_ring_destroy(er_global);
                er_global = 0;
        }
}

struct er_ring *er_lookup_ring(uint16_t ring_id) {
        //XXX: add multi-ring support
        return er_global;
}



