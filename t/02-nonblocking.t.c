#include "tap.c"
#include "../iris.h"
#include "dummy-calls.c"

int main(int argc, char **argv)
{
	plan_tests(8);

	errno = 0;
	ok(nonblocking(-1) != 0, "Can't set O_NONBLOCK on a non-fd");
	ok(errno == EBADF, "non-fd arg yields EBADF errno");

	int pipefd[2];
	ok(pipe(pipefd) == 0, "[sanity] pipe() call succeeds");

	int flags;
	flags = fcntl(pipefd[0], F_GETFL, 0);
	ok(flags >= 0, "fcntl(<new pipe>) succeeds");
	ok((flags & O_NONBLOCK) != O_NONBLOCK, "O_NONBLOCK is not set by default");

	ok(nonblocking(pipefd[0]) == 0, "set nonblocking on pipefd[0]");
	flags = fcntl(pipefd[0], F_GETFL, 0);
	ok(flags >= 0, "fcntl(<pipe>) succeeds");
	ok((flags & O_NONBLOCK) == O_NONBLOCK, "O_NONBLOCK is set after call to nonblocking");

	freopen("/dev/null", "w", stderr);
	vdebug("%s: starting", __FILE__);

	return exit_status();
}

