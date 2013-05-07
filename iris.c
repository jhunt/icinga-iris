#include "iris.h"

struct client *CLIENTS = NULL;
unsigned int NUM_CLIENTS = 0;

extern void iris_call_submit_result(struct pdu *pdu);
extern int iris_call_recv_data(int fd);
extern void vlog(unsigned int level, const char *fmt, ...);

unsigned long CRC32[256] = {0};

void strip(char *s)
{
	char *p;
	for (p = s+strlen(s)-1; p >= s && isspace(*p); *p-- = '\0')
		;
}

unsigned long crc32(void *ptr, int len)
{
	register unsigned long crc;
	int i, j;
	uint8_t c, *buf = (uint8_t*)ptr;

	if (CRC32[144] == 0) {
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

	if (!buf) return 0;

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

ssize_t pdu_read(int fd, uint8_t *buf, size_t start)
{
	ssize_t n = 0, len = 4300 - start, offset = 0;
	uint8_t *ptr = buf+start;

	if (!buf) {
		vdebug("IRIS: pdu_read: received a NULL buffer...");
		errno = EINVAL;
		return -1;
	}

	if (len < 0) {
		vlog(LOG_WARN, "IRIS: pdu_read told to start reading at %d for fd %d, which it won't do",
				start, fd);
		errno = EINVAL;
		return -1;
	}

	if (start != 0) {
		vdebug("IRIS: pdu_read: starting at byte offset %d for read of %d bytes from %s fd %d",
				start, len, client_addr(fd), fd);
	}

	errno = 0;
	while (len > 0 && (n = read(fd, ptr+offset, len)) > 0) {
		 offset += n; len -= n;
	}
	if (n < 0) {
		if (errno == EAGAIN || errno == EWOULDBLOCK)
			return offset;
		return n;
	}
	return offset;
}

ssize_t pdu_write(int fd, const uint8_t *buf)
{
	ssize_t n, len = 4300;
	const uint8_t *off = buf;

	errno = 0;
	while ((n = write(fd, off, len)) > 0) {
		 off += n; len -= n;
	}
	return n < 0 ? n : off - buf;
}

void pdu_dump(const struct pdu *pdu)
{
	if (!pdu) return;

	int pfd[2];
	if (pipe(pfd) != 0) {
		fprintf(stderr, "Failed to dump PDU via `/usr/bin/od -a': %s",
				strerror(errno));
		return;
	}

	if (fork() == 0) {
		close(pfd[0]);
		dup2(pfd[1], 0);
		//dup2(2,1); // 1>&2

		execl("/usr/bin/od", "-a", NULL);
		fprintf(stderr, "Failed to dump PDU via `/usr/bin/od -a': %s",
				strerror(errno));
		exit(42);
	}
	close(pfd[1]);
	write(pfd[0], pdu, sizeof(struct pdu));
	close(pfd[0]);
}

int pdu_pack(struct pdu *pdu)
{
	if (!pdu) return -1;

	pdu->version = htons(IRIS_PROTOCOL_VERSION); // LCOV_EXCL_LINE
	pdu->ts      = htonl((uint32_t)pdu->ts);     // LCOV_EXCL_LINE
	pdu->rc      = htons(pdu->rc);               // LCOV_EXCL_LINE

	pdu->crc32   = 0x0000;
	pdu->crc32   = htonl(crc32((uint8_t*)pdu, sizeof(struct pdu))); // LCOV_EXCL_LINE
	return 0;
}

int pdu_unpack(struct pdu *pdu)
{
	if (!pdu) return -1;

	uint32_t our_crc, their_crc;
	long age;
	time_t now;

	// check the CRC32
	their_crc = ntohl(pdu->crc32); // LCOV_EXCL_LINE
	pdu->crc32 = 0L;
	our_crc = crc32((uint8_t*)pdu, sizeof(struct pdu));
	// put the CRC32 back...
	pdu->crc32 = their_crc;
	if (our_crc != their_crc) {
		vlog(LOG_INFO, "IRIS: Bogus Packet - CRC mismatch (calculated %x != %x)", our_crc, their_crc);
		vdump(pdu);
		return -1;
	}

	pdu->version = ntohs(pdu->version); // LCOV_EXCL_LINE
	pdu->rc      = ntohs(pdu->rc);      // LCOV_EXCL_LINE
	pdu->ts      = ntohl(pdu->ts);      // LCOV_EXCL_LINE

	if (pdu->version != IRIS_PROTOCOL_VERSION) {
		vlog(LOG_INFO, "IRIS: Bogus Packet - Incorrect PDU version (got %d, wanted %d)", pdu->version, IRIS_PROTOCOL_VERSION);
		return -1;
	}

	// check packet age
	time(&now);
	if (pdu->ts > (uint32_t)(now)) {
		age = (long)(pdu->ts - now);
	} else {
		age = (long)(now - pdu->ts);
	}
	if (age > 900) { // FIXME: configuration file?
		vlog(LOG_INFO, "IRIS: Bogus Packet - PDU timestamp is %ds in the %s", age,
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
		vlog(LOG_ERROR, "IRIS: getaddrinfo failed: %s", gai_strerror(rc));
		return -1;
	}

	head = res;
	do { // FIXME: test on multi-homed hosts
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

	if (nonblocking(fd) != 0 || listen(fd, SOMAXCONN) < 0) {
		close(fd);
		return -1;
	}

	return fd;
}

int net_poller(int sockfd)
{
	int epfd;
	struct epoll_event ev;

	if ((epfd = epoll_create(42)) < 0) return -1;

	ev.data.fd = sockfd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &ev) != 0) return -1;
	return epfd;
}

int net_accept(int sockfd, int epfd)
{
	struct sockaddr_in in_addr;
	socklen_t in_len = sizeof(in_addr);
	struct epoll_event ev;
	struct client *client;
	int connfd;

	vdebug("IRIS: accepting inbound connection");
	if ((connfd = accept(sockfd, (struct sockaddr*)&in_addr, &in_len)) < 0) {
		// EAGAIN / EWOULDBLOCK == no more pending connections
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			vdebug("IRIS: accept bailed with an EAGAIN, done accepting inbound connections");
			return -1;
		}

		vlog(LOG_WARN, "IRIS: accept failed: %s", strerror(errno));
		return -1;
	}

	if (nonblocking(connfd) < 0) {
		vlog(LOG_ERROR, "IRIS: failed to make new socket non-blocking: %s", strerror(errno));
		vdebug("IRIS: closing fd %d", connfd);
		close(connfd);
		return -1;
	}

	ev.data.fd = connfd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) != 0) {
		vlog(LOG_ERROR, "IRIS: failed to inform epoll about new socket fd: %s", strerror(errno));
		vdebug("IRIS: closing fd %d", connfd);
		close(connfd);
		return -1;
	}

	if ((client = client_new(connfd, &(in_addr.sin_addr))) != NULL) {
		vdebug("IRIS: accepted inbound connection from %s, fd %d", client->addr, connfd);
	}
	return connfd;
}

int net_connect(const char *host, unsigned short port)
{
	struct sockaddr_in addr;
	int fd;

	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port   = htons(port); // LCOV_EXCL_LINE

	if (!inet_aton(host, &addr.sin_addr)) {
		struct hostent *he;
		if (!(he = gethostbyname(host))) return -1;
		memcpy(&addr.sin_addr, he->h_addr, he->h_length);
	}

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd < 0) return -1;

	if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) < 0) return -1;
	return fd;
}

int fd_sink(int fd)
{
	char buf[512];
	size_t n = 0;
	while ((n = read(fd, buf, 512)) > 0)
		;
	return n;
}

#define IRIS_CLI_MAX_LINE 5*1024
int read_packets(FILE *io, struct pdu **result, const char *delim)
{
	if (!result) return -1;

	struct pdu *pdu, *list = NULL;
	int i, n = 0;
	char buf[IRIS_CLI_MAX_LINE], *str, c;

	while (!feof(io)) {
		for (i = 0, c = getc(io);
		     !feof(io) && c != '\x17' && i < IRIS_CLI_MAX_LINE;
			 i++, c = getc(io)) {
				buf[i] = c;
		}
		c = getc(io);
		if (c > 0 && c != '\n') ungetc(c, io);

		buf[i] = '\0';
		strip(buf);
		if (!*buf) continue;

		// new packet!
		struct pdu *re = realloc(list, (n+1) * sizeof(struct pdu));
		if (!re) return -1;
		pdu = (list = re)+n;
		memset(pdu, 0, sizeof(struct pdu));

		str = strtok(buf, delim);
		if (!str) continue;
		strncpy(pdu->host, str, IRIS_PDU_HOST_LEN-1);

		str = strtok(NULL, delim);
		if (!str) continue;
		strncpy(pdu->service, str, IRIS_PDU_SERVICE_LEN-1);

		str = strtok(NULL, delim);
		if (!str || !str[0] || str[1])    continue; // too short or too long...
		if (str[0] < '0' || str[0] > '3') continue; // not 0-3
		pdu->rc = str[0] - '0';

		str = strtok(NULL, "\0");
		if (!str) continue;
		strncpy(pdu->output, str, IRIS_PDU_OUTPUT_LEN-1);

		n++;
	}

	*result = list;
	return n;
}

void mainloop(int sockfd, int epfd)
{
	int i, n;
	struct epoll_event events[IRIS_EPOLL_MAXFD];

	for (;;) {
		vdebug("IRIS: going into epoll_wait loop");
		n = epoll_wait(epfd, events, sizeof(events), -1);
		vdebug("IRIS: epoll gave us %d fds to work with", n);

		for (i = 0; i < n; i++) {
#ifdef DEBUG
			vdebug("IRIS: activity on %d: %04x =%s%s%s%s",
					events[i].data.fd, events[i].events,
					(events[i].events & EPOLLERR   ? " EPOLLERR"   : ""),
					(events[i].events & EPOLLHUP   ? " EPOLLHUP"   : ""),
					(events[i].events & EPOLLRDHUP ? " EPOLLRDHUP" : ""),
					(events[i].events & EPOLLIN    ? " EPOLLIN"    : ""));
#endif

			// CONNECT event
			if (events[i].data.fd == sockfd) {
				vdebug("IRIS: processing inbound connections", sockfd);
				while (net_accept(sockfd, epfd) >= 0)
					;
				continue;
			}

			// DATA event
			if (events[i].events & EPOLLIN) {
				vdebug("IRIS: processing readable filehandle");
				if (iris_call_recv_data(events[i].data.fd) != 0) {
					vlog(LOG_PROC, "IRIS: event loop terminating (recv_data signalled an error)");
					return;
				}
				continue;
			}

			vlog(LOG_INFO, "IRIS: unhandled activity on %d: %04x =%s%s%s%s",
					events[i].data.fd, events[i].events,
					(events[i].events & EPOLLERR   ? " EPOLLERR"   : ""),
					(events[i].events & EPOLLHUP   ? " EPOLLHUP"   : ""),
					(events[i].events & EPOLLRDHUP ? " EPOLLRDHUP" : ""),
					(events[i].events & EPOLLIN    ? " EPOLLIN"    : ""));
			client_close(events[i].data.fd);
		}
	}
}

int recv_data(int fd)
{
	struct client *c;
	ssize_t len;

	c = client_find(fd);
	if (!c) {
#ifdef DEBUG
		vlog(LOG_WARN, "IRIS: could not find a client session for fd %d", fd);
		int i;
		for (i = 0; i < NUM_CLIENTS; i++) {
			if (CLIENTS[i].fd == -1) continue;
			vlog(LOG_WARN, "IRIS:  client[%d] fd = %d // %s", i,
					CLIENTS[i].fd, CLIENTS[i].addr);
		}
#endif
		return 0;
	}

	vdebug("IRIS: reading from %s, fd %d", c->addr, fd);
	for (;;) {
		vdebug("IRIS >> fd(%d): have %d bytes, want %d total for %s",
				fd, c->offset, sizeof(c->pdu), c->addr);

		len = pdu_read(fd, (uint8_t*)(&c->pdu), c->offset);
		if (len <= 0) {
			if (errno == EAGAIN) return 0;
			if (len == 0)
				vdebug("IRIS: EOF from %s, fd %d", c->addr, fd);
			else
				vlog(LOG_INFO, "IRIS: failed to read from %s: %s",
						c->addr, strerror(errno));

			client_close(fd);
			break;
		}

		c->offset += len;
		c->bytes += len;
		vdebug("IRIS >> fd(%d): read %d (for %d total) from %s",
				fd, len, c->offset, c->addr);

		if (c->offset < sizeof(c->pdu)) {
			vdebug("IRIS: Read a partial PDU (%d/%d bytes) from %s, fd %d",
					c->offset, sizeof(c->pdu), c->addr, fd);
			continue;
		}

		c->offset = 0;
		if (pdu_unpack(&c->pdu) != 0) {
			vlog(LOG_WARN, "IRIS: discarding bogus packet from %s, fd %d", c->addr, fd);
#ifdef DEBUG
			uint8_t *byte = ((uint8_t*)(&c->pdu));
			int off = 0;
			for (off = 0; off < sizeof(c->pdu) && *(byte+off) == '\0'; off++)
				;
			vdebug("IRIS: first non-null byte in %s recv buffer is at position %d\n", c->addr, off);
#endif
			memset(&c->pdu, 0, sizeof(c->pdu));
			continue;
		}

		vlog(LOG_RESULT, "IRIS: SERVICE RESULT %08x v%d [%d] %s/%s (rc:%d) '%s'",
				c->pdu.crc32, c->pdu.version, (uint32_t)c->pdu.ts,
				c->pdu.host, c->pdu.service, c->pdu.rc, c->pdu.output);

		iris_call_submit_result(&c->pdu);
		memset(&c->pdu, 0, sizeof(c->pdu));
	}
	return 0;
}

int client_init(int n)
{
	CLIENTS = calloc(n, sizeof(struct client));
	if (!CLIENTS) {
		vdebug("IRIS: client_init() malloc(%d * client) failed: %s", n, strerror(errno));
		return -1;
	}
	NUM_CLIENTS = n;
	for (n = 0; n < NUM_CLIENTS; n++) {
		CLIENTS[n].fd = -1;
	}
	return NUM_CLIENTS;
}

void client_deinit(void)
{
	int n;
	for (n = 0; n < NUM_CLIENTS; n++) {
		if (CLIENTS[n].fd >= 0) {
			vdebug("IRIS: closing connected client fd %d", CLIENTS[n].fd);
			close(CLIENTS[n].fd);
		}
	}
	free(CLIENTS);
}

struct client* client_find(int fd)
{
	int i;
	for (i = 0; i < NUM_CLIENTS; i++) {
		if (CLIENTS[i].fd == fd)
			return &CLIENTS[i];
	}
	return NULL;
}

struct client* client_new(int fd, void *ip)
{
	struct client *c = client_find(-1);
	if (!c) {
		vdebug("IRIS: client_new() failed to find a free slot.  Perhaps you need to adjust max_clients");
		return NULL;
	}

	if (ip)
		inet_ntop(AF_INET, ip, c->addr, sizeof(c->addr));
	else
		strcpy(c->addr, "<unknown-peer>");

	c->fd = fd;
	c->offset = 0;
	c->bytes = 0;
	memset(&c->pdu, 0, sizeof(struct pdu));

	vdebug("IRIS: client %d // %s session starting", fd, c->addr);
	vdebug("IRIS: client %d {fd: '%d', offset: '%d', addr: '%s', pdu: <ignored>'}",
			fd, c->fd, c->offset, c->addr);

	return c;
}

void client_close(int fd)
{
	struct client *c = client_find(fd);
	if (c) {
		vdebug("IRIS: client %d // %s session ending (sent %d bytes)", fd, c->addr, c->bytes);
		vdebug("IRIS: closing connection from %s, fd %d", c->addr, fd);
		c->fd = -1;
		close(fd);
	}
}

const char *client_addr(int fd)
{
	struct client *c = client_find(fd);
	if (c) return c->addr;
	return NULL;
}
