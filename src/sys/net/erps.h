#ifndef _NET_ERPS_H
#define _NET_ERPS_H

#include <net/if.h>

struct erreq {
	// EAST/WEST port names and state (when reporting status)
	// or port being modified (port0)
	char		er_port0[IFNAMSIZ];
	char		er_port1[IFNAMSIZ];
	uint8_t		er_port0_failed;
	uint8_t		er_port1_failed;
	uint8_t		er_port0_blocked;
	uint8_t		er_port1_blocked;

	// ring configuration
	uint8_t		er_node_id[6];	// node-id
	uint8_t		er_id;			// ring id (used in last octet of mac address)
	uint8_t		er_version;		// ERPS protocol version 1 and 2 are supproted
	uint32_t	er_guard_time;	// R-APS guard timer (when ...) (milliseconds)
	uint32_t	er_wtr_time;	// WTR timer (milliseconds)
	//XXX: derived from guard_time	
	// uint32_t	er_wtb_time;	// WTB timer (milliseconds) 
	uint16_t	er_raps_vid;	// VID for R-APS transmission/reception
	uint8_t		er_revertive;	// is revertive operation?

	// RPL configuration
	uint16_t	er_rpl_port;
#define ERPS_RPL_NONE	0
#define ERPS_RPL_PORT0	1
#define ERPS_RPL_PORT1	2
	uint8_t		er_rpl_owner;
	uint8_t		er_rpl_neighbour;

	// R-APS statistics
	//TODO:
	
	// ring management commands
	uint16_t	er_cmd;			
#define ERPS_CLEAR		1
#define ERPS_MS			2
#define ERPS_FS			3
#define ERPS_FAIL		4
#define ERPS_RECOVER	5

};

#define ERIOCINIT	_IOW('i', 1, struct erreq)
#define ERIOCADDIF	_IOW('i', 2, struct erreq)
#define ERIOCDELIF	_IOW('i', 3, struct erreq)
#define ERIOCSTATUS	_IOWR('i', 4, struct erreq)
#define ERIOCCOMMAND	_IOWR('i', 5, struct erreq)

#endif
