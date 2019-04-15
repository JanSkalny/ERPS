#include "stdafx.h"

#include "ring_raps.h"

const char *RAPS_REQUEST_STR[16] = {
	"NR","1","2","3","4","5","6","MS",
	"8","9","10","SF","12","FS","EVENT","-",
};

struct ri_raps *ri_raps_create() {
	struct ri_raps *ret;

	ret = malloc(sizeof(struct ri_raps));
	if (!ret)
		die("malloc failed");
	memset(ret, 0, sizeof(struct ri_raps));

	return ret;
}

void ri_raps_destroy(struct ri_raps *raps) {
	free(raps);
}


/**
 * Get minimum data buffer size for given ri_raps structure
 */
int ri_raps_get_len(struct ri_raps *frame) {
	return 55;
}

/**
 * Parse given R-APS frame data into givet ri_raps structure
 */
bool ri_raps_parse(struct ri_raps *frame, uint8_t *data, int len) {
	uint8_t *p, request, subcode, flags, version, ring_id, cfm_op_code;
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

	// store results in new ri_raps structure
	memset(frame, 0, sizeof(struct ri_raps));
	frame->ring_id = ring_id;
	frame->request = request;
	frame->flags = flags;
	frame->node_id = data+2;
	
	return frame;
}

/**
 * Encode ri_raps structure into given data buffer of guaranteed size
 */
bool ri_raps_encode(struct ri_raps *frame, uint8_t *data, int max_len) {
	assert(data);

	D("ri_raps_encode");

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

