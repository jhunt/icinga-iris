#include "tap.c"
#include "../iris.h"

#define NET_HOST "127.0.0.1"
#define NET_PORT "12356"

int child_main(int rc)
{
	int fd = net_connect(NET_HOST, atoi(NET_PORT));
	if (fd < 0) return 1;

	struct pdu pdu;
	strcpy(pdu.host,    "host");
	strcpy(pdu.service, "service");
	strcpy(pdu.output,  "output");
	pdu.rc = rc;
	time((time_t*)&pdu.ts);
	if (pdu_pack(&pdu) != 0) return 2;

	size_t len = sizeof(pdu);
	size_t n = pdu_send(fd, &pdu, &len);
	if (len < sizeof(pdu)) return 3;
	shutdown(fd, SHUT_WR);
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	plan_no_plan();
	int sockfd, epfd;
	int n, children;
	struct epoll_event events[8];

	ok(net_poller(-1) < 0, "net_poller startup fails with bad sockfd");
	freopen("/dev/null", ">", stderr);

	sockfd = net_bind(NET_HOST, NET_PORT);
	if (sockfd < 0) {
		fail("Failed to net_bind to %s:%s; is something on that port?",
				NET_HOST, NET_PORT);
		return exit_status();
	}
	pass("bound to %s:%s", NET_HOST, NET_PORT);

	epfd = net_poller(sockfd);
	ok(epfd >= 0, "net_poller succeeds with bound and listening sockfd");

	// fork a few times, spinning off client workers that will
	// connect back to us with distinct results
	if (fork() == 0) _exit(child_main(0));
	if (fork() == 0) _exit(child_main(1));
	if (fork() == 0) _exit(child_main(2));
	if (fork() == 0) _exit(child_main(3));
	children = 4;

	for (;;) {
		n = epoll_wait(epfd, events, sizeof(events), -1);
		ok(n > 0, "got more than 1 event from epoll_wait");

		int i;
		for (i = 0; i < n; i++) {
			if ((events[i].events & EPOLLERR) ||
			    (events[i].events & EPOLLHUP) ||
			    (events[i].events & EPOLLRDHUP) ||
			    (!(events[i].events & EPOLLIN))) {

				close(events[i].data.fd);
				children--;

			} else if (events[i].data.fd == sockfd) {
				while (net_accept(sockfd, epfd) >= 0)
					;

			} else if (events[i].events & EPOLLIN) {
				fd_sink(events[i].data.fd);

				// unceremoniously close the fd
				close(events[i].data.fd);
				children--;
			}
		}

		// anybody left?
		if (!children) break;
	}

	close(sockfd);
	pass("all done");

	return exit_status();
}
