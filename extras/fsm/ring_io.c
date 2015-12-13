#include "stdafx.h"

#include "tick.h"
#include "ring.h"
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
	uint8_t buf[100];
	int len, param;

	len = recv(sock, buf, sizeof(uint8_t)+sizeof(int), 0);
	if (len > 0) {
		param = *(int*)(buf+1);
		switch(buf[0]) {
		case RING_IO_CMD_STATUS:
			D("status");
			break;
		case RING_IO_CMD_CLEAR:
			D("clear");
			break;
		case RING_IO_CMD_MS:
			D("ms %d", param);
			break;
		case RING_IO_CMD_FS:
			D("fs %d", param);
			break;
		case RING_IO_CMD_FAIL:
			D("fail %d", param);
			break;
		case RING_IO_CMD_RECOVER:
			D("recovert %d", param);
			break;
		}
	}

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




