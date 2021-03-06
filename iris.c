#include "iris.h"

#if _POSIX_MONOTONIC_CLOCK > 0
#  error "_POSIX_MONOTONIC_CLOCK not set; is the MONOTONIC clock available?"
#else
#endif

struct client *CLIENTS = NULL;
unsigned int NUM_CLIENTS = 0;
time_t MAX_LIFETIME = 20;

extern void iris_call_submit_result(struct pdu *pdu);
extern int iris_call_recv_data(int fd);
extern int iris_call_register_fd(int fd);

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

int close_on_exec(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;

	flags |= FD_CLOEXEC;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;

	return 0;
}

int server_init(struct server *s)
{
	if (!s) {
		errno = EINVAL;
		return 1;
	}

	s->port            = strdup("5668");
	s->max_clients     = 16 * 1024;
	s->max_lifetime    = MAX_LIFETIME;
	s->syslog_ident    = strdup("iris");
	s->syslog_facility = strdup("daemon");

	if (!s->port)            return 2;
	if (!s->syslog_ident)    return 2;
	if (!s->syslog_facility) return 2;
	return 0;
}

int parse_config_file(const char *file, struct server *s)
{
	FILE *io;
	int rc;

	io = fopen(file, "r");
	if (!io) return -1;

	rc = parse_config(io, s);
	fclose(io);
	return rc;
}

int parse_config(FILE *io, struct server *s)
{
	char buf[8192];
	char *a, *b;

	char *directive;
	char *value;
	char *endptr;

	int line = 0;

	while (fgets(buf, 8191, io)) {
		line++;

		for (a = buf; *a && isspace(*a); a++)
			/* skip whitespace */;
		if (*a == '#') continue; /* skip comments */
		if (!*a) continue; /* blank line */

		/* parse the configuration directive token */
		for (b = a; *b && !isspace(*b) && *b != '='; b++)
			/* a...b = directive */;
		if (a == b || !*b) {
			fprintf(stderr, "malformed configuration, line %d: %s\n", line, buf);
			return 1;
		}
		directive = a; *b = '\0';

		/* skip past m{\s*=*} */
		for (a = b+1; *a && *a != '='; a++)
			;
		if (!*a) {
			fprintf(stderr, "malformed configuration, line %d: %s\n", line, buf);
			return 2;
		}
		for (a++; *a && isspace(*a); a++)
			/* skip whitespace */;

		/* parse the configuration value */
		for (b = a; *b && !isspace(*b); b++)
			/* a...b = value */;
		if (a == b) {
			fprintf(stderr, "malformed configuration, line %d: %s\n", line, buf);
			return 3;
		}
		value = a; *b = '\0';

		if (strcmp(directive, "port") == 0) {
			free(s->port);
			s->port = strdup(value);
		} else if (strcmp(directive, "max_clients") == 0) {
			errno = 0;
			s->max_clients = strtol(value, &endptr, 10);
			if (errno != 0 || endptr == value) {
				fprintf(stderr, "malformed configuration, line %d: %s\n", line, buf);
				return 4;
			}
		} else if (strcmp(directive, "max_lifetime") == 0) {
			errno = 0;
			s->max_lifetime = strtol(value, &endptr, 10);
			if (errno != 0 || endptr == value) {
				fprintf(stderr, "malformed configuration, line %d: %s\n", line, buf);
				return 5;
			}
			MAX_LIFETIME = s->max_lifetime;
		} else if (strcmp(directive, "syslog_ident") == 0) {
			free(s->syslog_ident);
			s->syslog_ident = strdup(value);
		} else if (strcmp(directive, "syslog_facility") == 0) {
			for (a = value; *a; a++)
				*a = tolower(*a);
			free(s->syslog_facility);
			s->syslog_facility = strdup(value);
		} else {
			fprintf(stderr, "unrecognized configuration directive '%s', on line %i\n",
				directive, line);
		}
	}
	return 0;
}

ssize_t pdu_read(int fd, uint8_t *buf, size_t start)
{
	ssize_t n = 0, len = 4300 - start, offset = 0;
	uint8_t *ptr = buf+start;

	if (!buf) {
		vdebug("pdu_read: received a NULL buffer...");
		errno = EINVAL;
		return -1;
	}

	if (len < 0) {
		syslog(LOG_WARN, "pdu_read told to start reading at %lu for fd %i, which it won't do",
				start, fd);
		errno = EINVAL;
		return -1;
	}

	if (start != 0) {
		vdebug("pdu_read: starting at byte offset %lu for read of %lu bytes from %s fd %d",
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

	errno = EINVAL;
	if (!buf) return -1;

	errno = 0;
	while (len > 0 && (n = write(fd, off, len)) > 0) {
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
		close(pfd[1]);
		dup2(pfd[0], 0);
		//dup2(2,1); // 1>&2

		execl("/usr/bin/od", "od", "-a", NULL);
		fprintf(stderr, "Failed to dump PDU via `/usr/bin/od -a': %s",
				strerror(errno));
		exit(42);
	}
	close(pfd[0]);
	write(pfd[1], pdu, sizeof(struct pdu));
	close(pfd[1]);
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
		syslog(LOG_INFO, "Bogus Packet - CRC mismatch (calculated %x != %x)", our_crc, their_crc);
		vdump(pdu);
		return -1;
	}

	pdu->version = ntohs(pdu->version); // LCOV_EXCL_LINE
	pdu->rc      = ntohs(pdu->rc);      // LCOV_EXCL_LINE
	pdu->ts      = ntohl(pdu->ts);      // LCOV_EXCL_LINE

	if (pdu->version != IRIS_PROTOCOL_VERSION) {
		syslog(LOG_INFO, "Bogus Packet - Incorrect PDU version (got %d, wanted %d)", pdu->version, IRIS_PROTOCOL_VERSION);
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
		syslog(LOG_INFO, "Bogus Packet - PDU timestamp is %lus in the %s", age,
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
		syslog(LOG_ERROR, "getaddrinfo failed: %s", gai_strerror(rc));
		return -1;
	}

	head = res;
	do { // FIXME: test on multi-homed hosts
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0) {
			syslog(LOG_ERROR, "socket() call failed: %s", strerror(errno));
			continue;
		}

		int on = 1;
		if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on)) < 0) {
			syslog(LOG_ERROR, "failed to set SO_REUSEADDR on socket: %s", strerror(errno));
			close(fd);
			fd = -1;
			continue;
		}

		if (bind(fd, res->ai_addr, res->ai_addrlen) == 0) {
			// bound; stop trying addrinfo results!
			break;
		}

		close(fd);
	} while ((res = res->ai_next) != NULL);

	freeaddrinfo(head);

	if (nonblocking(fd) != 0) {
		syslog(LOG_ERROR, "failed to set socket non-blocking: %s", strerror(errno));
		close(fd);
		return -1;
	}
	if (close_on_exec(fd) != 0) {
		syslog(LOG_ERROR, "failed to set socket close-on-exec: %s", strerror(errno));
		close(fd);
		return -1;
	}
	if (listen(fd, SOMAXCONN) < 0) {
		syslog(LOG_ERROR, "listen() call failed: %s", strerror(errno));
		close(fd);
		return -1;
	}

	iris_call_register_fd(fd);
	return fd;
}

int net_poller(int sockfd)
{
	int epfd;
	struct epoll_event ev;
	memset(&ev, 0, sizeof(ev));

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
	struct epoll_event ev = {0};
	struct client *client;
	int connfd;

	clients_purge();
	vdebug("accepting inbound connection");
	if ((connfd = accept(sockfd, (struct sockaddr*)&in_addr, &in_len)) < 0) {
		// EAGAIN / EWOULDBLOCK == no more pending connections
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) {
			vdebug("accept bailed with an EAGAIN, done accepting inbound connections");
			return -1;
		}

		syslog(LOG_WARN, "accept failed: %s", strerror(errno));
		return -1;
	}

	if (nonblocking(connfd) < 0) {
		syslog(LOG_ERROR, "failed to make new socket non-blocking: %s", strerror(errno));
		vdebug("closing fd %d", connfd);
		close(connfd);
		return -1;
	}

	ev.data.fd = connfd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) != 0) {
		syslog(LOG_ERROR, "failed to inform epoll about new socket fd: %s", strerror(errno));
		vdebug("closing fd %d", connfd);
		close(connfd);
		return -1;
	}

	if ((client = client_new(connfd, &(in_addr.sin_addr))) != NULL) {
		vdebug("accepted inbound connection from %s, fd %d", client->addr, connfd);
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
		vdebug("going into epoll_wait loop");
		n = epoll_wait(epfd, events, IRIS_EPOLL_MAXFD, -1);
		vdebug("epoll gave us %d fds to work with", n);

		for (i = 0; i < n; i++) {
#ifdef DEBUG
			vdebug("activity on %d: %04x =%s%s%s%s",
					events[i].data.fd, events[i].events,
					(events[i].events & EPOLLERR   ? " EPOLLERR"   : ""),
					(events[i].events & EPOLLHUP   ? " EPOLLHUP"   : ""),
					(events[i].events & EPOLLRDHUP ? " EPOLLRDHUP" : ""),
					(events[i].events & EPOLLIN    ? " EPOLLIN"    : ""));
#endif

			// CONNECT event
			if (events[i].data.fd == sockfd) {
				vdebug("processing inbound connection on %d", sockfd);
				while (net_accept(sockfd, epfd) >= 0)
					;
				continue;
			}

			// DATA event
			if (events[i].events & EPOLLIN) {
				vdebug("processing readable filehandle");
				if (iris_call_recv_data(events[i].data.fd) != 0) {
					syslog(LOG_PROC, "event loop terminating (recv_data signalled an error)");
					return;
				}
				continue;
			}

			syslog(LOG_INFO, "unhandled activity on %d: %04x =%s%s%s%s",
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
		syslog(LOG_WARN, "could not find a client session for fd %d", fd);
		int i;
		for (i = 0; i < NUM_CLIENTS; i++) {
			if (CLIENTS[i].fd == -1) continue;
			syslog(LOG_WARN, " client[%d] fd = %d // %s", i,
					CLIENTS[i].fd, CLIENTS[i].addr);
		}
#endif
		return 0;
	}

	vdebug("reading from %s, fd %d", c->addr, fd);
	for (;;) {
		vdebug("IRIS >> fd(%d): have %d bytes, want %lu total for %s",
				fd, c->offset, sizeof(c->pdu), c->addr);

		len = pdu_read(fd, (uint8_t*)(&c->pdu), c->offset);
		if (len <= 0) {
			if (errno == EAGAIN) return 0;
			if (len == 0)
				vdebug("EOF from %s, fd %d", c->addr, fd);
			else
				syslog(LOG_INFO, "failed to read from %s: %s",
						c->addr, strerror(errno));

			client_close(fd);
			break;
		}

		c->offset += len;
		c->bytes += len;
		vdebug("IRIS >> fd(%d): read %lu (for %d total) from %s",
				fd, len, c->offset, c->addr);

		if (c->offset < sizeof(c->pdu)) {
			vdebug("Read a partial PDU (%d/%lu bytes) from %s, fd %d",
					c->offset, sizeof(c->pdu), c->addr, fd);
			continue;
		}

		c->offset = 0;
		if (pdu_unpack(&c->pdu) != 0) {
			syslog(LOG_WARN, "discarding bogus packet from %s, fd %d", c->addr, fd);
#ifdef DEBUG
			uint8_t *byte = ((uint8_t*)(&c->pdu));
			int off = 0;
			for (off = 0; off < sizeof(c->pdu) && *(byte+off) == '\0'; off++)
				;
			vdebug("first non-null byte in %s recv buffer is at position %d\n", c->addr, off);
#endif
			memset(&c->pdu, 0, sizeof(c->pdu));
			continue;
		}

		syslog(LOG_RESULT, "SERVICE RESULT %08x v%d [%d] %s/%s (rc:%d) '%s'",
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
		vdebug("client_init() malloc(%d * client) failed: %s", n, strerror(errno));
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
			vdebug("closing connected client fd %d", CLIENTS[n].fd);
			close(CLIENTS[n].fd);
		}
	}
	// FIXME: don't free CLIENTS yet, we need ITM-2357
	//free(CLIENTS);
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
		vdebug("client_new() failed to find a free slot.  Perhaps you need to adjust max_clients");
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
	clock_gettime(CLOCK_MONOTONIC, &c->deadline);
	c->deadline.tv_sec += MAX_LIFETIME;

	vdebug("client %d // %s session starting", fd, c->addr);
	vdebug("client %d {fd: '%d', offset: '%d', addr: '%s', pdu: <ignored>'}",
			fd, c->fd, c->offset, c->addr);

	return c;
}

void client_close(int fd)
{
	struct client *c = client_find(fd);
	if (c) {
		vdebug("client %d // %s session ending (sent %lu bytes)", fd, c->addr, c->bytes);
		vdebug("closing connection from %s, fd %d", c->addr, fd);
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

#define past_deadline(n,d) ((n).tv_sec > (d).tv_sec || \
                            ((n).tv_sec  == (d).tv_sec && \
                             (n).tv_nsec >= (d).tv_nsec))

void clients_purge(void)
{
	struct timespec now;
	int i;

	clock_gettime(CLOCK_MONOTONIC, &now);
	for (i = 0; i < NUM_CLIENTS; i++) {
		if (CLIENTS[i].fd >= 0 && past_deadline(now, CLIENTS[i].deadline)) {
			vdebug("client %d // %s is past deadline of %i.%i (now = %i.%i)",
					CLIENTS[i].fd, CLIENTS[i].addr,
					(int)CLIENTS[i].deadline.tv_sec, (int)CLIENTS[i].deadline.tv_nsec,
					(int)now.tv_sec, (int)now.tv_nsec);
			client_close(CLIENTS[i].fd);
		}
	}
}
