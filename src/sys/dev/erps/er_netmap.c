#include "stdafx.h"

#include "er_ring.h"
#include "er_port.h"

#include "er_netmap.h"

static u_int er_netmap_on_recv(struct nm_bdg_fwd *ft, uint8_t *ring_nr, struct netmap_vp_adapter *na);

int er_netmap_attach(struct er_ring *ring, struct er_port *port) {
	int err = 0;
#ifdef FBSD12
	struct nmreq_header hdr;
	struct nmreq_vale_attach req;

	bzero(&hdr, sizeof(hdr));
	bzero(&req, sizeof(req));

	// attach device to VALE bridge
        hdr.nr_version = NETMAP_API; 
	snprintf(hdr.nr_name, sizeof(hdr.nr_name)-1, "%s%s", ring->vale_name, port->name);
        hdr.nr_options = (uintptr_t)NULL;
	hdr.nr_body = (uintptr_t)&req;
	hdr.nr_reqtype = NETMAP_REQ_VALE_ATTACH;

	req.reg.nr_mode = NR_REG_ALL_NIC;

	err = nm_bdg_ctl_attach(&hdr, ring->vale_auth_token);
	if (err) {
		E("failed to attach port %s. err=%d", hdr.nr_name, err);
	} else {
		port->idx = req.port_index;
		E("attached %s as index %d", hdr.nr_name, req.port_index);
	}
#else
	struct nmreq nmr;

	// attach device to VALE bridge
	bzero(&nmr, sizeof(nmr));
	nmr.nr_cmd = NETMAP_BDG_ATTACH;
	nmr.nr_flags = NR_REG_ALL_NIC;
	nmr.nr_version = NETMAP_API;
	snprintf(nmr.nr_name, sizeof(nmr.nr_name)-1, "%s%s", ring->vale_name, port->name);
	err = netmap_bdg_ctl(&nmr, 0);
	if (err)
		E("failed to attach port %s", nmr.nr_name);
#endif

	return err;
}

int er_netmap_detach(struct er_ring *ring, struct er_port *port) {
#ifdef FBSD12
	struct nmreq_header hdr;
	struct nmreq_vale_detach req;
	int err = 0;

	bzero(&hdr, sizeof(hdr));
	bzero(&req, sizeof(req));

	// detach device from VALE bridge
        hdr.nr_version = NETMAP_API; 
	snprintf(hdr.nr_name, sizeof(hdr.nr_name)-1, "%s%s", ring->vale_name, port->name);
        hdr.nr_options = (uintptr_t)NULL;
	hdr.nr_body = (uintptr_t)&req;
	hdr.nr_reqtype = NETMAP_REQ_VALE_DETACH;

	err = nm_bdg_ctl_detach(&hdr, ring->vale_auth_token);
	if (err) 
		E("failed to detach port %s", hdr.nr_name);
#else
	struct nmreq nmr;
	int err;

	// detach device from VALE bridge
	bzero(&nmr, sizeof(nmr));
	nmr.nr_cmd = NETMAP_BDG_DETACH;
	nmr.nr_version = NETMAP_API;
	snprintf(nmr.nr_name, sizeof(nmr.nr_name)-1, "%s%s", ring->vale_name, port->name);
	err = netmap_bdg_ctl(&nmr, 0);
	if (err) 
		E("failed to detach port %s", nmr.nr_name);
#endif

	return err;
}

int er_netmap_get_indices(int ring_id, char *port_name, int *bridge_idx, int *port_idx) {
	struct nmreq nmr;
	int err;

	bzero(&nmr, sizeof(nmr));
	nmr.nr_version = NETMAP_API;
	nmr.nr_cmd = NETMAP_BDG_LIST;
	snprintf(nmr.nr_name, sizeof(nmr.nr_name)-1, "valeERPS%d:%s", ring_id, port_name);
	err = netmap_bdg_ctl(&nmr, 0);
	if (err) {
		D("%s failed to get indices", port_name);
		return err;
	}

	*port_idx = nmr.nr_arg2;
	*bridge_idx = nmr.nr_arg1;

	return 0;
}

int er_netmap_get_adapter(struct er_port *port) {
	struct nmreq nmr;
	int err;

	// get port from VALE bridge
	bzero(&nmr, sizeof(nmr));
	nmr.nr_version = NETMAP_API;
	snprintf(nmr.nr_name, sizeof(nmr.nr_name)-1, "%s", port->name);

	NMG_LOCK();
#ifdef FBSD12
	err = netmap_get_na(&nmr, &port->na, &port->ifp, 0);
#else
	err = netmap_get_na(&nmr, &port->na, 0);
#endif
	NMG_UNLOCK();

	if (err) {
		E("netmap_get_na: failed");
		return EINVAL;
	}
	if (!port->na) {
		E("netmap_get_na: na not found");
		goto cleanup;
	}
#ifdef FBSD12
	if (!port->ifp) {
		E("netmap_get_na faield to get ifp");
		goto cleanup;
	}
#endif
	return 0;

cleanup:
	er_netmap_unget_adapter(port);
	return EINVAL;
}

int er_netmap_unget_adapter(struct er_port *port) {
	NMG_LOCK();
#ifdef FBSD12
	netmap_unget_na(port->na, port->ifp);
#else
	netmap_adapter_put(port->na);
#endif
	NMG_UNLOCK();

	return 0;
}

int er_netmap_regops(struct er_ring *ring) {
	int err;
	static struct netmap_bdg_ops ops = {er_netmap_on_recv, 0, 0};

#ifdef FBSD12
	err = netmap_bdg_regops(ring->vale_name, &ops, 0, ring->vale_auth_token);
#else
	struct nmreq nmr;
	bzero(&nmr, sizeof(nmr));
	nmr.nr_version = NETMAP_API;
	nmr.nr_cmd = NETMAP_BDG_REGOPS;
	snprintf(nmr.nr_name, sizeof(nmr.nr_name)-1, "%s", ring->vale_name);
	err = netmap_bdg_ctl(&nmr, &ops);
	E("register custom ops");
#endif
	if (err) {
		E("failed to register custom ops on %s bridge (errno=%d)", ring->vale_name, err);
		return err;
	}

	return 0;
}


static u_int er_netmap_on_recv(struct nm_bdg_fwd *ft, uint8_t *ring_nr, struct netmap_vp_adapter *na) {
	struct er_ring *ring;
	struct er_port *recv_port=0;

	// lookup ring, this bridge belongs to
	//XXX: multi-ring support
	// ring = (struct er_ring*) na->arg1;
	ring = er_lookup_ring((uint16_t)ring_nr);

	// drop frame, if no active ring is found
	if (!ring || !ring->active)
		return NM_BDG_NOPORT;

	// is this port part of the ring?
	if (ring->port0->idx == na->bdg_port) {
		recv_port = ring->port0;
	} else if (ring->port1->idx == na->bdg_port) {
		recv_port = ring->port1;
	} 

	D("recv on %s (idx=%d,flags=%x, len=%d)", \
			recv_port ? recv_port->name : "non-ring-link-port", \
			na->bdg_port, ((struct netmap_adapter*)na)->na_flags, \
			ft->ft_len);

	// process received data
	// consume signalling, drop data packets on blocked ports and check for bogus traffic
	if (!er_forward_lookup(ring, recv_port, ft->ft_buf, ft->ft_len))
		return NM_BDG_NOPORT;

	D("forward frame...");

	// non-interesting frames are forwarded normally
	// reuse netmap's learning switch
	return netmap_bdg_learning(ft, ring_nr, na);
}

