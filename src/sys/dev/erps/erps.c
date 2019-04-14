#include <sys/param.h>
#include <sys/module.h>
#include <sys/kernel.h>
#include <sys/systm.h>
#include <sys/malloc.h>
#include <sys/conf.h>
#include <sys/ioccom.h>
#include <sys/time.h>

// netmap prerequisites
#include <machine/bus.h>
#include <sys/socket.h>
#include <sys/selinfo.h>
#include <net/if.h>
#include <net/if_var.h>
#include <net/if_types.h>

#include <net/netmap.h>
#include <dev/netmap/netmap_kern.h>

#include <net/erps.h>
#include <dev/erps/erps_kern.h>

struct er_ring;
struct er_port;

static struct er_ring *er_global=0;

void *er_malloc(size_t);
void er_free(void *);

struct er_port *er_lookup_ring_port(struct er_ring *ring, char *name);
struct er_port *er_create_port(char *name, struct er_ring *ring);
void er_destroy_port(struct er_port *port, struct er_ring *ring);

bool er_block_ring_port(struct er_ring *ring, struct er_port *port);
bool er_unblock_ring_port(struct er_ring *ring, struct er_port *port);

struct er_ring *er_lookup_ring(uint16_t ring_id);
struct er_ring *er_create_ring(uint16_t ring_id, char *port0_name, char *port1_name);
void er_destroy_ring(struct er_ring *ring);
bool er_activate_ring(struct er_ring *ring);
bool er_deactivate_ring(struct er_ring *ring);
bool er_attach_ring_port(struct er_ring *ring, struct er_port *port); 

// netmap wrapper functions
int er_netmap_detach(struct er_ring *ring, struct er_port *port);;
int er_netmap_attach(struct er_ring *ring, struct er_port *port);;
int er_netmap_get_adapter(struct er_port *port);
int er_netmap_unget_adapter(struct er_port *port);
int er_netmap_get_indices(int ring_id, char *port_name, int *bridge_idx, int *port_idx);
int er_netmap_regops(struct er_ring *ring);
static u_int er_netmap_forward_lookup(struct nm_bdg_fwd *ft, uint8_t *ring_nr, struct netmap_vp_adapter *na);
struct mbuf* er_netmap_get_buf(int len);
int er_netmap_send(struct er_port *port, struct mbuf *buf, int len);


bool er_is_raps_frame(uint8_t *data, int len);
bool er_is_keepalive_frame(uint8_t *data, int len);
bool er_is_valid_raps_frame(struct er_ring *ring, uint8_t *data, int len);
void er_process_raps_frame(struct er_ring *ring, uint8_t *data, int len, struct er_port *recv_port);
struct er_port *er_other_ring_port(struct er_ring *ring, struct er_port *port);

#define ER_RING_LOCK_INIT(_ring) mtx_init(&(_ring)->mtx, "erps_ring", NULL, MTX_DEF)
#define ER_RING_LOCK_DESTROY(_ring)  mtx_destroy(&(_ring)->mtx)
#define ER_RING_LOCK(_ring)      mtx_lock(&(_ring)->mtx)
#define ER_RING_UNLOCK(_ring)    mtx_unlock(&(_ring)->mtx)
#define ER_RING_LOCK_ASSERT(_ring)   mtx_assert(&(_ring)->mtx, MA_OWNED)

// ioctl handlers
void er_destroy_all_rings(void);

// KLD module functions 
int er_module_init(void);
void er_module_fini(void);

// cdev functions & definition
static d_open_t      er_open;
static d_close_t     er_close;
static d_read_t      er_read;
static d_write_t     er_write;
static d_ioctl_t     er_ioctl;

static struct cdevsw er_cdevsw = {
	.d_version = D_VERSION,
	.d_open = er_open,
	.d_close = er_close,
	.d_read = er_read,
	.d_write = er_write,
	.d_ioctl = er_ioctl,
	.d_name = "erps",
};

static struct cdev *er_cdev;


// ERPS internal structures
struct er_port {
	char name[IFNAMSIZ];
	struct netmap_adapter *na;
	struct ifnet *ifp;
	int idx;

	eventhandler_tag link_event;

	bool is_blocked;
	bool is_failed;
};

struct er_ring {
    // ring interfaces
    struct er_port *port0;
    struct er_port *port1;

	struct mtx mtx;
	struct callout callout;
	void * vale_auth_token;
	char vale_name[IFNAMSIZ];

    // ring configuration
    bool is_rpl_owner;
    bool is_rpl_neighbour;
    bool is_revertive;
    struct er_port *rpl_port;
    char version;
    int id;
    int raps_vid;
    uint8_t node_id[6];

	uint32_t guard_time;
	uint32_t wtr_time;
	uint32_t wtb_time;

	bool active;
};

MALLOC_DECLARE(M_ERPS);
MALLOC_DEFINE(M_ERPS, "erps", "ethernet ring protection switching");

void *er_malloc(size_t size) {
    return malloc(size, M_ERPS, M_NOWAIT | M_ZERO);
}

void er_free(void *addr) {
    free(addr, M_ERPS);
}


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

static u_int er_netmap_forward_lookup(struct nm_bdg_fwd *ft, uint8_t *ring_nr, struct netmap_vp_adapter *na) {
	struct er_ring *ring;
	struct er_port *recv_port=0, *other_port;

	// lookup ring, this bridge belongs to
	//XXX: multi-ring support
	// ring = (struct er_ring*) na->arg1;
	ring = er_lookup_ring(ring_nr);

	// drop frame, if no active ring is found
	if (!ring || !ring->active)
		return NM_BDG_NOPORT;

	// is this "ring link" port?
	if (ring->port0->idx == na->bdg_port) {
		recv_port = ring->port0;
	} else if (ring->port1->idx == na->bdg_port) {
		recv_port = ring->port1;
	} 

	ND("recv on %s (idx=%d,flags=%x)", \
			recv_port ? recv_port->name : "non-ring-link-port", \
			na->bdg_port, ((struct netmap_adapter*)na)->na_flags);

	if (er_is_keepalive_frame(ft->ft_buf, ft->ft_len)) {
		// send keepalive reply and drop original request
		//XXX:er_send_keepalive_reply(recv_port);
		return NM_BDG_NOPORT;

	} else if (er_is_raps_frame(ft->ft_buf, ft->ft_len)) {
		// drop all R-APS messages from non-"ring link" ports
		if (!recv_port) {
			E("R-APS on non-ring link");
			return NM_BDG_NOPORT;
		}

		D("recvd R-APS on %d (%s)", recv_port->idx, recv_port->name);

		// validate R-APS before processing/forwarding
		if (er_is_valid_raps_frame(ring, ft->ft_buf, ft->ft_len)) {
			// ref. G.8032 9.5 c)
			er_process_raps_frame(ring, ft->ft_buf, ft->ft_len, recv_port);

			if ((other_port = er_other_ring_port(ring, recv_port))) {
				// don't forward R-APS messages to/from blocked ports
				// ref: G.8032 9.5 a)
				if (!recv_port->is_blocked && !other_port->is_blocked) {
					// forward to other port
					D("forward R-APS to %d (%s)", other_port->idx, other_port->name);
					return other_port->idx;
				}
			}
		}

		// drop
		return NM_BDG_NOPORT;
	}

	// if source port is blocked by ERPS, drop frame
	// destination port is blocked via VALE and NAF_SW_ONLY flag
	if (recv_port && recv_port->is_blocked) 
		return NM_BDG_NOPORT;

	// non-interesting frames are forwarded normally
	// reuse netmap's learning switch
	return netmap_bdg_learning(ft, ring_nr, na);
}

bool er_is_keepalive_frame(uint8_t *data, int len) {
	return false;
}
bool er_is_raps_frame(uint8_t *data, int len) {
	return false;
}
bool er_is_valid_raps_frame(struct er_ring *ring, uint8_t *data, int len) {
	return true;
}

struct er_port *er_other_ring_port(struct er_ring *ring, struct er_port *port) {
	// non-ring port
	if (ring->port0 != port && ring->port1 != 0) 
		return 0;
		
	return ring->port0 == port ? ring->port1 : ring->port0;
}

void er_process_raps_frame(struct er_ring *ring, uint8_t *data, int len, struct er_port *recv_port) {
}


int er_netmap_regops(struct er_ring *ring) {
	int err;
	static struct netmap_bdg_ops ops = {er_netmap_forward_lookup, 0, 0};

#ifdef FBSD12
	err = netmap_bdg_regops(ring->vale_name, &ops, 0, ring->vale_auth_token);
#else
	struct nmreq nmr;
	bzero(&nmr, sizeof(nmr));
	nmr.nr_version = NETMAP_API;
	nmr.nr_cmd = NETMAP_BDG_REGOPS;
	snprintf(nmr.nr_name, sizeof(nmr.nr_name)-1, "%s", ring->vale_name);
	err = netmap_bdg_ctl(&nmr, &ops);
#endif
	if (err) {
		E("failed to register custom ops on %s bridge (errno=%d)", ring->vale_name, err);
		return err;
	}

	return 0;
}

void er_destroy_ring(struct er_ring *ring) {
	// deactivate ring first
	if (ring->active)
		er_deactivate_ring(ring);

	// remove all ring ports 
	if (ring->port0)
		er_destroy_port(ring->port0, ring);
	if (ring->port1)
		er_destroy_port(ring->port1, ring);
#ifdef FBSD12
	// cleanup netmap stuff
	if (ring->vale_auth_token) {
		netmap_vale_destroy(ring->vale_name, ring->vale_auth_token);
	}
#endif

	// release allocated memory
	er_free(ring);
}

void er_destroy_all_rings(void) {
	//XXX: add multi-ring support
	if (er_global) {
		er_destroy_ring(er_global);
		er_global = 0;
	}
}

struct er_ring *er_lookup_ring(uint16_t ring_id) {
	//XXX: add multi-ring support
	return er_global;
}

struct er_port *er_lookup_ring_port(struct er_ring *ring, char *name) {
	if (strcmp(ring->port0->name, name) == 0)
		return ring->port0;
	if (strcmp(ring->port1->name, name) == 0)
		return ring->port1;

	return 0;
}

static inline void
_er_ifnet_link_event(void *arg, struct ifnet *ifp, int linkstate)
{
	struct er_ring *ring = (struct er_ring*)arg;

	D("%s went %s %d", ifp->if_xname, linkstate==LINK_STATE_UP ? "up" : "down", ticks);
	
	(void)ring;
}

struct er_port *er_create_port(char *name, struct er_ring *ring) {
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

	// catch all interface link events
	port->link_event = EVENTHANDLER_REGISTER(ifnet_link_event, _er_ifnet_link_event, port, EVENTHANDLER_PRI_FIRST);

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

void er_destroy_port(struct er_port *port, struct er_ring *ring) {
	D("detach %s", port->name);

	if (port->link_event)
		EVENTHANDLER_DEREGISTER(ifnet_link_event, port->link_event);
	er_netmap_unget_adapter(port);
	er_netmap_detach(ring, port);
	er_free(port);
}


struct er_ring *er_create_ring(uint16_t ring_id, char *port0_name, char *port1_name) {
	struct er_ring *ring=0;

	ring = er_malloc(sizeof(struct er_ring));
	if (!ring) {
		E("malloc failed");
		return 0;
	}

	ring->active = false;
	ring->id = ring_id;
#ifdef FBSD12
	ring->vale_auth_token = NULL;
#endif
	snprintf(ring->vale_name, sizeof(ring->vale_name)-1, "valeERPS%d:", ring_id);

#ifdef FBSD12
	int err;

	NMG_LOCK();
	ring->vale_auth_token = netmap_vale_create(ring->vale_name, &err);
	NMG_UNLOCK();
	if (err != 0 || ring->vale_auth_token==NULL) {
		E("failed to create vale bridge: err=%d", err);
		goto cleanup;
	}


#endif

	ring->port0 = er_create_port(port0_name, ring);
	if (!ring->port0) {
		E("failed to open port %s", port0_name);
		goto cleanup;
	}

	ring->port1 = er_create_port(port1_name, ring);
	if (!ring->port1) {
		E("failed to open port %s", port1_name);
		goto cleanup;
	}

	er_netmap_regops(ring);

	//XXX: add multi-ring support
	er_global = ring;

	return ring;

cleanup:
/*
	if (ring->vale_auth_token != NULL) {
		E("netmap_vale_destroy");
		netmap_vale_destroy(vale_name, ring->vale_auth_token);
	}

*/
	er_free(ring);

	return 0;
}

#if 0
struct mbuf* er_netmap_get_buf(int len) {
	struct mbuf *m=0, *n=0;

	m = m_gethdr(M_DONTWAIT, MT_DATA);
	if (!m) {
		E("failed to get mbuf!");
		return 0;
	}

	if (len > MHLEN) {
		n = m_pullup(m, len);
		if (!n) {
			D("failed to expand mbuf!");
			m_free(m);
			return 0;
		}
		m = n;
	}

	m->m_pkthdr.len = len;

	return m;
}


int er_netmap_send(struct er_port *port, struct mbuf *m, int len) {
	struct netmap_adapter *na;

	if (len != m->m_pkthdr.len) {
		E("message size mismatch! %d != %d", len, m->m_pkthdr.len);
		goto cleanup;
	}
	

	if (port->na->ifp != port->ifp) {
		E("ifp differs!");
		goto cleanup;
	}

	struct netmap_vp_adapter *va;
	struct netmap_bwrap_adapter *ba;
	na = port->na;
   	va = (struct netmap_vp_adapter*)na;
	ba = (struct netmap_bwrap_adapter*)na->na_vp;
/*
	D("ba=%p", ba);
	D("na->name=%s", na->name ? na->name : "null");
	D("va->name=%s", va ? va->up.name : "null");
	D("na->na_vp->name=%s", na->na_vp ? na->na_vp->up.name : "null");
	D("na->na_hostvp->name=%s", na->na_hostvp ? na->na_hostvp->up.name : "null");
*/
/*
	Dec  5 23:24:45 test64-0 kernel: 285.949362 [ 501] er_netmap_send            na->name=em2
	Dec  5 23:24:45 test64-0 kernel: 285.957099 [ 503] er_netmap_send            na->na_vp->name=valeERPS1:em2
	Dec  5 23:24:45 test64-0 kernel: 285.957518 [ 504] er_netmap_send            na->na_hostvp->name=valeERPS1:em2^
*/

	D("call netmap_transmit on %s (ipf_xname=%s)", na->name, port->ifp->if_xname);
	if (netmap_transmit(port->ifp, m)) {
		E("netmap_transmit failed");
		goto cleanup;
	}

	//XXX: non-standard hack!
	if(netmap_sync_host_rxring(na)) {
		E("netmap_sync_host_adapter failed");
		goto cleanup;
	}
/*
	err = netmap_rxsync_from_host(na, NULL, NULL);
	if (err <= 0) {
		E("netmap_rxsync_from_host returned %d", err);
		goto cleanup;
	}
*/

	return 0;

cleanup:
	if (m)
		m_free(m);

	return EINVAL;
}
#endif


static void er_send_raps(void *arg) {
	struct er_ring *ring = (struct er_ring *)arg;
	uint8_t data[55];

	ER_RING_LOCK(ring);

	D("send R-APS messages");

	// XXX: build R-APS message
	memset(data, 0, 55);
	memcpy(data, "\x01\x19\xa7\x00\x00", 5); data[5] = (uint8_t)ring->id;
	memcpy(data+6, ring->node_id, 6);
	memcpy(data+12, "\xf0\x0d", 2);

	//netmap_bdg_injection_test(ring->port0->na);
	//netmap_bdg_injection_test((struct netmap_adapter*)ring->port0->na->na_vp);
#if 0
	if (netmap_bdg_inject((struct netmap_adapter*)ring->port0->na->na_vp, data, 55)) {
		E("failed to send R-APS message on port0");
		goto cleanup;
	}
#endif

cleanup:
	callout_reset(&ring->callout, 5*hz, er_send_raps, ring);

	ER_RING_UNLOCK(ring);
}

bool er_activate_ring(struct er_ring *ring) {
	//XXX: start FSM thread
	D("activating ERPS%d", ring->id);

	ER_RING_LOCK_INIT(ring);
	callout_init_mtx(&ring->callout, &ring->mtx, 0);

	// start sending R-APS messages
	ER_RING_LOCK(ring);
	callout_reset(&ring->callout, 10*hz, er_send_raps, ring);
	ER_RING_UNLOCK(ring);

	ring->active = true;

	return true;
}

bool er_deactivate_ring(struct er_ring *ring) {
	//XXX: stop FSM thread
	D("deactivating ERPS%d", ring->id);

	ring->active = false;

	// destroy callouts
	callout_drain(&ring->callout);
	ER_RING_LOCK_DESTROY(ring);

	return true;
}

bool er_block_ring_port(struct er_ring *ring, struct er_port *port) {
	D("blocking port %s", port->name);

	if (!port->na) {
		E("missing netmap_adapter");
		return false;
	}
	if (!port->na->na_vp) {
		E("missing netmap_vp_adapter");
		return false;
	}

	if (port->na->na_vp->up.na_flags&NAF_SW_ONLY) 
		E("port is already blocked!");

	// block receiving (er_netmap_forward_lookup(...))
	port->is_blocked = true;

	// block sending (nm_bdg_flush(...))
   	port->na->na_vp->up.na_flags |= NAF_SW_ONLY;

	return true;
}

bool er_unblock_ring_port(struct er_ring *ring, struct er_port *port) {
	D("unblocking port %s", port->name);

	if (!port->na) {
		E("missing netmap_adapter");
		return false;
	}
	if (!port->na->na_vp) {
		E("missing netmap_vp_adapter");
		return false;
	}

	// unblock receiving (er_netmap_forward_lookup(...))
	port->is_blocked = false;

	// unblock sending (nm_bdg_flush(...))
   	port->na->na_vp->up.na_flags ^= NAF_SW_ONLY;

	return true;
}

static int er_open(struct cdev *dev, int oflags, int devtype, struct thread *td) {
	return 0;
}
static int er_close(struct cdev *dev, int fflag, int devtype, struct thread *td) {
	return 0;
}
static int er_read(struct cdev *dev, struct uio *uio, int ioflag) {
	return 0;
}
static int er_write(struct cdev *dev, struct uio *uio, int ioflag) {
	return 0;
}

// -----------------------------------------
// ioctl callback for /dev/erps

static int er_ioctl(struct cdev *dev, u_long cmd, caddr_t data, int fflag, struct thread *td) {
	struct er_port *port0, *port1, *rpl=0, *port;
	struct er_ring *ring;
	struct erreq *req = (struct erreq*)data;
	int err = 0;

	if (!data)
		return EINVAL;


	switch (cmd) {
	case ERIOCINIT:
		// make sure this ring wasn't already created
		ring = er_lookup_ring(req->er_id);
		if (ring) {
			return EINVAL;
		}

		// validate RPL configuration
		if (req->er_rpl_owner && req->er_rpl_neighbour) {
			// neighbour or owner - cant be both
			return EINVAL;
		} else if (!req->er_rpl_owner && !req->er_rpl_neighbour) {
			// neither neighbour noor owner and RPL specified
			if (req->er_rpl_port != ERPS_RPL_NONE) {
				return EINVAL;
			}
		} else if (req->er_rpl_port == ERPS_RPL_NONE) {
			// owner/neighbour without RPL specified
			return EINVAL;
		}

		// validate port names
		//TODO

		// create new ring
		ring = er_create_ring(req->er_id, req->er_port0, req->er_port1);
		if (!ring) {
			E("failed to create ring");
			return EINVAL;
		}
		switch(req->er_rpl_port) {
		case ERPS_RPL_PORT0: rpl = ring->port0; break;
		case ERPS_RPL_PORT1: rpl = ring->port1; break;
		}

		// configure ring according to request, while validating inputs
		ring->rpl_port = rpl;
		ring->is_rpl_owner = req->er_rpl_owner;
		ring->is_rpl_neighbour = req->er_rpl_neighbour;
		ring->version = req->er_version;
		if (!ring->version) 
			ring->version = 2;
		ring->raps_vid = req->er_raps_vid;
		if (ring->raps_vid <= 0 || ring->raps_vid >= 4094) 
			ring->raps_vid = 2000;
		ring->guard_time = req->er_guard_time;
		if (ring->guard_time < 10 || ring->guard_time > 2000) 
			ring->guard_time = 500;
		ring->wtr_time = req->er_wtr_time;
		if (ring->wtr_time < 1*60000 || ring->wtr_time > 12*60000)
			ring->wtr_time = 5*60000;
		ring->wtb_time = ring->guard_time + 5000;
		ring->is_revertive = req->er_revertive;
		memcpy(ring->node_id, req->er_node_id, 6);
		
		// start ERPS
		if (!er_activate_ring(ring)) {
			//XXX: cleanup
			return EINVAL;
		}
		break;

	case ERIOCADDIF:
		uprintf("er_addif\n");
		break;

	case ERIOCDELIF:
		uprintf("er_delif\n");
		break;

	case ERIOCSTATUS:
		// make sure this ring is already created
		ring = er_lookup_ring(req->er_id);
		if (!ring) {
			return EINVAL;
		}
		strncpy(req->er_port0, ring->port0->name, sizeof(req->er_port0));
		strncpy(req->er_port1, ring->port1->name, sizeof(req->er_port1));
		req->er_port0_failed = ring->port0->is_failed;
		req->er_port1_failed = ring->port1->is_failed;
		req->er_port0_blocked = ring->port0->is_blocked;
		req->er_port1_blocked = ring->port1->is_blocked;
		req->er_rpl_owner = ring->is_rpl_owner;
		req->er_rpl_neighbour = ring->is_rpl_neighbour;
		break;

	case ERIOCCOMMAND:
		// make sure this ring is already active & created
		ring = er_lookup_ring(req->er_id);
		if (!ring) 
			return EINVAL;

		switch (req->er_cmd) {
		case ERPS_FAIL:
			port = er_lookup_ring_port(ring, req->er_port0);
			if (!port)
				return EINVAL;
			if (!er_block_ring_port(ring, port))
				return EINVAL;
			break;

		case ERPS_RECOVER:
			port = er_lookup_ring_port(ring, req->er_port0);
			if (!port)
				return EINVAL;
			if (!er_unblock_ring_port(ring, port))
				return EINVAL;
			break;
		}
		break;

	default:
		err = ENXIO;
	}

	return err;
}

// -----------------------------------------
// freebsd kernel module stuff

int er_module_init(void) {
	int err;

	err = make_dev_p(MAKEDEV_CHECKNAME|MAKEDEV_WAITOK, &er_cdev, &er_cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "erps");
	if (err) {
		E("failed to create erps device");
		return err;
	}

	return 0;
}

void er_module_fini(void) {
	er_destroy_all_rings();
	destroy_dev(er_cdev);
}

static int er_module_handler(struct module *module, int event, void *arg) {
	int err = 0;

	switch (event) {
	case MOD_LOAD:
		err = er_module_init();
		break;
	case MOD_UNLOAD:
		er_module_fini();
		break;
	default:
		err = EOPNOTSUPP; 
		break;
	}
       
	return err;
}

DEV_MODULE(erps, er_module_handler, NULL);
MODULE_DEPEND(erps, netmap, 1, 1, 1);
