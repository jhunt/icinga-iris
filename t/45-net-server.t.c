#include "tap.c"
#include "../iris.h"

#define NET_HOST "127.0.0.1"
#define NET_PORT "12356"

int children = 0;
void iris_call_submit_result(struct pdu *pdu) { }
int iris_call_recv_data(int fd)
{
	pass("saw fd %d (%d children still active)", fd, --children);
	fd_sink(fd);

	if (children <= 0) return -2; // force loop exit
	return -1; // force a close
}

int child_main(int rc)
{
	struct pdu pdu;
	time_t now;
	int fd;

	fd = net_connect(NET_HOST, atoi(NET_PORT));
	if (fd < 0) return 1;

	strcpy(pdu.host,    "host");
	strcpy(pdu.service, "service");
	strcpy(pdu.output,  "output");
	pdu.rc = rc;
	time(&now); pdu.ts = (uint32_t)now;
	if (pdu_pack(&pdu) != 0) return 2;

	if (pdu_write(fd, (uint8_t*)&pdu) < sizeof(pdu)) return 3;
	shutdown(fd, SHUT_WR);
	close(fd);
	return 0;
}

int main(int argc, char **argv)
{
	plan_no_plan();
	int sockfd, epfd;

	freopen("/dev/null", "w", stderr);
	vdebug("%s: starting", __FILE__);

	ok(net_poller(-1) < 0, "net_poller startup fails with bad sockfd");

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

	mainloop(sockfd, epfd);
	pass("all done");

	return exit_status();
}
