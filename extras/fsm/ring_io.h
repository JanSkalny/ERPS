#pragma once

#define RING_IO_PORT0	0xF0
#define RING_IO_PORT1	0xF1

#define RING_IO_CMD_STATUS	0x01
#define RING_IO_CMD_CLEAR	0x02
#define RING_IO_CMD_MS		0x03
#define RING_IO_CMD_FS		0x04
#define RING_IO_CMD_FAIL	0x05
#define RING_IO_CMD_RECOVER	0x06

#define RING_IO_SOCKET "/tmp/ring.sock"
#define RING_IO_MAX_MSG_SIZE 100

struct ring_io_status {
	int ring_id;
	int status;

	bool port0_blocked;
	bool port0_failed;
	bool port1_blocked;
	bool port1_failed;
};

struct ring_io {
	pthread_t thr;
	struct ring *ring;

	int abort_send;
	int abort_recv;
	int sock;
	fd_set fds;
	int max_fd;
};

bool ring_io_init(struct ring *ring);
void ring_io_destroy();

