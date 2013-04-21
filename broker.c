#include "iris.h"

#include <sys/epoll.h>
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
#define IRIS_MAXFD 64

NEB_API_VERSION(CURRENT_NEB_API_VERSION);

/*************************************************************/

static void *IRIS_MODULE;
pthread_t tid;

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
	free(buf);
}

static void iris_noop(const char *fmt, ...)
{
}

#ifdef DEBUG
#  define iris_debug iris_log
#else
#  define iris_debug iris_noop
#endif


static int iris_read(int fd, char *buf, size_t *len)
{
	size_t n;
	char *off = buf;

	errno = 0;
	while ((n = read(fd, off, *len)) > 0) {
		iris_debug("IRIS: iris_read read in %d bytes\n", n);
		 off += n;
		*len -= n;
	}
	iris_debug("IRIS: iris_read read returned %d\n", n);
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

	iris_debug("IRIS: accepting inbound connection");
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

	iris_debug("IRIS: accepted inbound connection on fd %d", connfd);

	ev.data.fd = connfd;
	ev.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &ev) != 0) {
		iris_log("IRIS: failed to inform epoll about new socket fd: %s", strerror(errno));
		close(connfd);
		return -1;
	}

	return connfd;
}

static int iris_pdu_unpack(struct pdu *pdu)
{
	uint32_t our_crc, their_crc;
	long age;
	time_t now;

	// check the CRC32
	their_crc = ntohl(pdu->crc32);
	pdu->crc32 = 0L;
	our_crc = iris_crc32((char*)pdu, sizeof(struct pdu));
	if (our_crc != their_crc) {
		iris_log("CRC mismatch (calculated %x != %x); discarding packet and closing connection",
				our_crc, their_crc);
		return -1;
	}

	pdu->version = ntohs(pdu->version);
	pdu->rc      = ntohs(pdu->rc);
	pdu->ts      = ntohl(pdu->ts);

	if (pdu->version != IRIS_PROTOCOL_VERSION) {
		iris_log("IRIS: incorrect PDU version (got %d, wanted %d)", pdu->version, IRIS_PROTOCOL_VERSION);
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

static int iris_recv_data(int fd, struct pdu *pdu)
{
	size_t len = sizeof(struct pdu);
	char *raw = malloc(len);
	int rc;

	iris_debug("IRIS: reading from fd %d", fd);
	if ((rc = iris_read(fd, raw, &len)) < 0) {
		iris_log("IRIS: failed to read from fd %d: %s", fd, strerror(errno));
		free(raw);
		close(fd);
		return -1;
	}
	if (len == 0) {
		free(raw);
		close(fd);
		return -1;
	}

	memcpy(pdu, raw, len);
	free(raw);
	if (iris_pdu_unpack(pdu) != 0) {
		iris_log("IRIS: discarding packet from fd %d", fd);
		close(fd);
		return -1;
	}

	iris_debug("IRIS: received result for v%d [%d] %s/%s (rc:%d) '%s'",
		pdu->version, (uint32_t)pdu->ts, pdu->host, pdu->service, pdu->rc, pdu->output);

	check_result *res = malloc(sizeof(check_result));
	init_check_result(res);

	res->output_file    = NULL;
	res->output_file_fd = -1;

	res->host_name = strdup(pdu->host);
	if (strcmp(pdu->service, "HOST") != 0) {
		res->service_description = strdup(pdu->service);
		res->object_check_type = SERVICE_CHECK;
	}

	res->output = strdup(pdu->output);

	res->return_code = pdu->rc;
	res->exited_ok = 1;
	res->check_type = SERVICE_CHECK_PASSIVE;

	res->start_time.tv_sec = pdu->ts;
	res->start_time.tv_usec = 0;
	res->finish_time = res->start_time;

	add_check_result_to_list(res);
	// Icinga is now responsible for malloc'd _res_ memory

	return 0;
}

static void* iris_daemon(void *udata)
{
	int i, n;
	int sockfd, epfd;
	struct epoll_event event;
	struct epoll_event events[IRIS_MAXFD];

	struct pdu data;

	iris_log("IRIS: starting up the iris daemon on *:%d", IRIS_DEFAULT_PORT);

	iris_init_crc32();

	// bind and listen on *:5667
	sockfd = iris_bind(NULL, IRIS_DEFAULT_PORT_STRING);
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

	// and loop
	for (;;) {
		n = epoll_wait(epfd, events, sizeof(events), 1);
		if (n <= 0) continue;

		iris_debug("IRIS: epoll gave us %d fds to work with", n);

		for (i = 0; i < n; i++) {
			iris_debug("IRIS DEBUG: activity on %d:%s%s%s", events[i].data.fd,
					(events[i].events & EPOLLERR ? " EPOLLERR" : ""),
					(events[i].events & EPOLLHUP ? " EPOLLHUP" : ""),
					(events[i].events & EPOLLIN  ? " EPOLLIN"  : ""));

			// ERROR event
			if ((events[i].events & EPOLLERR) ||
			    (events[i].events & EPOLLHUP) ||
			    !(events[i].events & EPOLLIN)) {

				// What just happened?
				//  - there was an error on the file descriptor :  EPOLLERR
				//  - the other end of the pipe was closed      :  EPOLLHUP
				//  - the file descriptor wasn't readable       : !EPOLLIN

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

	iris_debug("IRIS: dispatching PROCESS_DATA event");

	switch (event) {
	case NEBCALLBACK_PROCESS_DATA:
		procdata = (nebstruct_process_data*)data;
		if (procdata->type == NEBTYPE_PROCESS_EVENTLOOPSTART) {
			iris_debug("IRIS: dispatching event-loop start");
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

	iris_debug("IRIS: registering callbacks");
	rc = neb_register_callback(NEBCALLBACK_PROCESS_DATA, IRIS_MODULE, 0, iris_process_data);
	if (rc != 0) {
		iris_log("IRIS: PROCESS_DATA event registration failed, error %i", rc);
		return 1;
	}

	iris_debug("IRIS: startup complete");
	return 0;
}

int nebmodule_deinit(int flags, int reason)
{
	iris_log("IRIX: v" VERSION " shutting down");
	iris_debug("IRIS DEBUG: flags=%d, reason=%d", flags, reason);

	iris_debug("IRIS: deregistering callbacks");
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, iris_process_data);

	iris_debug("IRIS: shutdown complete");
	return 0;
}
