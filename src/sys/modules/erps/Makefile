# $FreeBSD$

.PATH: ${.CURDIR}/../../net/erps
.PATH: ${.CURDIR}/../../dev/erps

KMOD=erps
SRCS=stdafx.c er_raps.c er_netmap.c er_port.c er_ring.c erps.c

CFLAGS+=-I${.CURDIR}/../../../sys/
.ifdef (WITH_NETMAP_HEAD)
CFLAGS+=-DWITH_NETMAP_HEAD
.endif

CLEANFILES+=@ machine x86

WERROR=

.include <bsd.kmod.mk>
