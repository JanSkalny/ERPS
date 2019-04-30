#include "stdafx.h"

#include "er_ring.h"

#include "er_raps.h"

const char *RAPS_REQUEST_STR[16] = {
	"NR","1","2","3","4","5","6","MS",
	"8","9","10","SF","12","FS","EVENT","-",
};

struct er_raps *er_raps_create(void) {
	struct er_raps *ret;

	ret = er_malloc(sizeof(struct er_raps));
	if (!ret) {
		E("malloc failed!");
		return 0;
	}
	memset(ret, 0, sizeof(struct er_raps));

	return ret;
}

void er_raps_destroy(struct er_raps *raps) {
	er_free(raps);
}

bool er_is_raps_frame(uint8_t *data, int len) {
        // verify length
        if (len < 55)
                return false;

        // verify destination address
        if (memcmp(data, "\x01\x19\xA7\x00\x0", 5) != 0)
                return false;

        // XXX: verify VLAN tag

        return true;
}

bool er_is_keepalive_frame(uint8_t *data, int len) {
	return false;
}

bool er_is_valid_raps_frame(struct er_ring *ring, uint8_t *data, int len) {
	return true;
}

/**
 * Get minimum data buffer size for given er_raps structure
 */
int er_raps_get_len(struct er_raps *frame) {
	return 55;
}

/**
 * Parse given R-APS frame data into givet er_raps structure
 */
bool er_raps_parse(struct er_raps *frame, uint8_t *data, int len) {
	uint8_t request, flags, version, ring_id, cfm_op_code;
	uint16_t eth_type;

	assert (data && frame);

	// previous function verified:
	//  - destination MAC address
	//  - VLAN tag
	// (as part of R-APS routing and ring identification)

	// does it meet minimum requirements?
	if (len < 55) {
		E("R-APS frame is too short");
		return false;
	}

	// -- Ethernet2 header --
	ring_id = READ_BYTE(data+5);
	eth_type = READ_WORD(data+12);
	data += 14;

	// -- dot1q header (might be missing)
	if (eth_type == 0x8100) {
		READ_WORD(data+2);
		data += 4;
	}
	E("R-APS eth_type is %04x", eth_type);
	
	// -- CFM header --
	version = READ_BYTE(data) & 0x1f;
	if (version > 2) {
		E("R-APS frame with unsupported version %d", version);
		return false;
	}
	cfm_op_code = READ_BYTE(data+1);
	if (cfm_op_code != 40) {
		E("R-APS frame with invalid cfm op_code %d", cfm_op_code);
		return false;
	}
	data += 4;

	// -- R-APS specific information
	request = (enum raps_request)((READ_BYTE(data)&0xf0)>>4);
	switch (request) {
	case RAPS_REQ_NR: 
	case RAPS_REQ_MS: 
	case RAPS_REQ_FS: 
	case RAPS_REQ_SF: 
		break;

	default:
		E("R-APS with unknown state/request code");
		return false;
	}

	flags = READ_BYTE(data+1)&0xe0;

	// store results in new er_raps structure
	memset(frame, 0, sizeof(struct er_raps));
	frame->ring_id = ring_id;
	frame->request = request;
	frame->flags = flags;
	frame->node_id = data+2;
	
	return frame;
}

/**
 * Encode er_raps structure into given data buffer of guaranteed size
 */
bool er_raps_encode(struct er_raps *frame, uint8_t *data, int max_len) {
	assert(data);

	D("er_raps_encode");

	if (max_len < 55)
		return false;

	memset(data, 0, 55);

	// -- ethernet 2 --
	memcpy(data, "\x01\x19\xa7\x00\x00", 5);
	WRITE_BYTE(data+5, frame->ring_id);
	memcpy(data+6, frame->node_id, 6);
	WRITE_WORD(data+12, 0x8100);
	data += 14;

	// -- 802.1q --
	WRITE_WORD(data, frame->vid);
	WRITE_WORD(data+2, 0x8902);
	data += 4;

	// -- CFM OAM
	WRITE_BYTE(data, 0x02);
	WRITE_BYTE(data+1, 40);
	WRITE_BYTE(data+3, 32);
	data += 4;

	// -- R-APS specific information
	WRITE_BYTE(data, ((uint8_t)frame->request)<<4 | frame->subcode);
	WRITE_BYTE(data+1, frame->flags);
	memcpy(data+2, frame->node_id, 6);
	
	return true;
}


bool er_is_local_raps_frame(struct er_ring *ring, uint8_t *data, int len) {
    uint16_t eth_type;
    int offset = 0;

    if (len < 55)
        return false;

    eth_type = READ_WORD(data+12);
    if (eth_type == 0x8100)
        offset += 4;

    if (memcmp(data+14+offset+6, ring->node_id, 6) != 0)
        return false;

    return true;
}


