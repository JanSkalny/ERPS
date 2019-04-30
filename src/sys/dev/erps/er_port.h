#pragma once

struct er_ring;

struct er_port {
	char name[IFNAMSIZ];
	struct netmap_adapter *na;
	struct ifnet *ifp;
	int idx;

	eventhandler_tag link_event;

	bool is_blocked;
	bool is_failed;
};

struct er_port * er_port_create(struct er_ring *ring, char *name);
void er_port_destroy(struct er_ring *ring, struct er_port *port);

/*
bool ring_port_send(struct er_port *port, uint8_t *data, int len);
uint8_t *ring_port_recv(struct er_port *port, int *len);
*/

bool er_port_is_blocked(struct er_port *port);
bool er_port_is_failed(struct er_port *port);
void er_port_unblock(struct er_port *port);
void er_port_block(struct er_port *port);
