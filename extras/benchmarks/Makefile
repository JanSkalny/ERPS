CC=clang
SVN_REV= $(shell svnversion -n .)
CFLAGS= -Wall -Wno-unused-variable -g -D'SVN_REV="$(SVN_REV)"' -I/usr/local/include -L/usr/local/lib
LIBS= -lpcap -lev

all: bridge_select bridge_ev

bridge_select:
	$(CC) -o bridge_select -DWITH_SELECT bridge.c $(CFLAGS) $(LIBS)

bridge_ev:
	$(CC) -o bridge_ev -DWITH_EV bridge.c $(CFLAGS) $(LIBS)

%.o: %.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	-rm -f bridge_select bridge_ev *.o

