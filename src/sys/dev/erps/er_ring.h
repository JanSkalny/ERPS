#pragma once

struct er_port;
struct er_raps;

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

struct er_ring {
	// ring interfaces
	struct er_port *port0;
	struct er_port *port1;
	
	// bsd stuff
	struct mtx mtx;
	struct callout callout;
#ifdef FBSD12
	void * vale_auth_token;
#endif
	char vale_name[IFNAMSIZ];

	// ring configuration
	bool is_rpl_owner;
	bool is_rpl_neighbour;
	bool is_revertive;
	struct er_port *rpl_port;
	char version;
	int id;
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
	struct er_port *prev_request_port;
	uint8_t prev_request_remote_node_id[6];

	// R-APS message transmission
	bool is_sending_raps;
	struct er_raps *raps;
	uint8_t *raps_frame;
	int raps_frame_len;
	int raps_bursts_remain;

	// timer configuration
	uint32_t guard_time;
	uint32_t wtr_time;
	uint32_t wtb_time;

	// 
	bool active;
};

struct er_ring *er_ring_create(uint16_t ring_id, char *port0_name, char *port1_name, uint8_t rpl_owner, uint8_t rpl_neighbour, uint8_t *node_id);
void er_ring_destroy(struct er_ring *ring);
void er_destroy_all_rings(void);
struct er_ring *er_lookup_ring(uint16_t ring_id);

struct er_port *er_lookup_ring_port(struct er_ring *ring, char *name);
struct er_port* er_ring_other_port(struct er_ring *ring, struct er_port *port);

bool er_activate_ring(struct er_ring *ring);
bool er_deactivate_ring(struct er_ring *ring);

bool er_forward_lookup(struct er_ring *ring, struct er_port *recv_port, uint8_t *data, int len);


//void er_ring_timer(struct er_ring *ring);
//void er_ring_process_raps(struct er_ring *ring, uint8_t *data, int len, struct er_port *origin);

#define ER_RING_LOCK_INIT(_ring) mtx_init(&(_ring)->mtx, "erps_ring", NULL, MTX_DEF)
#define ER_RING_LOCK_DESTROY(_ring)  mtx_destroy(&(_ring)->mtx)
#define ER_RING_LOCK(_ring)      mtx_lock(&(_ring)->mtx)
#define ER_RING_UNLOCK(_ring)    mtx_unlock(&(_ring)->mtx)
#define ER_RING_LOCK_ASSERT(_ring)   mtx_assert(&(_ring)->mtx, MA_OWNED)


static struct er_ring *er_global;
