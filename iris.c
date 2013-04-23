#include "iris.h"

unsigned long CRC32[256] = {0};

void log_info(const char *fmt, ...)
{
	va_list ap;
#ifdef IRIS_EVENT_BROKER
	char *buf;
	int n;

	va_start(ap, fmt);
	n = vasprintf(&buf, fmt, ap);
	va_end(ap);

	write_to_all_logs(buf, NSLOG_INFO_MESSAGE);
	free(buf);
#else

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
#endif
}

void strip(char *s)
{
	char *p;
	for (p = s+strlen(s)-1; p >= s && isspace(*p); *p-- = '\0')
		;
}

static void crc32_init(void)
{
	unsigned long crc;
	int i, j;
	if (CRC32[0]) return;

	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 8; j > 0; j--) {
			if (crc & 1) {
				crc = (crc >> 1) ^ 0xedb88320L;
			} else {
				crc >>= 1;
			}
		}
		CRC32[i] = crc;
	}
}

unsigned long crc32(char *buf, int len)
{
	register unsigned long crc;
	int c, i;

	crc32_init();
	crc = 0xFFFFFFFF;
	for (i = 0; i < len; i++) {
		c = (uint8_t)buf[i];
		crc = ((crc >> 8) & 0x00FFFFFF) ^ CRC32[(crc ^ c) & 0xFF];
	}

	return (crc ^ 0xFFFFFFFF);
}

int nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;

	return 0;
}

int pdu_read(int fd, char *buf, size_t *len)
{
	ssize_t n;
	char *off = buf;

	errno = 0;
	while ((n = read(fd, off, *len)) > 0) {
		 off += n; *len -= n;
	}
	*len = off - buf;
	return n;
}

int pdu_pack(struct pdu *pdu)
{
	pdu->version = htons(IRIS_PROTOCOL_VERSION);
	pdu->ts      = htonl((uint32_t)pdu->ts);
	pdu->rc      = htons(pdu->rc);

	pdu->crc32   = 0x0000;
	pdu->crc32   = htonl(crc32((char*)pdu, sizeof(struct pdu)));
	return 0;
}

int pdu_unpack(struct pdu *pdu)
{
	uint32_t our_crc, their_crc;
	long age;
	time_t now;

	// check the CRC32
	their_crc = ntohl(pdu->crc32);
	pdu->crc32 = 0L;
	our_crc = crc32((char*)pdu, sizeof(struct pdu));
	if (our_crc != their_crc) {
		log_info("CRC mismatch (calculated %x != %x); discarding packet and closing connection",
				our_crc, their_crc);
		return -1;
	}

	pdu->version = ntohs(pdu->version);
	pdu->rc      = ntohs(pdu->rc);
	pdu->ts      = ntohl(pdu->ts);

	if (pdu->version != IRIS_PROTOCOL_VERSION) {
		log_info("IRIS: incorrect PDU version (got %d, wanted %d)", pdu->version, IRIS_PROTOCOL_VERSION);
		return -1;
	}

	// check packet age
	time(&now);
	if (pdu->ts > (uint32_t)(now)) {
		age = (long)(pdu->ts - now);
	} else {
		age = (long)(now - pdu->ts);
	}
	if (age > 30) {
		log_info("Packet age is %ds in the %s", age,
			(pdu->ts > (uint32_t)(now) ? "future" : "past"));
		return -1;
	}

	// looks good
	return 0;
}

int net_bind(const char *host, const char *port)
{
	int fd, rc;
	struct addrinfo hints, *res, *head;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(host, port, &hints, &res);
	if (rc != 0) {
		log_info("IRIS: getaddrinfo failed: %s", gai_strerror(rc));
		return -1;
	}

	head = res;
	do {
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0) continue;

		char on = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

		if (bind(fd, res->ai_addr, res->ai_addrlen) == 0) {
			// bound; stop trying addrinfo results!
			break;
		}

		close(fd);
	} while ((res = res->ai_next) != NULL);

	freeaddrinfo(head);

	if (nonblocking(fd) != 0) {
		log_info("IRIS: failed to set O_NONBLOCK on network socket");
		close(fd);
		return -1;
	}

	if (listen(fd, SOMAXCONN) < 0) {
		log_info("IRIS: failed to listen() on socket fd %d", fd);
		close(fd);
		return -1;
	}
	return fd;
}

int net_accept(int sockfd, int epfd)
{
	struct sockaddr_in in_addr;
	socklen_t in_len = sizeof(in_addr);
	struct epoll_event ev;

	log_debug("IRIS DEBUG: accepting inbound connection");
	int connfd = accept(sockfd, (struct sockaddr*)&in_addr, &in_len);
	if (connfd < 0) {
		// EAGAIN / EWOULDBLOCK == no more pending connections
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) return -1;

		log_info("IRIS: accept failed: %s", strerror(errno));
		return -1;
	}

	if (nonblocking(connfd) < 0) {
		log_info("IRIS: failed to make new socket non-blocking: %s", strerror(errno));
		log_debug("IRIS DEBUG: closing fd %d", connfd);
		close(connfd);
		return -1;
	}

	char addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(in_addr.sin_addr), addr, INET_ADDRSTRLEN);
	log_debug("IRIS DEBUG: accepted inbound connection from %s on fd %d", addr, connfd);

	ev.data.fd = connfd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) != 0) {
		log_info("IRIS: failed to inform epoll about new socket fd: %s", strerror(errno));
		log_debug("IRIS DEBUG: closing fd %d", connfd);
		close(connfd);
		return -1;
	}

	return connfd;
}

#define IRIS_CLI_MAX_LINE 5*1024
int read_packets(FILE *io, struct pdu **result, const char *delim)
{
	struct pdu *pdu, *list = NULL;
	int c, i, n = 0;
	char buf[IRIS_CLI_MAX_LINE], *str;

	while (!feof(io)) {
		for (i = 0, c = getc(io);
		     !feof(io) && c != '\x17' && i < IRIS_CLI_MAX_LINE;
			 i++, c = getc(io)) {
				buf[i] = c;
		}
		c = getc(io);
		if (c > 0 && c != '\n') putc(c, io);

		buf[i] = '\0';
		strip(buf);
		if (!*buf) continue;

		// new packet!
		struct pdu *re = realloc(list, (n+1) * sizeof(struct pdu));
		if (!re) {
			fprintf(stderr, "memory exhausted.\n");
			exit(2);
		}
		list = re;
		pdu = list+n;
		memset(pdu, 0, sizeof(struct pdu));

		str = strtok(buf, delim);
		if (!str) continue;
		strncpy(pdu->host, str, IRIS_PDU_HOST_LEN-1);

		str = strtok(NULL, delim);
		if (!str) continue;
		strncpy(pdu->service, str, IRIS_PDU_SERVICE_LEN-1);

		str = strtok(NULL, delim);
		if (!str) continue;
		pdu->rc = (uint16_t)atoi(str);

		str = strtok(NULL, "\0");
		if (!str) continue;
		strncpy(pdu->output, str, IRIS_PDU_OUTPUT_LEN-1);
		//escape(pdu->output, IRIS_PDU_OUTPUT_LEN);

		n++;
	}

	*result = list;
	return n;
}
