#include "stdafx.h"

#include "er_ring.h"
#include "er_netmap.h"

#include "er_port.h"

struct er_port * er_port_create(struct er_ring *ring, char *name) {
	struct er_port *port=0;
	int tmp;

	// create new "ring link" structure
	port = er_malloc(sizeof(struct er_port));
	if (!port) {
		E("malloc failed");
		goto cleanup;
	}
	strncpy(port->name, name, sizeof(port->name));
	port->name[sizeof(port->name)-1] = 0;
	port->idx = NM_BDG_NOPORT;

	if (er_netmap_attach(ring, port)) 
		goto cleanup;
	if (er_netmap_get_adapter(port)) 
		goto cleanup;

	// save port index for lookups in er_netmap_forward_lookup
	er_netmap_get_indices(ring->id, port->name, &tmp, &(port->idx));
	//XXX: port->idx = ((struct netmap_vp_adapter *)port->na)->bdg_port;

/*
	// catch all interface link events
	port->link_event = EVENTHANDLER_REGISTER(ifnet_link_event, _er_ifnet_link_event, port, EVENTHANDLER_PRI_FIRST);
*/

	E("attached %s as %d na=%p", port->name, port->idx, port->na);

	return port;

cleanup:
	if (port) {
		er_netmap_unget_adapter(port);
		er_netmap_detach(ring, port);
		er_free(port);
	}
	return 0;
}

void er_port_destroy(struct er_ring *ring, struct er_port *port) {
	assert(port);

	D("detach %s", port->name);

	if (port->link_event)
		EVENTHANDLER_DEREGISTER(ifnet_link_event, port->link_event);
	er_netmap_unget_adapter(port);
	er_netmap_detach(ring, port);
	er_free(port);
}

bool er_port_is_failed(struct er_port *port) {
	assert(port);

	return port->is_failed;
}

bool er_port_is_blocked(struct er_port *port) {
	assert(port);

	return port->is_blocked;
}

void er_port_unblock(struct er_port *port) {
	assert(port);

	port->is_blocked = false;
}

void er_port_block(struct er_port *port) {
	assert(port);

	port->is_blocked = true;
}

