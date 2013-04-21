#include "iris.h"
#include <ctype.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#define MAX_LINE 8192

struct {
	char *host;
	int   port;
	int   timeout;
	char  delim[2];
} OPTS = {
	.host    = NULL,
	.port    = IRIS_DEFAULT_PORT,
	.timeout = IRIS_DEFAULT_TIMEOUT,
	.delim   = "\t"
};

static void strip(char *s)
{
	char *p;
	for (p = s+strlen(s)-1; p >= s && isspace(*p); *p-- = '\0')
		;
}

static void escape(char *buf, int len)
{
}

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

void alarm_handler(int sig)
{
	printf("Timed out after %d seconds\n", OPTS.timeout);
	alarm(0); exit(3);
}

int process_args(int argc, char **argv)
{
	int c;

	free(OPTS.host);
	OPTS.host = strdup("127.0.0.1");

	while ((c = getopt(argc, argv, "H:p:t:h?")) != -1) {
		switch (c) {
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
			exit(0);
			break;
		}
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct pdu *packets = NULL, *pdu;
	unsigned int npackets = 0, nsent = 0;
	char buf[MAX_LINE], *str;
	struct sockaddr_in addr;
	int i, len;

	if (process_args(argc, argv) != 0) {
		alarm(0); exit(3);
	}

	iris_init_crc32();

	while (!feof(stdin)) {
		int c = getc(stdin);
		if (c == -1) break;

		int i = 0;
		while (c != '\x17') {
			if (c == -1) break;
			buf[i++] = c;
			c = getc(stdin);
		}
		buf[i] = '\0';
		strip(buf);
		if (!*buf) continue;

		// new packet!
		struct pdu *re = realloc(packets, (npackets + 1) * sizeof(struct pdu));
		if (!re) {
			fprintf(stderr, "memory exhausted.\n");
			alarm(0); exit(2);
		}
		packets = re;
		pdu = packets+npackets;
		memset(pdu, 0, sizeof(struct pdu));

		str = strtok(buf, OPTS.delim);
		if (!str) continue;
		strncpy(pdu->host, str, IRIS_PDU_HOST_LEN-1);

		str = strtok(NULL, OPTS.delim);
		if (!str) continue;
		strncpy(pdu->service, str, IRIS_PDU_SERVICE_LEN-1);

		str = strtok(NULL, OPTS.delim);
		if (!str) continue;
		pdu->rc = (int16_t)atoi(str);

		str = strtok(NULL, "\0");
		if (!str) continue;
		strncpy(pdu->output, str, IRIS_PDU_OUTPUT_LEN-1);
		escape(pdu->output, IRIS_PDU_OUTPUT_LEN);

		npackets++;
	}

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

	int sock = socket(AF_INET, SOCK_STREAM, 0);
	if (sock < 0) {
		fprintf(stderr, "failed to create a socket: %s", strerror(errno));
		exit(3);
	}

	if (connect(sock, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "connection failed: %s", strerror(errno));
		exit(2);
	}

	signal(SIGALRM, alarm_handler);
	alarm(OPTS.timeout);

	for (i = 0; i < npackets; i++) {
		time((time_t*)&packets[i].ts);

		packets[i].version = htons(IRIS_PROTOCOL_VERSION);
		packets[i].ts      = htonl((uint32_t)packets[i].ts);
		packets[i].rc      = htons(packets[i].rc);
		packets[i].crc32   = htonl(iris_crc32((char*)(&packets[i]), sizeof(struct pdu)));

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
	}
	close(sock);
	fprintf(stderr, "sent %d results to host\n", nsent);
	alarm(0);
	return 0;
}
