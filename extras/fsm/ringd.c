#include "stdafx.h"

#include "tick.h"
#include "ring_io.h"
#include "ring_port.h"
#include "ring.h"

struct port_io {
	struct ev_io io;
	struct ring *ring;
	struct ring_port *port;
};

struct ring_timer  {
	struct ev_timer timer;
	struct ring *ring;
};


struct ring_timer raps_timer_watcher, ring_timer_watcher;
tick_t last_raps_frame=0;

void usage() {
	fprintf(stdout, "usage: ./erps [-o rpl_port | -n rpl_port] port0 port0 node_id\n\n");
	fprintf(stdout, " -o rpl_port	node is RPL owner node\n");
	fprintf(stdout, " -n rpl_port	node is RPL neighbour node\n");
	fprintf(stdout, " port0,port1	port0 and port1 of ethernet ring\n");
	fprintf(stdout, " node_id		formatted as MAC address (eg. aa:aa:aa:aa:aa:aa)\n\n");
}

void exit_usage() {
	fprintf(stderr,"\n");
	usage();
	exit(1);
}

bool is_local_erps_frame(uint8_t *data, int len, uint8_t *node_id) {
	if (len < 55)
		return false;

	if (memcmp(data+14+4+6, node_id, 6) != 0)
		return false;

	return true;
}

bool is_erps_frame(uint8_t *data, int len) {
	// verify length
	if (len < 55)
		return false;
	
	// verify destination address
	if (memcmp(data, "\x01\x19\xA7\x00\x0", 5) != 0)
		return false;

	// XXX: verify VLAN tag

	return true;
}

#if 0
void external_command(struct ring *ring) {
	char cmd_buf[100];
	char *brkt, *cmd=0, *port_name=0;
	char sep[] = " \t\n\r";
	struct ring_port *port = 0;

	fgets(cmd_buf, 100, stdin);

	cmd = strtok_r(cmd_buf, sep, &brkt);
	if (!cmd) {
		E("invalid command");
		return;
	}
	port_name = strtok_r(0, sep, &brkt);
	
	// parse (optional) port name
	if (port_name) {
		if (strcmp(port_name, "port0") == 0) {
			port = ring->port0;
		} else if (strcmp(port_name, "port1") == 0) {
			port = ring->port1;
		} else {
			E("invalid port specified");
			return;
		}
	}

	// parse command
	if (strcmp(cmd, "clear") == 0) {
		D("CLEAR command");
		ring_process_request(ring, RING_REQ_CLEAR, 0, 0);

	} else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "status") == 0) {
		D("RING%d\n" \
		  "\tstate=%s\n" \
		  "\tport0=%s%s%s\n" \
		  "\tport1=%s%s%s\n", \
		  ring->ring_id, \
		  RING_STATE_STR[ring->state], \
		  ring->port0->name, ring->port0->is_blocked ? ",blocked" : "", ring->port0->is_failed ? ",failed" : "", \
		  ring->port1->name, ring->port1->is_blocked ? ",blocked" : "", ring->port1->is_failed ? ",failed" : "" );

	} else { 
		if (!port) {
			E("incomplete command: port not specified");
			return;
		}

		if (strcmp(cmd, "fs") == 0) {
			D("FS command on %s", port->name);
			ring_process_request(ring, RING_REQ_FS, port, 0);
		} else if (strcmp(cmd, "ms") == 0) {
			D("MS command on %s", port->name);
			ring_process_request(ring, RING_REQ_MS, port, 0);
		} else if (strcmp(cmd, "fail") == 0) {
			D("simulated fail of %s", port->name);
			ring_process_request(ring, RING_REQ_SF, port, 0);
		} else if (strcmp(cmd, "recover") == 0) {
			D("simulated recovery of %s", port->name);
			ring_process_request(ring, RING_REQ_CLEAR_SF, port, 0);
		} else {
			D("unknown command");
		}
	}
}
#endif 

static void port_cb(EV_P_ struct ev_io *_ev, int revents) {
	struct port_io *ev = (struct port_io *)_ev;
	struct ring_port *other_port;
	uint8_t *data;
	int len;

	// receive data from active port
	data = ring_port_recv(ev->port, &len);
	if (!data) {
		E("ring_port_recv failed");
		return;
	}

	if (is_erps_frame(data, len)) {
		if (is_local_erps_frame(data, len, ev->ring->node_id)) {
			// ignore frames originated with out node_id
			//D("local frame received. ignore");
			return;
		}
		
		// R-APS data goes into "ERP control"
		ring_process_raps(ev->ring, data, len, ev->port);

		// R-APS_FF
		// forward original data to other ring port, if not blocked
		other_port = ring_other_port(ev->ring, ev->port);
		if (!ring_port_is_blocked(ev->port)) {
			D("forwarding R-APS  %s->%s", ev->port->name, other_port->name);
			ring_port_send(other_port, data, len);
		}
	} else {
		// Service_FF
		// forward frame according to switching table
		//TODO:
	}
}

static void ring_timer_cb(EV_P_ struct ev_timer *_ev, int revents) {
	struct port_io *ev = (struct port_io *)_ev;
	struct ring *ring = ev->ring;

	if (!ring) {
		E("invalid ring");
		return;
	}
	ring_timer(ring);
}

static void send_raps_cb(EV_P_ struct ev_timer *_ev, int revents) {
	struct port_io *ev = (struct port_io *)_ev;
	struct ring *ring = ev->ring;

	if (!ring) {
		E("invalid ring");
		return;
	}

	if (!ring->is_sending_raps) {
		//D("not sending raps ring=%p", ring);
		return;
	}
	
	tick_t now;
	int delta;

	// send raps frame
	if (ring->raps_bursts_remain > 0) {
		//XXX: 1ms
		ring->raps_bursts_remain--;
		raps_timer_watcher.timer.repeat = 0.01;
	} else {
		raps_timer_watcher.timer.repeat = 5.0;
	}

//	now = tick_now();
//	delta = (ring->raps_bursts_remain > 0) ? 3 : 5000;

//	if (tick_diff_msec(now, last_raps_frame) > delta) {
	ring_send_raps(ring);
//		last_raps_frame = tick_now(); 
//	}
}


int main(int argc, char **argv) {
	struct timeval tv;
	fd_set fds, fds_read;
	int port0_fd, port1_fd, max_fd;
	int ret;

	int ch;
	bool is_rpl_owner=false, is_rpl_neighbour=false, work=true;
	char *port0_name, *port1_name, *rpl_port_name=0, *node_id_str=0;

	while ((ch = getopt(argc, argv, "o:n:")) != -1) {
		switch(ch) {
		case 'o':
			is_rpl_owner = true;
			rpl_port_name = optarg;
			break;
		case 'n':
			is_rpl_neighbour = true;
			rpl_port_name = optarg;
			break;
		case '?':
		case 'h':
		default:
			exit_usage();
		}
	}
	argc -= optind;
	argv += optind;

	if (argc != 3) {
		E("missing arguments");
		exit_usage();
	}

	port0_name = argv[0];
	port1_name = argv[1];
	node_id_str = argv[2];

	if (strlen(node_id_str) != 17) {
		E("node_id is in invalid format");
		exit_usage();
	}

	if (strcmp(port0_name, port1_name) == 0) {
		E("RPL ports must differ");
		exit_usage();
	}

	if (is_rpl_owner && is_rpl_neighbour) {
		E("node cannot be RPL owner and neighbor at a same time");
		exit_usage();
	}

	if (is_rpl_owner || is_rpl_neighbour) {
		if (strcmp(port0_name, rpl_port_name) != 0 &&
			strcmp(port1_name, rpl_port_name) != 0) {
			E("RPL port must be either [port0] or [port1]");
			exit_usage();
		}
	}

	// convert node id (mac) into array
	unsigned int mac[6], i;
	uint8_t node_id[6];
	sscanf(node_id_str, "%x:%x:%x:%x:%x:%x", \
		mac+0, mac+1, mac+2, mac+3, mac+4, mac+5);
	for (i=0; i!=6; i++) 
		node_id[i] = (uint8_t)(mac[i]);



	struct ring *ring;
	struct ring_port *port0, *port1, *rpl_port=0;
	struct ring_port *port;

	// open ring ports
	port0 = ring_port_create(port0_name, 1500);
	port1 = ring_port_create(port1_name, 1500);

	if (is_rpl_owner || is_rpl_neighbour) {
		if (strcmp(port0_name, rpl_port_name) == 0) {
			rpl_port = port0;
		} else if (strcmp(port1_name, rpl_port_name) == 0) {
			rpl_port = port1;
		} else {
			die("wtf");
		}
	}

	// create ring
	ring = ring_create(port0, port1, rpl_port, is_rpl_owner, is_rpl_neighbour, node_id);
	D("ring_create: ring=%p", ring);

	ring_io_init(ring);

	//XXX:
	port0_fd = pcap_get_selectable_fd(port0->dev);
	port1_fd = pcap_get_selectable_fd(port1->dev);
	if (port0_fd == -1 || port1_fd == -1)
		die("pcap_get_selectable_fd failed");

	struct port_io port0_watcher = { .ring=ring, .port=port0 };
	struct port_io port1_watcher = { .ring=ring, .port=port1 };
	struct ev_loop *loop = ev_default_loop (0);

	raps_timer_watcher.ring = ring;
	ring_timer_watcher.ring = ring;

	ev_io_init((struct ev_io*)&port0_watcher, port_cb, port0_fd, EV_READ);
	ev_io_init((struct ev_io*)&port1_watcher, port_cb, port1_fd, EV_READ);
	ev_timer_init ((struct ev_timer*)&ring_timer_watcher, ring_timer_cb, 1.0, 1.0);
	ev_timer_init ((struct ev_timer*)&raps_timer_watcher, send_raps_cb, 1.0, 1.0);

	ev_io_start (loop, (struct ev_io*)&port0_watcher);
	ev_io_start (loop, (struct ev_io*)&port1_watcher);
	ev_timer_start (loop, (struct ev_timer*)&ring_timer_watcher);
	ev_timer_start (loop, (struct ev_timer*)&raps_timer_watcher);
	ev_loop (loop, 0);

/*
	FD_ZERO(&fds);
	FD_SET(port0_fd, &fds);
	FD_SET(port1_fd, &fds);
	FD_SET(STDIN_FILENO, &fds);
	max_fd = (port0_fd > port1_fd) ? port0_fd : port1_fd;

	//XXX: optimize
	tv.tv_sec = 0;
	tv.tv_usec = 10000;

	int send_raps_fd = -1;
	int wtb_timer_fd = -1;
	int wtr_timer_fd = -1;

	struct ring_port *recv_from;
	uint8_t *data;
	int len;

	tick_t now, last_raps_frame=0;
	int delta;

	while (work) {
		recv_from = 0;

		// wait for next event...
		FD_COPY(&fds, &fds_read);
		ret = select(max_fd+1, &fds_read, 0, 0, &tv);
		if (ret < 0) {
			// error
			die("select failed (errno=%d)", errno);
		} else if (ret != 0) {
			// check events
			if (FD_ISSET(port0_fd, &fds_read)) {
				recv_from = port0;
			} else if (FD_ISSET(port1_fd, &fds_read)) {
				recv_from = port1;
			} else if (FD_ISSET(STDIN_FILENO, &fds_read)) {
				external_command(ring);
			} else if (FD_ISSET(send_raps_fd, &fds_read)) {
				ring_send_raps(ring);
			}
		}

		// check timers
		if (ring->is_sending_raps) {
			now = tick_now();
			delta = (ring->raps_bursts_remain > 0) ? 3 : 5000;

			if (tick_diff_msec(now, last_raps_frame) > delta) {
			}
		}

		if (!recv_from)
			continue;

	}
*/

	// cleanup
	ring_io_destroy();
	ring_destroy(ring);

	return 42;
}

