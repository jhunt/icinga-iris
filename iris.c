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
#define IRIS_MAXFD 64

/*************************************************************/

NEB_API_VERSION(CURRENT_NEB_API_VERSION);

void *IRIS_MODULE = NULL;
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
}

#ifdef DEBUG
#  define iris_debug iris_log
#else
#  define iris_debug
#endif

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
		abort();
	}
	return fd;
}

static int iris_accept(int sockfd)
{
	struct sockaddr_in in_addr;
	socklen_t in_len = sizeof(in_addr);

	iris_log("IRIS: accepting inbound connection");
	int connfd = accept(sockfd, (struct sockaddr*)&in_addr, &in_len);
	if (connfd < 0) {
		// EAGAIN / EWOULDBLOCK == no more pending connections
		if ((errno == EAGAIN) || (errno == EWOULDBLOCK)) return -1;

		iris_log("IRIS: accept failed: %s", strerror(errno));
		return -1;
	}

	if (iris_noblock(connfd) < 0) {
		iris_log("IRIS: failed to set O_NONBLOCK on inbound fd %i: %s",
				connfd, strerror(errno));
		abort();
	}

	iris_log("IRIS: accepted inbound connection on fd %d", connfd);
	return connfd;
}

static void* iris_daemon(void *data)
{
	int rc, i, n;
	int sockfd, epfd;
	struct epoll_event event;
	struct epoll_event *events;

	iris_log("IRIS: starting up the iris daemon on *:5667");

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
			iris_debug("IRIS DEBUG: activity on %d:%s%s%s", events[i].data.fd,
					(events[i].events & EPOLLERR ? " EPOLLERR" : ""),
					(events[i].events & EPOLLHUP ? " EPOLLHUP" : ""),
					(events[i].events & EPOLLIN  ? " EPOLLIN"  : ""));

			if ((events[i].events & EPOLLERR) ||
			    (events[i].events & EPOLLHUP) ||
			   !(events[i].events & EPOLLIN)) {

				// What just happened?
				//  - there was an error on the file descriptor :  EPOLLERR
				//  - the other end of the pipe was closed      :  EPOLLHUP
				//  - the file descriptor wasn't readable ???   : !EPOLLIN

				close(events[i].data.fd);
				continue;
			}

			if (events[i].data.fd == sockfd) {
				// one or more connections inbound
				iris_debug("IRIS DEBUG: activity on socket fd %d, processing inbound connections", sockfd);

				for (;;) {
					int connfd = iris_accept(sockfd);
					if (connfd < 0) break;

					iris_log("IRIS: setting epoll watch on fd %d", connfd);

					event.data.fd = connfd;
					event.events = EPOLLIN | EPOLLET;
					if (epoll_ctl(epfd, EPOLL_CTL_ADD, connfd, &event) != 0) {
						iris_log("IRIS: epoll_ctl(%d, EPOLL_CTL_ADD, %d, &event) failed: %s",
								epfd, sockfd, strerror(errno));
					}
				}
				continue;
			}

			// some of our clients are readable
			iris_log("IRIS: reading from fd %d", events[i].data.fd);
			for (;;) {
				ssize_t nread;
				char buf[512];

				nread = read(events[i].data.fd, buf, sizeof(buf)-1);
				if (nread < 0) { // error on file descriptor
					if (errno != EAGAIN) {
						iris_log("IRIS: failed to read from fd %d: %s",
								events[i].data.fd, strerror(errno));
						close(events[i].data.fd);
					}
					break;

				} else if (nread == 0) { // EOF, remote end closed
					close(events[i].data.fd);
					break;
				}

				// FIXME: handle buffer reads
				buf[nread] = '\0';
				iris_log("IRIS: read from %d: '%s'", events[i].data.fd, buf);
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

