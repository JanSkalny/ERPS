#pragma once

typedef uint64_t tick_t;

#define TICK_ZERO			0
#define TICK_INFINITY		-1

tick_t tick_diff_msec(tick_t t1, tick_t t2);
tick_t tick_now();

