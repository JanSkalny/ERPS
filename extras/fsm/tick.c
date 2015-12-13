#include "stdafx.h"
#include "tick.h"

#include <sys/time.h>

/**
 * get current system timestamp in milliseconds
 * !!! unrelated to EPOCH! !!!
 */
tick_t tick_now() {
	tick_t ret;

#ifdef CLOCK_MONOTONIC
	struct timespec ts;

	clock_gettime(CLOCK_MONOTONIC, &ts);

	ret =	(ts.tv_sec * 1000) +
			(ts.tv_nsec / 1000000);

#else
	struct timeval now;

	gettimeofday(&now,NULL);

	ret =	(now.tv_sec * 1000) +
			(now.tv_usec / 1000);
#endif

	return ret;
}

tick_t tick_diff_msec(tick_t t1, tick_t t2) {
	return t1 - t2;
}

