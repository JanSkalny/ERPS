#pragma once

extern const char *RING_STATE_STR[6];
extern const char *RING_REQUEST_STR[15];

// ring states, as defined in table 10-2
enum ring_state {
	RING_STATE_INIT = 0,
	RING_STATE_IDLE,
	RING_STATE_PROTECTION,
	RING_STATE_MANUAL_SWITCH,
	RING_STATE_FORCED_SWITCH,
	RING_STATE_PENDING,
};

// ring requests, as defined in table 10-1
enum ring_request {
	RING_REQ_INVALID = 0,
	RING_REQ_RAPS_NR,
	RING_REQ_RAPS_NR_RB,
	RING_REQ_WTB_RUNNING,
	RING_REQ_WTB_EXPIRES,
	RING_REQ_WTR_RUNNING,
	RING_REQ_WTR_EXPIRES,
	RING_REQ_MS,
	RING_REQ_RAPS_MS,
	RING_REQ_RAPS_SF,
	RING_REQ_CLEAR_SF,
	RING_REQ_SF,
	RING_REQ_RAPS_FS,
	RING_REQ_FS,
	RING_REQ_CLEAR,
};

struct ring {
	// ring interfaces
	struct ring_port *port0;
	struct ring_port *port1;
	
	// ring configuration
	bool is_rpl_owner;
	bool is_rpl_neighbour;
	bool is_revertive;
	struct ring_port *rpl_port;
	char version;
	int ring_id;
	int raps_vid;
	uint8_t node_id[6];

	// guard timer
	bool guard_timer_active;
	tick_t guard_timer_active_since;

	// WTR timer
	bool wtr_timer_active;
	tick_t wtr_timer_active_since;

	// WTB timer
	bool wtb_timer_active;
	tick_t wtb_timer_active_since;

	// top priority local request -- either FS, MS or SF
	enum ring_request local_request; 

	// R-APS request processing state
	enum ring_state state;

	// FSM request de-duplication -- previous request & its params
	enum ring_request prev_request;
	struct ring_port *prev_request_port;
	uint8_t prev_request_remote_node_id[6];

	// R-APS message transmission
	bool is_sending_raps;
	struct ri_raps *raps;
	uint8_t *raps_frame;
	int raps_frame_len;
	int raps_bursts_remain;
};

struct ring *ring_create(struct ring_port *port0, struct ring_port *port1, struct ring_port *rpl_port, bool is_rpl_owner, bool is_rpl_neighbour, uint8_t *node_id);
void ring_destroy(struct ring *ring);
void ring_send_raps(struct ring *ring);
void ring_process_raps(struct ring *ring, uint8_t *data, int len, struct ring_port *origin);

void ring_process_request(struct ring *r, enum ring_request request, struct ring_port *port, uint8_t *raps_node_id);
void ring_fsm(struct ring *r, enum ring_request req, struct ring_port *port, uint8_t *raps_node_id);
void ring_timer(struct ring *ring);

struct ring_port* ring_other_port(struct ring *ring, struct ring_port *port);


