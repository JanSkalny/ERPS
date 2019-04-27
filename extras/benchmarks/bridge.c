#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <unistd.h>
#include <pcap/pcap.h>

#ifdef WITH_SELECT
#include <sys/select.h>
#endif

#ifdef WITH_EV
#include <ev.h>
#endif



#define MAX_RECV_LEN 1514

#define D(format, ...) \
	do { \
		struct timeval __xxts; \
		gettimeofday(&__xxts, 0); \
		fprintf(stdout, "%03d.%06d %s+%-4d %*s" format "\n", \
			(int)__xxts.tv_sec % 1000, (int)__xxts.tv_usec, \
			__FUNCTION__, __LINE__, (int)(strlen(__FUNCTION__) > 25 ? 1 : 25-strlen(__FUNCTION__)), " ", ##__VA_ARGS__); \
	} while (0);

struct iface {
	pcap_t *dev;
	char *name;
	int fd;
};

struct iface* iface_open(char *dev) {
	struct iface *iface;
	char err[PCAP_ERRBUF_SIZE];

	iface = malloc(sizeof(struct iface));
	iface->name = strdup(dev);
	iface->dev = pcap_open_live(dev, MAX_RECV_LEN, 1, 1, err);
	pcap_setdirection(iface->dev, PCAP_D_IN);
	pcap_setnonblock(iface->dev, 1, err);
	iface->fd = pcap_get_selectable_fd(iface->dev);

	return iface;
}

int iface_recv(struct iface* iface, uint8_t **buf) {
	struct pcap_pkthdr *hdr;
	const u_char *data;

	if (pcap_next_ex(iface->dev, &hdr, &data) <= 0) {
		D("pcap_next_ex failed");
		return 0;
	}

	*buf = (uint8_t*)data;
	return hdr->caplen;
}

bool iface_send(struct iface *iface, uint8_t *data, int len) {
	if (pcap_inject(iface->dev, data, len) < 0)
		return false;
	return true;
}

#ifdef WITH_EV

struct port_io {
	struct ev_io io;
	struct iface *recv_port;
	struct iface *send_port;
};

static void port_cb(EV_P_ struct ev_io *_ev, int revents) {
	struct port_io *ev = (struct port_io*) _ev;
	uint8_t *data;
	int len;

	len = iface_recv(ev->recv_port, &data);
	if (len > 0)
		iface_send(ev->send_port, data, len);
}

#endif


int main(int argc, char **argv) {
	struct iface *port1, *port2;
	int junk[2];

	if (argc != 3) 
		return 1;

	// simulate some opened file descriptors
	pipe(junk);
	pipe(junk);
	pipe(junk);

	// open interfaces
	port1 = iface_open(argv[1]);
	port2 = iface_open(argv[2]);

#ifdef WITH_SELECT
	fd_set fds, fds_orig;
	struct timeval timeout;
	int fd_max;
	int ret;
	int len;
	uint8_t *data;

	fd_max = port1->fd > port2->fd ? port1->fd : port2->fd;
	FD_ZERO(&fds_orig);
	FD_SET(port1->fd, &fds_orig);
	FD_SET(port2->fd, &fds_orig);
	
	while (1) {
		timeout.tv_sec = 0;
		timeout.tv_usec = 100;
		FD_COPY(&fds_orig, &fds);

		ret = select(fd_max+1, &fds, 0, 0, &timeout);
		if (ret < 0) {
			D("select failed");
			exit(2);
		} else if (ret == 0) {
			continue;
		} 

		if (FD_ISSET(port1->fd, &fds)) {
			len = iface_recv(port1, &data);
			if (len > 0)
				iface_send(port2, data, len);
		}
		if (FD_ISSET(port2->fd, &fds)) {
			len = iface_recv(port2, &data);
			if (len > 0)
				iface_send(port1, data, len);
		}
	}
#endif

#ifdef WITH_EV
	struct port_io port1_watcher = {.recv_port=port1, .send_port=port2};
	struct port_io port2_watcher = {.recv_port=port2, .send_port=port1};
	struct ev_loop *loop = ev_default_loop(0);

    ev_io_init((struct ev_io*)&port1_watcher, port_cb, port1->fd, EV_READ);
    ev_io_init((struct ev_io*)&port2_watcher, port_cb, port2->fd, EV_READ);

	ev_io_start(loop, (struct ev_io*)&port1_watcher);
	ev_io_start(loop, (struct ev_io*)&port2_watcher);

    ev_loop(loop, 0);
#endif

	return 0;
}
