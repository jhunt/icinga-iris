#include "iris.h"

#define MAX_LINE 8192

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

static int sendall(int fd, char *buf, int *len)
{
	int left = *len;
	*len = 0;
	int n = 0;

	while (left > 0) {
		n = write(fd, buf + *len, left);
		if (n < 0) break;
		*len += n;
		left -= n;
	}
	return n < 0 ? n : 0;
}

static int sink(int fd)
{
	char buf[512];
	size_t n = 0;
	while ((n = read(fd, buf, n)) > 0)
		;
	return n;
}

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
	unsigned int npackets = 0, nsent = 0;
	struct sockaddr_in addr;
	int i, len;

	if (process_args(argc, argv) != 0) {
		alarm(0); exit(3);
	}

	int npackets = read_packets(stdin, &packets, OPTS.delim);

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(OPTS.port);

	if (!inet_aton(OPTS.host, &addr.sin_addr)) {
		struct hostent *he;
		he = gethostbyname((const char*)OPTS.host);
		if (!he) {
			fprintf(stderr, "%s: %s\n", OPTS.host, strerror(errno));
			exit(3);
		}
		memcpy(&addr.sin_addr, he->h_addr, he->h_length);
	}

	signal(SIGALRM, alarm_handler);
	alarm(OPTS.timeout);

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "failed to create a socket: %s\n", strerror(errno));
		exit(3);
	}

	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "connection failed: %s\n", strerror(errno));
		exit(2);
	}
	alarm(0);

	for (i = 0; i < npackets; i++) {
		time((time_t*)&packets[i].ts);
		pdu_pack(&packets[i]);

		alarm(OPTS.timeout);
		len = sizeof(struct pdu);
		if (sendall(sock, (char*)(&packets[i]), &len) < 0) {
			fprintf(stderr, "error sending data to %s:%d\n", OPTS.host, OPTS.port);
			close(sock);
			alarm(0); exit(3);
		}

		if (len < sizeof(packets[i])) {
			fprintf(stderr, "warning: only sent %d/%lu bytes to %s:%i\n",
				len, sizeof(packets[i]), OPTS.host, OPTS.port);
			close(sock);
			alarm(0); exit(3);
		}
		nsent++;
		alarm(0);
	}
	shutdown(sock, SHUT_WR);
	sink(sock);
	close(sock);
	alarm(0);

	if (!OPTS.quiet) {
		printf("sent %d results to host\n", nsent);
	}

	free(packets);
	return 0;
}
