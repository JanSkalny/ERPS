#pragma once

extern const char *RAPS_REQUEST_STR[16];

// R-APS protocol constants
enum raps_request {
	RAPS_REQ_NR		= 0b0000,
	RAPS_REQ_MS		= 0b0111,
	RAPS_REQ_SF		= 0b1011,
	RAPS_REQ_FS		= 0b1101,
	RAPS_REQ_EVENT	= 0b1110,
};
#define RAPS_FLAG_DNF	0x40
#define RAPS_FLAG_RB	0x80

struct ri_raps {
	uint8_t ring_id;
	uint16_t vid;
	uint8_t version;
	uint8_t flags;
	enum raps_request request;
	uint8_t subcode;
	uint8_t *node_id;
};


struct ri_raps *ri_raps_create();
void ri_raps_destroy(struct ri_raps *frame);
int ri_raps_get_len(struct ri_raps *frame);
bool ri_raps_parse(struct ri_raps *frame, uint8_t *data, int len);
bool ri_raps_encode(struct ri_raps *frame, uint8_t *data, int max_len);

