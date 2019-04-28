#include "stdafx.h"

#include "ring_port.h"

struct ring_port * ring_port_create(char *dev, int max_recv_len) {
	struct ring_port *ret;	
	char err[PCAP_ERRBUF_SIZE];

	assert(dev);

	ret = malloc(sizeof(struct ring_port));
	if (!ret)
		die("malloc failed");
	memset(ret, 0, sizeof(struct ring_port));

	ret->name = strdup(dev);
	ret->dev = pcap_open_live(dev, max_recv_len, 1, 1, err);
	if (!ret->dev)
		die("pcap_open_live failed: %s", err);
	//if (pcap_setnonblock(ret->dev, 1, err) < 1) 
	//	die("pcap_setnonblock failed: %s", err);
	//
	if (pcap_setdirection(ret->dev, PCAP_D_IN) != 0) 
		die("pcap_setdirection failed");

	return ret;
}

void ring_port_destroy(struct ring_port *port) {
	assert(port);

	pcap_close(port->dev);
	free(port->name);
	free(port);
}

bool ring_port_send(struct ring_port *port, uint8_t *data, int len) {
	assert(port);

	if (pcap_inject(port->dev, data, len) < 0)
		return false;

	return true;
}

uint8_t *ring_port_recv(struct ring_port *port, int *len) {
	struct pcap_pkthdr *hdr;
	const u_char *data;

	assert(port);

	if (pcap_next_ex(port->dev, &hdr, &data) <= 0) {
		E("pcap_next_ex failed");
		return 0;
	}
	*len = hdr->caplen;

	return (uint8_t*)data;
}

bool ring_port_is_failed(struct ring_port *port) {
	assert(port);

	return port->is_failed;
}

bool ring_port_is_blocked(struct ring_port *port) {
	assert(port);

	return port->is_blocked;
}

void ring_port_unblock(struct ring_port *port) {
	assert(port);

	port->is_blocked = false;
}

void ring_port_block(struct ring_port *port) {
	assert(port);

	port->is_blocked = true;
}

