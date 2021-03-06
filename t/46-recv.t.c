#include "tap.c"
#include "../iris.h"

#define NET_HOST "127.0.0.1"
#define NET_PORT "12358"

int expect_packets = 0;
int num_packets    = 0;

void timedout(int sig) { _exit(5); }

int iris_call_recv_data(int fd) { return 0; }
void iris_call_submit_result(struct pdu *pdu)
{
	ok(pdu->rc == num_packets % 3, "Got packet with RC %d (expect %d)",
			pdu->rc, num_packets % 3);
	num_packets++;
}

int child_main(int fd)
{
	int i;
	size_t len;
	struct pdu pdu;
	time_t now;

	memset(&pdu, 0, sizeof(struct pdu));
	strcpy(pdu.host,    "a.host");
	strcpy(pdu.service, "b.service");
	strcpy(pdu.output,  "output");

	for (i = 0; i < expect_packets; i++) {
		pdu.rc = i % 3;
		time(&now); pdu.ts = (uint32_t)now;
		if (pdu_pack(&pdu) != 0) return 2;

		pdu_dump(&pdu);
		len = pdu_write(fd, (uint8_t*)&pdu);
		if (len < sizeof(pdu)) return 3;

		pdu.host[0]++;
		pdu.service[0]++;
	}

	return 0;
}

extern struct client *CLIENTS;

#define SETUP(fd,n) do { \
	ok(pipe(fd) == 0, "got a bi-directional pipe"); \
	ok(nonblocking((fd)[0]) == 0, "set O_NONBLOCK on read end of pipe"); \
	num_packets = 0; \
	expect_packets = (n); \
} while (0)

#define RECEIVE(pfd) do { \
	close((pfd)[1]); \
	client_new((pfd)[0], NULL); \
	\
	struct client *c; \
	c = client_find((pfd)[0]); \
	ok(c, "Found client struct for fd %d @%p", (pfd)[0], c); \
	\
	while (c->fd != -1) \
		recv_data((pfd)[0]); \
	ok(num_packets == expect_packets, "received %d packets (expected %d)", \
			num_packets, expect_packets); \
	client_close((pfd)[0]); \
	c = client_find((pfd)[0]); \
	ok(!c, "Removed client struct for fd %d (== %p)", (pfd)[0], c); \
} while (0)

int main(int argc, char **argv)
{
	int pipefd[2];
	client_init(8);

	plan_no_plan();
	vdebug("%s: starting", __FILE__);

	SETUP(pipefd, 1);
	if (fork() == 0) {
		close(pipefd[0]);
		_exit(child_main(pipefd[1]));
	}
	RECEIVE(pipefd);


	SETUP(pipefd, 1);
	if (fork() == 0) {
		close(pipefd[0]);
		_exit(child_main(pipefd[1]));
	}
	RECEIVE(pipefd);


	// now, test the edge cases

	// what if the client connects and then closes?
	SETUP(pipefd, 0);
	if (fork() == 0) {
		close(pipefd[0]);
		close(pipefd[1]); // don't even send anything
		_exit(6);
	}
	RECEIVE(pipefd);


	// what if the client wants to read an IV packet (under a timeout)?
	ok(pipe(pipefd) == 0, "got a bi-directional pipe");
	num_packets = 0;
	expect_packets = 0;
	if (fork() == 0) {
		char buf[8192];
		signal(SIGALRM, timedout);
		alarm(2);
		read(pipefd[0], buf, 8192);
		// should have to timeout
		// but just in case...
		_exit(33);
	}
	close(pipefd[1]);
	ok(nonblocking(pipefd[0]) == 0, "set O_NONBLOCK on read end of pipe");
	client_new(pipefd[0], NULL);
	recv_data(pipefd[0]);
	ok(num_packets == expect_packets, "received all %d packets (expected %d)",
			num_packets, expect_packets);
	close(pipefd[0]);


	// what if the client writes less than one packet?
	SETUP(pipefd, 0);
	if (fork() == 0) {
		close(pipefd[0]);
		write(pipefd[1], "OH HAI!", 7);
		_exit(3);
	}
	RECEIVE(pipefd);


	return exit_status();
}
