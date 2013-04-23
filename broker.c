#define IRIS_EVENT_BROKER
#include "iris.h"

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

/* Define EPOLLRDHUP ourselves, if the kernel didn't do it already.
   see http://sourceware.org/bugzilla/show_bug.cgi?id=5040 */
#ifndef EPOLLRDHUP
#  define EPOLLRDHUP 0x2000
#endif

NEB_API_VERSION(CURRENT_NEB_API_VERSION);

/*************************************************************/

static void *IRIS_MODULE;
pthread_t tid;

/*************************************************************/

static int iris_recv_data(int fd)
{
	int rc;
	struct pdu pdu;
	size_t len = sizeof(pdu);

	log_debug("IRIS DEBUG: reading from fd %d", fd);
	for (;;) {
		memset(&pdu, 0, sizeof(pdu));
		len = sizeof(pdu);

		rc = pdu_read(fd, (char*)&pdu, &len);
		log_debug("IRIS DEBUG: pdu_read(%d) returned %d, read %d bytes", fd, rc, len);

		if (rc < 0) {
			if (errno == EAGAIN) break;
			log_info("IRIS: failed to read from fd %d: %s", fd, strerror(errno));
			return -1;
		}

		if (len == 0) {
			log_info("IRIS: read 0 bytes from fd %d", fd);
			return -1;
		}

		if (pdu_unpack(&pdu) != 0) {
			log_info("IRIS: discarding bogus packet from fd %d", fd);
			return -1;
		}

		log_info("IRIS: SERVICE RESULT v%d [%d] %s/%s (rc:%d) '%s'",
			pdu.version, (uint32_t)pdu.ts, pdu.host, pdu.service, pdu.rc, pdu.output);

		check_result *res = malloc(sizeof(check_result));
		init_check_result(res);

		res->output_file    = NULL;
		res->output_file_fd = -1;

		res->host_name = strdup(pdu.host);
		if (strcmp(pdu.service, "HOST") != 0) {
		res->service_description = strdup(pdu.service);
			res->object_check_type = SERVICE_CHECK;
		}

		res->output = strdup(pdu.output);

		res->return_code = pdu.rc;
		res->exited_ok = 1;
		res->check_type = SERVICE_CHECK_PASSIVE;

		res->start_time.tv_sec = pdu.ts;
		res->start_time.tv_usec = 0;
		res->finish_time = res->start_time;

		add_check_result_to_list(res);
		// Icinga is now responsible for malloc'd _res_ memory

		log_debug("IRIS DEBUG: submitted result to main process");

		if (rc == 0) {
			log_debug("IRIS DEBUG: reached EOF on fd %d", fd);
			return -1;
		}
	}
	return 0;
}

static void* iris_daemon(void *udata)
{
	int i, n;
	int sockfd, epfd;
	struct epoll_event event;
	struct epoll_event events[IRIS_MAXFD];

	log_info("IRIS: starting up the iris daemon on *:%d", IRIS_DEFAULT_PORT);

#ifdef DEBUG_LIMITS
	struct rlimit lims;
	getrlimit(RLIMIT_NOFILE, &lims);
	lims.rlim_cur = 3072;
	lims.rlim_max = 4096;
	log_info("IRIS: running under LIMITS mode; setting no_file limits to %d/%d",
		lims.rlim_cur, lims.rlim_max);
	setrlimit(RLIMIT_NOFILE, &lims);
#endif

	// bind and listen on *:5667
	if ((sockfd = net_bind(NULL, IRIS_DEFAULT_PORT_STRING)) < 0) exit(2);

	// start up eopll
	if ((epfd = epoll_create(42)) < 0) {
		log_info("IRIS: epoll initialization failed: %s", strerror(errno));
		exit(2);
	}

	// register our listening socket
	event.data.fd = sockfd;
	event.events = EPOLLIN | EPOLLET;
	if (epoll_ctl(epfd, EPOLL_CTL_ADD, sockfd, &event) != 0) {
		log_info("IRIS: epoll_ctl(%d, EPOLL_CTL_ADD, %d, &event) failed: %s",
				epfd, sockfd, strerror(errno));
	}

	// and loop
	for (;;) {
		n = epoll_wait(epfd, events, sizeof(events), 1); // FIXME: experiment with timeout = -1
		if (n <= 0) continue;

		log_debug("IRIS DEBUG: epoll gave us %d fds to work with", n);

		for (i = 0; i < n; i++) {
#ifdef DEBUG
			log_debug("IRIS DEBUG: activity on %d: %04x =%s%s%s%s",
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

				close(events[i].data.fd);
				continue;
			}

			// CONNECT event
			if (events[i].data.fd == sockfd) {
				log_debug("IRIS DEBUG: processing inbound connections", sockfd);
				while (net_accept(sockfd, epfd) >= 0)
					;

			} else if ((events[i].events & EPOLLIN) ||
			           (events[i].events & EPOLLRDHUP)) {
				if (iris_recv_data(events[i].data.fd) != 0) {
					log_debug("IRIS DEBUG: closing fd %d", events[i].data.fd);
					close(events[i].data.fd);
				}
			}
		}
	}

	close(sockfd);
	close(epfd);
	return NULL;
}

static int iris_hook(int event, void *data)
{
	if (event != NEBCALLBACK_PROCESS_DATA) return 0;

	nebstruct_process_data *proc = (nebstruct_process_data*)data;
	if (proc->type != NEBTYPE_PROCESS_EVENTLOOPSTART) return 0;

	pthread_create(&tid, 0, iris_daemon, data);
	return 0;
}

/*************************************************************/

int nebmodule_init(int flags, char *args, nebmodule *mod)
{
	int rc;
	IRIS_MODULE = mod;

	log_info("IRIS: v" VERSION " starting up");
	log_debug("IRIS DEBUG: flags=%d, args='%s'", flags, args);

	log_debug("IRIS DEBUG: registering callbacks");
	rc = neb_register_callback(NEBCALLBACK_PROCESS_DATA, IRIS_MODULE, 0, iris_hook);
	if (rc != 0) {
		log_info("IRIS: PROCESS_DATA event registration failed, error %i", rc);
		return 1;
	}

	log_debug("IRIS DEBUG: startup complete");
	return 0;
}

int nebmodule_deinit(int flags, int reason)
{
	log_info("IRIX: v" VERSION " shutting down");
	log_debug("IRIS DEBUG: flags=%d, reason=%d", flags, reason);

	log_debug("IRIS DEBUG: deregistering callbacks");
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, iris_hook);

	log_debug("IRIS DEBUG: shutdown complete");
	return 0;
}
