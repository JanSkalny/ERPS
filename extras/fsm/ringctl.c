#include "stdafx.h"

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "tick.h"
#include "ring_io.h"
#include "ring.h"

void usage() {
	fprintf(stderr, "usage: ./ringctl command [port]\n\n");
	fprintf(stderr, "valid commands are:\n");
	fprintf(stderr, "  status        show current ring state\n");
	fprintf(stderr, "  clear         remote active MS/FS command or block RPL on owner\n");
	fprintf(stderr, "  ms port       issue Manual Switch command on specified port\n");
	fprintf(stderr, "  fs port       issue Forced Switch command on specified port\n");
	fprintf(stderr, "  fail port     simulate port failure\n");
	fprintf(stderr, "  recover port  recover from simulated port failure\n\n");
	fprintf(stderr, "valid ports are \"port0\" and \"port1\"\n\n");
}

void exit_usage(int code) {
	usage();
	exit(code);
}

void* ring_io_send_msg(uint8_t code, int arg) {
	int s, len;
	struct sockaddr_un addr;
	uint8_t buf[RING_IO_MAX_MSG_SIZE];
	struct ring_io_req req;

	// connect to ringd socket
	if ((s = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
		perror("socket");
		exit(1);
	}
	memset(&addr, 0, sizeof(addr));
	addr.sun_family = AF_UNIX;
	strncpy(addr.sun_path, RING_IO_SOCKET, sizeof(addr.sun_path)-1);
	len = strlen(addr.sun_path) + sizeof(addr.sun_family) + 1;
	if (connect(s, (struct sockaddr *)&addr, len) == -1) {
		perror("connect");
		exit(1);
	}

	req.cmd = code;
	req.param = arg;

	// write message
	D("send 1");
	send(s, &req, sizeof(req), 0);

	// read reply
	D("recv 1");
	len = recv(s, buf, RING_IO_MAX_MSG_SIZE, 0);
	if (len > 0) {
		// process reply
		//TODO:
	}

	close(s);

	return 0;
}

int main(int argc, char **argv) {
	char *cmd, *port_name=0;
	struct ring_io_status status;
	int port=0;

	if (argc != 2 && argc != 3) {
		exit_usage(1);
	}

	cmd = argv[1];
	if (argc == 3) 
		port_name = argv[2];

	// parse (optional) port name
	if (port_name) {
		if (strcmp(port_name, "port0") == 0) {
			port = RING_IO_PORT0;
		} else if (strcmp(port_name, "port1") == 0) {
			port = RING_IO_PORT1;
		} else {
			fprintf(stderr, "error: invalid port specified\n");
			exit_usage(1);
		}
	}

	// parse command
	if (strcmp(cmd, "clear") == 0) {
		ring_io_send_msg(RING_IO_CMD_CLEAR, 0);

	} else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "status") == 0) {
		ring_io_send_msg(RING_IO_CMD_STATUS, 0);

	} else { 
		if (!port) {
			fprintf(stderr, "error: port not specified\n");
			exit(1);
		}

		if (strcmp(cmd, "fs") == 0) {
			ring_io_send_msg(RING_IO_CMD_FS, port);
		} else if (strcmp(cmd, "ms") == 0) {
			ring_io_send_msg(RING_IO_CMD_MS, port);
		} else if (strcmp(cmd, "fail") == 0) {
			ring_io_send_msg(RING_IO_CMD_FAIL, port);
		} else if (strcmp(cmd, "recover") == 0) {
			ring_io_send_msg(RING_IO_CMD_RECOVER, port);
		} else {
			fprintf(stderr, "error: invalid command\n");
			exit(1);
		}
	}

	return 0;
}

