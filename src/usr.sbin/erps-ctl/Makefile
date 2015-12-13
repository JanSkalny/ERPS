.PATH:	${.CURDIR}/../../sys/net/

PROG=	erps-ctl
SRCS=	erps-ctl.c
BINDIR=	/usr/sbin

MAN=	erps-ctl.8

DPADD=  ${LIBL}
LDADD=  -ll

CFLAGS+=-I. -I${.CURDIR} -I${.CURDIR}/../../sys/ -g

.include <bsd.prog.mk>
