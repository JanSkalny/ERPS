#pragma once

struct ring_port {
	bool is_blocked;
	bool is_failed;
	char *name;
	int recv_buf_len;

	pcap_t *dev;
};

struct ring_port * ring_port_create(char *dev, int max_recv_len);
void ring_port_destroy(struct ring_port *port);

bool ring_port_send(struct ring_port *port, uint8_t *data, int len);
uint8_t *ring_port_recv(struct ring_port *port, int *len);

bool ring_port_is_blocked(struct ring_port *port);
bool ring_port_is_failed(struct ring_port *port);
void ring_port_unblock(struct ring_port *port);
void ring_port_block(struct ring_port *port);

