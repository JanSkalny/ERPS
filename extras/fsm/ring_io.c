#include "stdafx.h"

#include "tick.h"
#include "ring.h"
#include "ring_port.h"
#include "ring_io.h"

struct ring_io g_ring_io;

void rio_client(struct ring_io *io, int sock);
int rio_accept(struct ring_io *io);
void* rio_worker(void *param);

bool ring_io_init(struct ring *ring) {
	struct ring_io *io;
	struct sockaddr_un addr;
	int fd[2], flags, len;

	io = &g_ring_io;

	io->ring = ring;

	// create pipe used for cancellation signalling
	pipe(fd);
	io->abort_send = fd[0];
	io->abort_recv = fd[1];

	// create unix communication socket
	io->sock = socket(AF_UNIX, SOCK_STREAM, 0);
	if (io->sock == -1) {
		die("socket failed");
		return false;
	}
	addr.sun_family = AF_UNIX;  
	strcpy(addr.sun_path, RING_IO_SOCKET);
	unlink(addr.sun_path);
	len = strlen(addr.sun_path) + sizeof(addr.sun_family) + 1;
	bind(io->sock, (struct sockaddr *)&addr, len);
	flags = fcntl(io->sock, F_GETFL, 0);
	fcntl(io->sock, F_SETFL, flags | O_NONBLOCK);
	listen(io->sock, 10);

	// prepare fds and max_fd for later selects
	io->max_fd = (fd[0] > fd[1]) ? fd[0] : fd[1];
	io->max_fd = (io->max_fd > io->sock) ? io->max_fd : io->sock;
	FD_ZERO(&io->fds);
	FD_SET(io->sock, &io->fds);
	FD_SET(io->abort_recv, &io->fds);

	// start worker thread
	if (pthread_create(&io->thr, 0, rio_worker, (void*)io)) {
		die("pthread_create failed");
		return false;
	}

	return io;
}


void ring_io_destroy() {
	struct ring_io *io;
	uint8_t one = 1;

	io = &g_ring_io;
	
	// abort worker thread and wait for exit
	write(io->abort_send, &one, 1);
	pthread_join(io->thr, 0);
}

void rio_client(struct ring_io *io, int sock) {
	struct ring *ring = io->ring;
	struct ring_port *port = 0;
	struct ring_io_req req;
	uint8_t buf[100];
	int len;

	len = recv(sock, buf, sizeof(uint8_t)+sizeof(int), 0);
	if (len != 5) {
		E("got invalid request. len=%d", len);
		goto cleanup;
	}

	memcpy(&req, buf, 5);

	if (req.param == RING_IO_PORT0)
		port = ring->port0;
	if (req.param == RING_IO_PORT1)
		port = ring->port1;

	if (len > 0) {
		switch(req.cmd) {
		case RING_IO_CMD_STATUS:
			D("[exec] status");
			D("RING%d\n" \
			  "\tstate=%s\n" \
			  "\tport0=%s%s%s%s\n" \
			  "\tport1=%s%s%s%s\n" \
			  "\tguard=%d wtr=%d wtb=%d\n" \
			  "\tis_revertive=%d\n" \
			  "\tis_rpl_owner=%d\n" \
			  "\tis_rpl_neighbour=%d\n", \
			  ring->ring_id, \
			  RING_STATE_STR[ring->state], \
			  ring->port0->name, ring->port0->is_blocked ? ",blocked" : "", ring->port0->is_failed ? ",failed" : "", \
			  ring->port0 == ring->rpl_port ? ",rpl" : "", \
			  ring->port1->name, ring->port1->is_blocked ? ",blocked" : "", ring->port1->is_failed ? ",failed" : "", \
			  ring->port1 == ring->rpl_port ? ",rpl" : "", \
			  ring->guard_timer_active, ring->wtr_timer_active, ring->wtb_timer_active, \
			  ring->is_revertive, \
			  ring->is_rpl_owner, \
			  ring->is_rpl_neighbour);

			break;
		case RING_IO_CMD_CLEAR:
			D("[cmd] clear");
			ring_process_request(ring, RING_REQ_CLEAR, 0, 0);
			break;
		case RING_IO_CMD_MS:
			D("[cmd] ms %s", port->name);
			ring_process_request(ring, RING_REQ_MS, port, 0);
			break;
		case RING_IO_CMD_FS:
			D("[cmd] fs %s", port->name);
			ring_process_request(ring, RING_REQ_FS, port, 0);
			break;
		case RING_IO_CMD_FAIL:
			D("[cmd] fail %s", port->name);
			port->is_failed = true;
			ring_process_request(ring, RING_REQ_SF, port, 0);
			break;
		case RING_IO_CMD_RECOVER:
			D("[cmd] recover %s", port->name);
			port->is_failed = false;
			ring_process_request(ring, RING_REQ_CLEAR_SF, port, 0);
			break;
		}
	}

cleanup:

	close(sock);
	FD_CLR(sock, &io->fds);
	//TODO: find new io->max_fd
}

int rio_accept(struct ring_io *io) {
	int sock;

	sock = accept(io->sock, 0, 0);
	if (sock < 0)
		return -1;

	FD_SET(sock, &io->fds);
	if (sock > io->max_fd)
		io->max_fd = sock;

	return sock;
}

void* rio_worker(void *param) {
	struct timeval timeout;
	struct ring_io *io;
	int i, ret;
	fd_set fds;

	D("IO: running");

	io = (struct ring_io*)param;

	timeout.tv_sec = 10;
	timeout.tv_usec = 0;

	while (1) {
		// prepare fds for select
		FD_COPY(&io->fds, &fds);
		ret = select(io->max_fd+1, &fds, 0, 0, &timeout);
		if (ret < 0) {
			die("select failed");
			pthread_exit(0);
			return 0;
		}
		if (ret == 0) {
			continue;
		}

		// new client is connected
		if (FD_ISSET(io->sock, &fds)) {
			rio_accept(io);
			FD_CLR(io->sock, &fds);
		}

		if (FD_ISSET(io->abort_recv, &fds)) {
			// abort worker thread
			D("IO: abort");
			pthread_exit(0);
			return 0;
			FD_CLR(io->abort_recv, &fds);
		}

		for (i=0; i!=io->max_fd+1; i++) {
			// is something else was activated
			if (FD_ISSET(i, &fds)) {
				// recv from client
				rio_client(io, i);
			}
		}
	}

	pthread_exit(0);
	return 0;
}




