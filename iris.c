#include "iris.h"

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

static void crc32_init(void)
{
	unsigned long crc;
	int i, j;
	if (CRC32[144]) return;

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

ssize_t pdu_read(int fd, char *buf)
{
	ssize_t n, len = 4300;
	char *off = buf;

	errno = 0;
	while ((n = read(fd, off, len)) > 0) {
		 off += n; len -= n;
	}
	return n < 0 ? n : off - buf;
}

ssize_t pdu_write(int fd, const char *buf)
{
	ssize_t n, len = 4300;
	const char *off = buf;

	errno = 0;
	while ((n = write(fd, off, len)) > 0) {
		 off += n; len -= n;
	}
	return n < 0 ? n : off - buf;
}

int pdu_pack(struct pdu *pdu)
{
	pdu->version = htons(IRIS_PROTOCOL_VERSION); // LCOV_EXCL_LINE
	pdu->ts      = htonl((uint32_t)pdu->ts);     // LCOV_EXCL_LINE
	pdu->rc      = htons(pdu->rc);               // LCOV_EXCL_LINE

	pdu->crc32   = 0x0000;
	pdu->crc32   = htonl(crc32((char*)pdu, sizeof(struct pdu))); // LCOV_EXCL_LINE
	return 0;
}

int pdu_unpack(struct pdu *pdu)
{
	uint32_t our_crc, their_crc;
	long age;
	time_t now;

	// check the CRC32
	their_crc = ntohl(pdu->crc32); // LCOV_EXCL_LINE
	pdu->crc32 = 0L;
	our_crc = crc32((char*)pdu, sizeof(struct pdu));
	if (our_crc != their_crc) {
		vlog(LOG_INFO, "CRC mismatch (calculated %x != %x); discarding packet and closing connection",
				our_crc, their_crc);
		return -1;
	}

	pdu->version = ntohs(pdu->version); // LCOV_EXCL_LINE
	pdu->rc      = ntohs(pdu->rc);      // LCOV_EXCL_LINE
	pdu->ts      = ntohl(pdu->ts);      // LCOV_EXCL_LINE

	if (pdu->version != IRIS_PROTOCOL_VERSION) {
		vlog(LOG_INFO, "IRIS: incorrect PDU version (got %d, wanted %d)", pdu->version, IRIS_PROTOCOL_VERSION);
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
		vlog(LOG_INFO, "Packet age is %ds in the %s", age,
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

	vdebug("IRIS: accepting inbound connection");
	int connfd = accept(sockfd, (struct sockaddr*)&in_addr, &in_len);
	if (connfd < 0) {
		// EAGAIN / EWOULDBLOCK == no more pending connections
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) return -1;

		vlog(LOG_WARN, "IRIS: accept failed: %s", strerror(errno));
		return -1;
	}

	if (nonblocking(connfd) < 0) {
		vlog(LOG_ERROR, "IRIS: failed to make new socket non-blocking: %s", strerror(errno));
		vdebug("IRIS: closing fd %d", connfd);
		close(connfd);
		return -1;
	}

	char addr[INET_ADDRSTRLEN];
	inet_ntop(AF_INET, &(in_addr.sin_addr), addr, INET_ADDRSTRLEN);
	vdebug("IRIS: accepted inbound connection from %s on fd %d", addr, connfd);

	ev.data.fd = connfd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) != 0) {
		vlog(LOG_ERROR, "IRIS: failed to inform epoll about new socket fd: %s", strerror(errno));
		vdebug("IRIS: closing fd %d", connfd);
		close(connfd);
		return -1;
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

			// ERROR event
			if ((events[i].events & EPOLLERR)   || // error on fd
			    (events[i].events & EPOLLHUP)   || // force closure (thanks, kernel)
			    (events[i].events & EPOLLRDHUP) || // client shutdown(x, SHUT_WR)
			    !(events[i].events & EPOLLIN)) {   // not really readable (???)

				vdebug("IRIS: closing fd %d", events[i].data.fd);
				close(events[i].data.fd);
				continue;
			}

			// CONNECT event
			if (events[i].data.fd == sockfd) {
				vdebug("IRIS: processing inbound connections", sockfd);
				while (net_accept(sockfd, epfd) >= 0)
					;

			} else if (events[i].events & EPOLLIN) {
				switch (iris_call_recv_data(events[i].data.fd)) {
					case 0:
						break;

					case -2:
						vdebug("IRIS: mainloop terminating on request");
						return;

					case -1:
					default:
						vdebug("IRIS: closing fd %d", events[i].data.fd);
						close(events[i].data.fd);
				}
			}
		}
	}
}

int recv_data(int fd)
{
	struct pdu pdu;
	ssize_t len;

	vdebug("IRIS: reading from fd %d", fd);
	for (;;) {
		vdebug("Reading from fd %d", fd);
		memset(&pdu, 0, sizeof(pdu));
		len = pdu_read(fd, (char*)&pdu);
		vdebug("IRIS: read %d bytes from fd %d", len, fd);

		if (len <= 0) {
			if (errno == EAGAIN) return 0;
			if (len == 0)
				vdebug("IRIS: reached EOF on fd %d", fd);
			else
				vlog(LOG_INFO, "IRIS: failed to read from fd %d: %s", fd, strerror(errno));
			return -1;
		}

		if (pdu_unpack(&pdu) != 0) {
			// FIXME: stop using fds, start using client IP
			vlog(LOG_WARN, "IRIS: discarding bogus packet from fd %d", fd);
			return -1;
		}

		vlog(LOG_RESULT, "IRIS: SERVICE RESULT %04x v%d [%d] %s/%s (rc:%d) '%s'",
				pdu.crc32, pdu.version, (uint32_t)pdu.ts,
				pdu.host, pdu.service, pdu.rc, pdu.output);

		iris_call_submit_result(&pdu);
	}
	return 0;
}
