#include "stdafx.h"

MALLOC_DEFINE(M_ERPS, "erps", "ethernet ring protection switching");

void *er_malloc(size_t size) {
    return malloc(size, M_ERPS, M_NOWAIT | M_ZERO);
}

void er_free(void *addr) {
    free(addr, M_ERPS);
}

