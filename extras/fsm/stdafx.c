#include "stdafx.h"

void die(char *format, ...) {
	va_list args;
	char buf[1024];

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);

	fprintf(stdout, "%s: %s\n", sprintts(0), buf);
	exit(2);
}

void xlog(char *format, ...) {
	va_list args;
	char buf[1024];

	va_start(args, format);
	vsnprintf(buf, sizeof(buf), format, args);
	va_end(args);
	fprintf(stdout, "%s: %s\n", sprintts(0), buf);
}

void dump_bytes(uint8_t *data, int len) {
	int i=0;
	while (len-- > 0) {
		printf("%02x", *(data++));
		if (i++%4 == 3)
			printf(" ");
	}
	// printf("\n");
}

char* sprintts(struct timeval *tv) {
	static char res[400];
	time_t time;
	struct timeval tv_now;
	struct tm *tm;

	if (!tv) {
		gettimeofday(&tv_now, 0);
		tv = &tv_now;
	}

	time = tv->tv_sec;
	tm = localtime(&time);

	//strftime(res, sizeof(res), "%Y-%m-%d %H:%M:%S", tm);
	strftime(res, sizeof(res), "%H:%M:%S", tm);
	snprintf(res+strlen(res), sizeof(res), ".%03d", (int)(tv->tv_usec/1000));

	return res;
}
