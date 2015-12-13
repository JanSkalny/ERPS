#pragma once

#include <assert.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdarg.h>
#include <time.h>

#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include <arpa/inet.h>
#include <pcap/pcap.h>
#include <pthread.h>
#include <ev.h>

#define E
#define D(format, ...) \
	do { \
		struct timeval __xxts; \
		gettimeofday(&__xxts, 0); \
		fprintf(stdout, "%03d.%06d %s+%-4d %*s" format "\n", \
			(int)__xxts.tv_sec % 1000, (int)__xxts.tv_usec, \
			__FUNCTION__, __LINE__, (int)(strlen(__FUNCTION__) > 25 ? 1 : 25-strlen(__FUNCTION__)), " ", ##__VA_ARGS__); \
	} while (0);

//fprintf(stderr, "%s:%d:%s(): " fmt, __FILE__, __LINE__, __func__, __VA_ARGS__); 


void xlog(char *format, ...);
void die(char *format, ...);
void dump_bytes(uint8_t *data, int len);
char* sprintts(struct timeval *tv);


#define READ_BYTE(o) ( *((uint16_t*)(o)) )
#define READ_WORD(o) ( ntohs(*((uint16_t*)(o))) )

#define WRITE_BYTE(o,x) ( *((uint8_t*)(o)) = (uint8_t)(x) )
#define WRITE_WORD(o,x) ( *((uint16_t*)(o)) = htons((uint16_t)(x)) )

#define MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#define MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"

