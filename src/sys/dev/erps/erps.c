#include "stdafx.h"

#include "er_ring.h"
#include "er_port.h"

#include <net/erps.h>
#include <dev/erps/erps_kern.h>


// KLD module functions 
int er_module_init(void);
void er_module_fini(void);

// cdev functions & definition
static d_open_t      er_open;
static d_close_t     er_close;
static d_read_t      er_read;
static d_write_t     er_write;
static d_ioctl_t     er_ioctl;

static struct cdevsw cdevsw = {
	.d_version = D_VERSION,
	.d_open = er_open,
	.d_close = er_close,
	.d_read = er_read,
	.d_write = er_write,
	.d_ioctl = er_ioctl,
	.d_name = "erps",
};

static struct cdev *cdev;

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
	//struct er_port *port0, *port1, *rpl=0, *port;
	uint8_t rpl_owner_port, rpl_neighbour_port;
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

		rpl_owner_port = ERPS_RPL_NONE;
		rpl_neighbour_port = ERPS_RPL_NONE;

		if (req->er_rpl_owner)
			rpl_owner_port = req->er_rpl_port;
		if (req->er_rpl_neighbour)
			rpl_neighbour_port = req->er_rpl_port;

		//TODO: validate port names

		if (er_global) {
			E("ring already exists!");
			return EINVAL;
		}

		// create new ring
		ring = er_ring_create(req->er_id, req->er_port0, req->er_port1, rpl_owner_port, rpl_neighbour_port, req->er_node_id);
		if (!ring) {
			E("failed to create ring");
			return EINVAL;
		}

		//XXX:
		er_global = ring;

		// configure ring according to request, while validating inputs
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

		// start ERPS
		if (!er_activate_ring(ring)) {
			E("failed to activate ring!");
			//XXX: cleanup
			return EINVAL;
		}

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

/*
		switch (req->er_cmd) {
		case ERPS_FAIL:
			port = er_lookup_ring_port(ring, req->er_port0);
			if (!port)
				return EINVAL;
			if (!ring_block_ring_port(ring, port))
				return EINVAL;
			break;

		case ERPS_RECOVER:
			port = er_lookup_ring_port(ring, req->er_port0);
			if (!port)
				return EINVAL;
			if (!ring_unblock_ring_port(ring, port))
				return EINVAL;
			break;
		}
*/
		E("not implemented");
		return EINVAL;
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

	err = make_dev_p(MAKEDEV_CHECKNAME|MAKEDEV_WAITOK, &cdev, &cdevsw, 0, UID_ROOT, GID_WHEEL, 0600, "erps");
	if (err) {
		E("failed to create erps device");
		return err;
	}

	return 0;
}

void er_module_fini(void) {
	D("unloading module");
	er_destroy_all_rings();
	destroy_dev(cdev);
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
