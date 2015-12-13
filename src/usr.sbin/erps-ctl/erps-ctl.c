#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdbool.h>

#include <sys/ioctl.h>

#include <net/erps.h>

int g_dev;

void usage() {
	fprintf(stderr, "usage: ./erps-ctlcommand [arguments ...]\n\n");
	fprintf(stderr, "valid commands are:\n");
	fprintf(stderr, "  init [-o rpl_port | -n rpl_port] [-i node_id] port0 port1\n");
	fprintf(stderr, "                initialize ERPS instance\n");
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

void show_ring_status() {
	struct erreq req;
	int ret;

	bzero(&req, sizeof(req));
	req.er_id = 1;

	ret = ioctl(g_dev, ERIOCSTATUS, &req);
	if (ret) {
		perror("ioctl failed");
		exit(1);
	}
	printf("%s %s %s\n", req.er_port0, \
			req.er_port0_blocked ? "blocked" : "forwarding", \
			req.er_port0_failed ? "failed" : "");
	printf("%s %s %s\n", req.er_port1, \
			req.er_port1_blocked ? "blocked" : "forwarding", \
			req.er_port1_failed ? "failed" : "");
}

void ring_command(int command, char *port) {
	struct erreq req;
	int ret;

	bzero(&req, sizeof(req));
	req.er_id = 1;
	req.er_cmd = command;
	if (port)
		strncpy(req.er_port0, port, sizeof(req.er_port0));
	ret = ioctl(g_dev, ERIOCCOMMAND, &req);
	if (ret) {
		perror("ioctl failed");
		exit(1);
	}

	printf("command issued\n");
}

bool init_ring(char *port0, char *port1, int argc, char *argv[]) {
	int ch, ret;
	bool is_rpl_owner=false, is_rpl_neighbour=false;
	char *rpl_port=0;
	unsigned int mac[6], i;
	struct erreq req;
	uint8_t node_id[6];
	bzero(node_id, 6);

	while ((ch = getopt(argc, argv, "o:n:i:h")) != -1) {
		switch (ch) {
		case 'o':
			is_rpl_owner = true;
			rpl_port = optarg;
			break;
		case 'n':
			is_rpl_neighbour = true;
			rpl_port = optarg;
			break;
		case 'i':
			if (!optarg || strlen(optarg) != 17) {
				fprintf(stderr, "error: invalid node_id format (aa:aa:aa:aa:aa:aa)\n");
				exit(1);
			}
			sscanf(optarg, "%x:%x:%x:%x:%x:%x", \
					mac+0, mac+1, mac+2, mac+3, mac+4, mac+5);
			for (i=0; i!=6; i++) {
				node_id[i] = (uint8_t)(mac[i]);
			}
			break;
		case 'h':
			usage();
			return false;
		}
	}
	argc -= optind;
	argv += optind;

	bzero(&req, sizeof(req));

	req.er_id = 1;
	req.er_version = 2;
	memcpy(req.er_node_id, "\x0\x0\x0\x0\x0\x0", 6);
	strncpy(req.er_port0, port0, IFNAMSIZ);
	strncpy(req.er_port1, port1, IFNAMSIZ);

	if (node_id) {
		//XXX: ...
	}

	if (rpl_port) {
		if (strcmp(rpl_port, port0) == 0) {
			req.er_rpl_port = ERPS_RPL_PORT0;
		} else if (strcmp(rpl_port, port1) == 0) {
			req.er_rpl_port = ERPS_RPL_PORT1;
		} else {
			fprintf(stderr, "error: RPL must be either port0 or port1\n");
			return false;
		}
	} else {
		req.er_rpl_port = ERPS_RPL_NONE;
	}

	if (is_rpl_owner)
		req.er_rpl_owner = 1;
	if (is_rpl_neighbour)
		req.er_rpl_neighbour = 1;

	ret = ioctl(g_dev, ERIOCINIT, &req);
	if (ret !=0 ) {
		perror("ioctl failed");
		return false;
	}

	return true;
}

int main(int argc, char **argv) {
	char *cmd;
	int ch;

	if (argc < 2) 
		exit_usage(1);

	// open erps device
	g_dev = open("/dev/erps", O_RDWR);
	if (g_dev == -1) {
		fprintf(stderr, "error: unable to open /dev/erps device\n");
		exit(1);
	}

	while ((ch = getopt(argc, argv, "h")) != -1) {
		switch (ch) {
		case 'h':
		case '?':
			exit_usage(0);
		}
	}
	argc -= optind;
	argv += optind;

	cmd = argv[0];

	// parse command
	if (strcmp(cmd, "clear") == 0) {
		ring_command(ERPS_CLEAR, 0);

	} else if (strcmp(cmd, "info") == 0 || strcmp(cmd, "status") == 0) {
		show_ring_status();

	} else if (strcmp(cmd, "init") == 0) {
		if (argc < 3) {
			fprintf(stderr, "error: ports not specified\n");
			goto err;
		}
		init_ring(argv[1], argv[2], argc-2, argv+2);

	} else { 
		if (argc < 2) {
			fprintf(stderr, "error: port not specified\n");
			goto err;
		}

		if (strcmp(cmd, "fs") == 0) {
			ring_command(ERPS_FS, argv[1]);
		} else if (strcmp(cmd, "ms") == 0) {
			ring_command(ERPS_MS, argv[1]);
		} else if (strcmp(cmd, "fail") == 0) {
			ring_command(ERPS_FAIL, argv[1]);
		} else if (strcmp(cmd, "recover") == 0) {
			ring_command(ERPS_RECOVER, argv[1]);
		} else {
			fprintf(stderr, "error: invalid command %s\n", cmd);
			goto err;
		}
	}

	// clean exit
	close(g_dev);
	return 0;

err:
	close(g_dev);
	return 1;
}

