CC=clang
SVN_REV= $(shell svnversion -n .)
CFLAGS= -Wall -Wno-unused-variable -g -D'SVN_REV="$(SVN_REV)"' -I/usr/local/include -L/usr/local/lib
LIBS= -lpcap -lpthread -lev
SRCS= stdafx.c ring.c ring_raps.c ring_port.c ring_io.c tick.c ringd.c ringctl.c
OBJS= stdafx.o ring.o ring_raps.o ring_port.o ring_io.o tick.o 
DEPS=

OSNAME=$(shell uname -s)
CPUNAME=$(shell uname -m)

all: ringd ringctl

ringd: $(OBJS) ringd.o
	$(CC) -o ringd ringd.o $(OBJS) $(CFLAGS) $(LIBS)

ringctl: $(OBJS) ringctl.o
	$(CC) -o ringctl ringctl.o $(OBJS) $(CFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f ringd ringctl *.o

