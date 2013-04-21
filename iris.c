#define VERSION "1.8.7"

#include <sys/types.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <pthread.h>

#define NSCORE
#include "nebmodules.h"
#include "nebcallbacks.h"

#include "nebstructs.h"
#include "broker.h"

#include "config.h"
#include "common.h"
#include "icinga.h"

/* Maximum number of file descriptors that a single epoll_wait
   will return.  This is *not* the max of pollable FDs. */
#define IRIS_MAXFD       64

#define PDU_MAX_HOST       64
#define PDU_MAX_SERVICE   128
#define PDU_MAX_OUTPUT   4096
#define OLD_MAX_OUTPUT    512
#define OLD_PDU_SIZE     (sizeof(struct data_pdu) - PDU_MAX_OUTPUT + OLD_MAX_OUTPUT)

#define PDU_IV_SIZE       128
#define PDU_IV "17f712e9365a68f51ae54429890feb5aaea9e27a3d9df8aa3d9589924ad900c6" \
               "aa132daa53bb392e82453fdb81139fec7aaacbe74195afa88eceff0a83c47616"

#define PROTOCOL_V3  3

NEB_API_VERSION(CURRENT_NEB_API_VERSION);

/*************************************************************/

struct init_pdu {
	char     iv[PDU_IV_SIZE];
	uint32_t ts;
};

struct data_pdu {
	int16_t  version;
	uint32_t crc32;
	uint32_t ts;
	int16_t  rc;
	char     host[PDU_MAX_HOST];
	char     service[PDU_MAX_SERVICE];
	char     output[PDU_MAX_OUTPUT];
};

/*************************************************************/

static void *IRIS_MODULE;
pthread_t tid;
unsigned long CRC32[256];

/*************************************************************/

static void iris_log(const char *fmt, ...)
{
	char *buf;
	int n;
	va_list ap;

	va_start(ap, fmt);
	n = vsnprintf(NULL, 0, fmt, ap);
	va_end(ap);

	buf = calloc(n+1, sizeof(char));
	va_start(ap, fmt);
	vsnprintf(buf, n+1, fmt, ap);
	va_end(ap);

	write_to_all_logs(buf, NSLOG_INFO_MESSAGE);
}

#ifdef DEBUG
#  define iris_debug iris_log
#else
#  define iris_debug
#endif

static void iris_init_crc32(void)
{
	unsigned long crc;
	int i, j;

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

static unsigned long iris_crc32(char *buf, int len)
{
	register unsigned long crc;
	int c, i;

	crc = 0xFFFFFFFF;
	for (i = 0; i < len; i++) {
		c = (int)buf[i];
		crc = ((crc >> 8) & 0x00FFFFFF) ^ CRC32[(crc ^ c) & 0xFF];
	}

	return (crc ^ 0xFFFFFFFF);
}

static int iris_read(int fd, char *buf, size_t *len)
{
	size_t n;
	char *off = buf;

	errno = 0;
	while ((n = read(fd, off, *len)) > 0) {
		iris_log("IRIS: iris_read read in %d bytes\n", n);
		 off += n;
		*len -= n;
	}
	if (errno == EAGAIN) {
		iris_log("IRIS: triggered EGAGAIN");
	}
	iris_log("IRIS: iris_read read returned %d\n", n);
	*len = off - buf;

	if (n < 0 && errno == EAGAIN) return 0;
	return n;
}

static int iris_noblock(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) {
		iris_debug("IRIS DEBUG: fcntl(%d, F_GETFL, 0) failed: %s",
				fd, strerror(errno));
		return -1;
	}

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) {
		iris_debug("IRIS DEBUG: fcntl(%d, F_GETFL, %d) failed: %s",
				fd, flags, strerror(errno));
		return -1;
	}

	return 0;
}

static void iris_xor(char *buf, int len, const char *iv)
{
	int i, j;
	for (i = 0, j = 0; i < len; i++, j++) {
		if (i < 16) iris_log("IRIS: WAS buf[%d] = %x, iv[%d] = %x", i, buf[i], j, iv[j]);
		if (j > PDU_IV_SIZE) j = 0;
		buf[i] ^= iv[j];
		if (i < 16) iris_log("IRIS: XOR buf[%d] = %x", i, buf[i]);
	}

#if 0
	const char *pw = "";
	int pwlen = strlen(pw);
	for (i = 0, j = 0; i < len; i++, j++) {
		if (j >= pwlen) j = 0;
		buf[i] ^= pw[j];
	}
#endif

	return;
}

static int iris_decode(struct data_pdu *pdu)
{
	uint32_t our_crc, their_crc;
	long age;
	time_t now;

	iris_xor((char*)(pdu), sizeof(struct data_pdu), PDU_IV);
	pdu->version = ntohs(pdu->version);
	pdu->crc32   = ntohl(pdu->crc32);
	pdu->rc      = ntohs(pdu->rc);
	pdu->ts      = ntohl(pdu->ts);

	if (pdu->version != PROTOCOL_V3) {
		iris_log("IRIS: incorrect PDU version (got %d, wanted 3)", pdu->version);
		return -1;
	}

	// check the CRC32
	their_crc = pdu->crc32;
	pdu->crc32 = 0L;
	our_crc = iris_crc32((char*)pdu, sizeof(struct data_pdu));
	if (our_crc != their_crc) {
		iris_log("CRC mismatch (calculated %x != %x); discarding packet and closing connection",
				our_crc, their_crc);
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
		iris_log("Packet age is %ds in the %s", age,
			(pdu->ts > (uint32_t)(now) ? "future" : "past"));
		return -1;
	}

	// looks good
	return 0;
}

static int iris_bind(const char *host, const char *port)
{
	int fd, rc;
	struct addrinfo hints, *res, *head;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(host, port, &hints, &res);
	if (rc != 0) {
		iris_log("IRIS: getaddrinfo failed: %s", gai_strerror(rc));
		abort();
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

	if (iris_noblock(fd) != 0) {
		iris_log("IRIS: failed to set O_NONBLOCK on network socket");
		abort();
	}
	return fd;
}

static int iris_accept(int sockfd, int epfd)
{
	struct sockaddr_in in_addr;
	socklen_t in_len = sizeof(in_addr);
	struct epoll_event ev;

	iris_log("IRIS: accepting inbound connection");
	int connfd = accept(sockfd, (struct sockaddr*)&in_addr, &in_len);
	if (connfd < 0) {
		// EAGAIN / EWOULDBLOCK == no more pending connections
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) return -1;

		iris_log("IRIS: accept failed: %s", strerror(errno));
		return -1;
	}

	if (iris_noblock(connfd) < 0) {
		iris_log("IRIS: failed to make new socket non-blocking: %s", strerror(errno));
		close(connfd);
		return -1;
	}

	iris_log("IRIS: accepted inbound connection on fd %d", connfd);

	ev.data.fd = connfd;
	ev.events = EPOLLOUT | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) != 0) {
		iris_log("IRIS: failed to inform epoll about new socket fd: %s", strerror(errno));
		close(connfd);
		return -1;
	}

	return connfd;
}

static int iris_send_init(int fd, struct init_pdu *pdu, int epfd)
{
	int rc;
	size_t len = sizeof(struct init_pdu);
	struct epoll_event ev;

	iris_log("IRIS: writing to fd %d", fd);

	memcpy(pdu->iv, PDU_IV, PDU_IV_SIZE);
	time((time_t*)(&pdu->ts));
	pdu->ts = (uint32_t)htonl(pdu->ts);

	rc = write(fd, (char*)pdu, len);
	if (rc < 0) {
		iris_log("IRIS: failed to write data: %s", strerror(errno));
		close(fd);
		return -1;

	} else if (rc < len) {
		iris_log("IRIS: sent %d/%d bytes; closing connection", rc, len);
		close(fd);
		return -1;
	}

	// re-register with epoll as a READ handle
	ev.data.fd = fd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_MOD, fd, &ev) != 0) {
		iris_log("IRIS: failed to update epoll for fd %d: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	return 0;
}

static int iris_recv_data(int fd, struct data_pdu *pdu)
{
	size_t len = sizeof(struct data_pdu);
	char *raw = malloc(len);
	int rc;

	iris_log("IRIS: reading from fd %d", fd);
	if ((rc = iris_read(fd, raw, &len)) < 0) {
		iris_log("IRIS: failed to read from fd %d: %s", fd, strerror(errno));
		close(fd);
		return -1;
	}

	memcpy(pdu, raw, len);
	if (iris_decode(pdu) != 0) {
		iris_log("IRIS: discarding packet from fd %d", fd);
		close(fd);
		return -1;
	}

	iris_log("IRIS: received result for %s/%s (rc:%d) '%s'",
		pdu->host, pdu->service, pdu->rc, pdu->output);
	return 0;
}

static void* iris_daemon(void *udata)
{
	int i, n;
	int sockfd, epfd;
	struct epoll_event event;
	struct epoll_event *events;

	struct init_pdu init;
	struct data_pdu data;

	iris_log("IRIS: starting up the iris daemon on *:5667");

	iris_init_crc32();

	// bind and listen on *:5667
	sockfd = iris_bind(NULL, "5667");
	if (listen(sockfd, SOMAXCONN) < 0) {
		iris_log("IRIS: failed to listen() on socket fd %d", sockfd);
		abort();
	}

	// start up eopll
	if ((epfd = epoll_create(42)) < 0) {
		iris_log("IRIS: epoll initialization failed: %s", strerror(errno));
		abort();
	}

	// register our listening socket
	event.data.fd = sockfd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event) != 0) {
		iris_log("IRIS: epoll_ctl(%d, EPOLL_CTL_ADD, %d, &event) failed: %s",
				epfd, sockfd, strerror(errno));
	}

	// allocate return-value buffer
	events = calloc(IRIS_MAXFD, sizeof(struct epoll_event));

	// and loop
	for (;;) {
		n = epoll_wait(epfd, events, IRIS_MAXFD, 1);
		if (n <= 0) continue;

		iris_log("IRIS: epoll gave us %d fds to work with", n);

		for (i = 0; i < n; i++) {
			iris_debug("IRIS DEBUG: activity on %d:%s%s%s%s", events[i].data.fd,
					(events[i].events & EPOLLERR ? " EPOLLERR" : ""),
					(events[i].events & EPOLLHUP ? " EPOLLHUP" : ""),
					(events[i].events & EPOLLIN  ? " EPOLLIN"  : ""),
					(events[i].events & EPOLLOUT ? " EPOLLOUT" : ""));

			// ERROR event
			if ((events[i].events & EPOLLERR) ||
			    (events[i].events & EPOLLHUP) ||
			    (!(events[i].events & EPOLLIN) &&
			     !(events[i].events & EPOLLOUT))) {

				// What just happened?
				//  - there was an error on the file descriptor :  EPOLLERR
				//  - the other end of the pipe was closed      :  EPOLLHUP
				//  - the file descriptor wasn't read/writable  : !EPOLLIN && !EPOLLOUT

				close(events[i].data.fd);
				continue;
			}

			// CONNECT event
			if (events[i].data.fd == sockfd) {
				iris_debug("IRIS DEBUG: processing inbound connections", sockfd);
				while (iris_accept(sockfd, epfd) >= 0)
					;

			} else if (events[i].events & EPOLLIN) {
				iris_recv_data(events[i].data.fd, &data);

			} else if (events[i].events & EPOLLOUT) {
				iris_send_init(events[i].data.fd, &init, epfd);
			}
		}
	}

	close(sockfd);
	close(epfd);
	free(events);
}

static int iris_process_data(int event, void *data)
{
	nebstruct_process_data *procdata;

	iris_log("IRIS: dispatching PROCESS_DATA event");

	switch (event) {
	case NEBCALLBACK_PROCESS_DATA:
		procdata = (nebstruct_process_data*)data;
		if (procdata->type == NEBTYPE_PROCESS_EVENTLOOPSTART) {
			iris_log("IRIS: dispatching event-loop start");
			pthread_create(&tid, 0, iris_daemon, data);
		}
		return 0;

	default:
		iris_log("IRIS: Unhandled event type: %lu", event);
		return 0;
	}
}

/*************************************************************/

int nebmodule_init(int flags, char *args, nebmodule *mod)
{
	int rc;
	IRIS_MODULE = mod;

	iris_log("IRIS: v" VERSION " starting up");
	iris_debug("IRIS DEBUG: flags=%d, args='%s'", flags, args);

	iris_log("IRIS: registering callbacks");
	rc = neb_register_callback(NEBCALLBACK_PROCESS_DATA, IRIS_MODULE, 0, iris_process_data);
	if (rc != 0) {
		iris_log("IRIS: PROCESS_DATA event registration failed, error %i", rc);
		return 1;
	}

	iris_log("IRIS: startup complete");
	return 0;
}

int nebmodule_deinit(int flags, int reason)
{
	iris_log("IRIX: v" VERSION " shutting down");
	iris_debug("IRIS DEBUG: flags=%d, reason=%d", flags, reason);

	iris_log("IRIS: deregistering callbacks");
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, iris_process_data);

	iris_log("IRIS: shutdown complete");
	return 0;
}
