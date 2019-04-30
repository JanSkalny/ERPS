#pragma once

#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/time.h>

// netmap prerequisites
#include <machine/bus.h>
#include <sys/socket.h>
#include <sys/selinfo.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>

#define E D

#define READ_BYTE(o) ( *((uint16_t*)(o)) )
#define READ_WORD(o) ( ntohs(*((uint16_t*)(o))) )

#define WRITE_BYTE(o,x) ( *((uint8_t*)(o)) = (uint8_t)(x) )
#define WRITE_WORD(o,x) ( *((uint16_t*)(o)) = htons((uint16_t)(x)) )


#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

MALLOC_DECLARE(M_ERPS);

void *er_malloc(size_t);
void er_free(void *);

// not in the kernel
#define assert(val) {}

// let's use time measurement form <sys/kernel.h>
#define tick_t int
#define TICK_NOW ticks
#define TICK_DIFF_MSEC(a,b) (((a-b)*1000)/hz)
