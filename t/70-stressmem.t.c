#include "tap.c"
#include <sys/resource.h>
#include <stdlib.h>
#include "../iris.h"

int main(int argc, char **argv)
{
	plan_no_plan();

	int pipefd[2];
	ok(pipe(pipefd) == 0, "[sanity] piped properly");

	if (fork() == 0) {
		close(pipefd[0]);
		const char *buf = "infinite\tloop\t0\tforever\n\x17";
		size_t len = strlen(buf);

		unsigned int i;
		for (i = 0; i < 0xfffffffe; i++) {
			if (write(pipefd[1], buf, len) <= 0)
				break;
		}
		close(pipefd[1]);
	}
	close(pipefd[1]);
	FILE *io = fdopen(pipefd[0], "r");

	struct pdu *packets;
	size_t n;

	struct rlimit lim;
	if (getrlimit(RLIMIT_AS, &lim) != 0) {
		fail("getrlimit failed: %s", strerror(errno));
		return exit_status();
	}

	lim.rlim_cur = 128*1024;
	lim.rlim_max = 256*1024;

	if (setrlimit(RLIMIT_AS, &lim) != 0) {
		fail("setrlimit failed: %s", strerror(errno));
		return exit_status();
	}

	errno = 0;
	n = read_packets(io, &packets, "\t");
	ok(n == -1, "read_packets failed");
	ok(errno == ENOMEM, "read_packets failed because of ENOMEM");


	freopen("/dev/null", "w", stderr);
	log_debug("%s: starting", __FILE__);

	return exit_status();
}
