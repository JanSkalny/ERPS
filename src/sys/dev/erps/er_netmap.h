#pragma once

struct er_ring;
struct er_port;

// netmap wrapper functions
int er_netmap_detach(struct er_ring *ring, struct er_port *port);;
int er_netmap_attach(struct er_ring *ring, struct er_port *port);;
int er_netmap_get_adapter(struct er_port *port);
int er_netmap_unget_adapter(struct er_port *port);
int er_netmap_get_indices(int ring_id, char *port_name, int *bridge_idx, int *port_idx);
int er_netmap_regops(struct er_ring *ring);
struct mbuf* er_netmap_get_buf(int len);
int er_netmap_send(struct er_port *port, struct mbuf *buf, int len);


