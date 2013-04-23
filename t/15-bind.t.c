#include "tap.c"
#include "../iris.h"

int main(int argc, char **argv)
{
	plan_tests(3);

	int fd;
	fd = net_bind("0.0.0.0", "5669");
	ok(fd >= 0, "bound to 5669, all interfaces");

	int again;
	again = net_bind("0.0.0.0", "5669");
	ok(again < 0, "net_bind returns -1 on bind failure");

	close(fd);
	fd = net_bind("some.random.interface", "glort-port");
	ok(fd < 0, "net_bind fails if getaddrinfo fails");

	return exit_status();
}
