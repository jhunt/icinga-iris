#include "iris.h"

struct {
	char *host;
	int   port;
	int   timeout;
	int   quiet;
	char  delim[2];
} OPTS = {
	.host    = NULL,
	.port    = IRIS_DEFAULT_PORT,
	.timeout = IRIS_DEFAULT_TIMEOUT,
	.quiet   = 0,
	.delim   = "\t"
};

void alarm_handler(int sig)
{
	printf("Timed out after %d seconds\n", OPTS.timeout);
	alarm(0); exit(3);
}

int process_args(int argc, char **argv)
{
	int c;

	while ((c = getopt(argc, argv, "qvH:p:t:h?")) != -1) {
		switch (c) {
		case 'q':
			OPTS.quiet = 1;
			break;

		case 'v':
			OPTS.quiet = 0;
			break;

		case 'H':
			free(OPTS.host);
			OPTS.host = strdup(optarg);
			break;

		case 'p':
			OPTS.port = atoi(optarg);
			break;

		case 't':
			OPTS.timeout = atoi(optarg);
			break;

		case 'h':
		case '?':
			printf("USAGE: send_iris -H <host> [-p <port>] [-t <timeout>]\n");
			printf("\n");
			printf("  -h\n");
			printf("      Show this informative help screen.\n");
			printf("\n");
			printf("  -H <hostname>\n");
			printf("      IP or hostname of who we should submit results to.\n");
			printf("      (this option is required)\n");
			printf("\n");
			printf("  -p <port>\n");
			printf("      TCP port to connect to.\n");
			printf("      Defaults to %d\n", IRIS_DEFAULT_PORT);
			printf("\n");
			printf("  -t <timeout>\n");
			printf("      Connection timeout, in seconds.\n");
			printf("      Defaults to %d\n", IRIS_DEFAULT_TIMEOUT);
			printf("\n");
			exit(0);
			break;
		}
	}

	if (!OPTS.host) {
		fprintf(stderr, "Missing required -H option\n");
		return 1;
	}

	return 0;
}

int main(int argc, char **argv)
{
	struct pdu *packets = NULL;
	int npackets = 0, nsent = 0;
	int sock, i;
	time_t now;

	if (process_args(argc, argv) != 0)
		exit(3);

	npackets = read_packets(stdin, &packets, OPTS.delim);

	signal(SIGALRM, alarm_handler);
	alarm(OPTS.timeout);
	if ((sock = net_connect(OPTS.host, OPTS.port)) < 0) {
		fprintf(stderr, "error connecting to %s:%d: %s\n",
				OPTS.host, OPTS.port, strerror(errno));
		alarm(0); exit(3);
	}

	for (i = 0; i < npackets; i++) {
		time(&now); packets[i].ts = (uint32_t)now;
		pdu_pack(&packets[i]);

		if (pdu_write(sock, (char*)&packets[i]) < 0) {
			fprintf(stderr, "error sending data to %s:%d\n", OPTS.host, OPTS.port);
			close(sock);
			alarm(0); exit(3);
		}
		nsent++;
	}
	shutdown(sock, SHUT_WR);
	fd_sink(sock);
	close(sock);
	alarm(0);

	if (!OPTS.quiet) {
		printf("sent %d results to host\n", nsent);
	}

	free(packets);
	return 0;
}
