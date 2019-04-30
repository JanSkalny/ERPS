#pragma once

struct er_port;
struct er_ring;

extern const char *RAPS_REQUEST_STR[16];

// R-APS protocol constants
enum raps_request {
	RAPS_REQ_NR    = 0b0000,
	RAPS_REQ_MS    = 0b0111,
	RAPS_REQ_SF    = 0b1011,
	RAPS_REQ_FS    = 0b1101,
	RAPS_REQ_EVENT = 0b1110,
};
#define RAPS_FLAG_DNF  0x40
#define RAPS_FLAG_RB   0x80

struct er_raps {
	uint8_t ring_id;
	uint16_t vid;
	uint8_t version;
	uint8_t flags;
	enum raps_request request;
	uint8_t subcode;
	uint8_t *node_id;
};


struct er_raps *er_raps_create(void);
void er_raps_destroy(struct er_raps *frame);
int er_raps_get_len(struct er_raps *frame);
bool er_raps_parse(struct er_raps *frame, uint8_t *data, int len);
bool er_raps_encode(struct er_raps *frame, uint8_t *data, int max_len);

bool er_is_raps_frame(uint8_t *data, int len);
bool er_is_keepalive_frame(uint8_t *data, int len);
bool er_is_valid_raps_frame(struct er_ring *ring, uint8_t *data, int len);
bool er_is_local_raps_frame(struct er_ring *ring, uint8_t *data, int len);
