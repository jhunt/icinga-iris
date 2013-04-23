#include "tap.c"
#include "../iris.h"

#define NET_HOST     "127.0.0.1"
#define NET_HOST_DNS "localhost"
#define NET_PORT     "12357"

int server_main(int expect)
{
	close(0); close(1); close(2);
	int sockfd, epfd;
	int n;
	struct epoll_event events[8];

	sockfd = net_bind(NET_HOST, NET_PORT);
	if (sockfd < 0) return 1;

	epfd = net_poller(sockfd);
	if (epfd < 0) return 2;

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
				expect--;

			} else if (events[i].data.fd == sockfd) {
				while (net_accept(sockfd, epfd) >= 0)
					;

			} else if (events[i].events & EPOLLIN) {
				fd_sink(events[i].data.fd);

				// unceremoniously close the fd
				close(events[i].data.fd);
				expect--;
			}
		}

		// anybody left?
		if (!expect) break;
	}
	close(sockfd);
	return 0;
}

int main(int argc, char **argv)
{
	plan_no_plan();

	// set up a server that will read 1 packet from us
	if (fork() == 0) _exit(server_main(1));

	int fd;
	struct pdu pdu;
	size_t n, len;

	strcpy(pdu.host,    "host");
	strcpy(pdu.service, "service");
	strcpy(pdu.output,  "output");
	pdu.rc = 0;
	time((time_t*)&pdu.ts);
	ok(pdu_pack(&pdu) == 0, "packed PDU");


	fd = net_connect(NET_HOST, atoi(NET_PORT));
	ok(fd >= 0, "connected to %s:%s", NET_HOST, NET_PORT);

	len = sizeof(pdu);
	n = pdu_send(fd, &pdu, &len);
	ok(len == sizeof(pdu), "sent PDU %d/%d bytes to the server", len, sizeof(pdu));
	ok(n == 0, "pdu_send returned 0");
	shutdown(fd, SHUT_WR);
	close(fd);


	fd = net_connect(NET_HOST_DNS, atoi(NET_PORT));
	ok(fd >= 0, "connected to %s:%s", NET_HOST_DNS, NET_PORT);

	len = sizeof(pdu);
	n = pdu_send(fd, &pdu, &len);
	ok(len == sizeof(pdu), "sent PDU %d/%d bytes to the server", len, sizeof(pdu));
	ok(n == 0, "pdu_send returned 0");
	shutdown(fd, SHUT_WR);
	close(fd);


	pass("all done");
	return exit_status();
}
