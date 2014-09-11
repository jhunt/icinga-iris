#include "tap.c"
#include "../iris.h"
#include "dummy-calls.c"
#include <unistd.h>

extern int MAX_LIFETIME;
int main(int argc, char **argv)
{
	plan_no_plan();

	struct client *c;
	MAX_LIFETIME = 2;

	ok(client_init(3) == 3, "allocated enough space for 3 client objects");
	ok(!client_find(3), "no client at fd 3");

	c = client_new(3, NULL);
	ok(client_find(3), "found client for fd 3");
	clients_purge();
	ok(client_find(3), "found client for fd 3 (after immediate purge)");

	diag("sleeping for 3 seconds to run out the deadline clock");
	sleep(3);

	ok(client_find(3), "found expired client (before purge)");
	clients_purge();
	ok(!client_find(3), "client 3 not found (after purge)");

	return exit_status();
}
