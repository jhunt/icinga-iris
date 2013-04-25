#include "tap.c"
#include "../iris.h"
#include "dummy-calls.c"

int main(int argc, char **argv)
{
	plan_no_plan();

	struct sockaddr_in client1, client2, client3;
	struct client *res;

	memset(&client1, 0, sizeof(struct sockaddr_in));
	ok(inet_pton(AF_INET, "10.15.16.17", &(client1.sin_addr)) == 1,
			"converted 10.15.16.17 for client1");
	memset(&client2, 0, sizeof(struct sockaddr_in));
	ok(inet_pton(AF_INET, "10.0.0.9", &(client2.sin_addr)) == 1,
			"converted 10.0.0.9 for client2");
	memset(&client3, 0, sizeof(struct sockaddr_in));
	ok(inet_pton(AF_INET, "192.168.7.207", &(client3.sin_addr)) == 1,
			"converted 192.168.7.207 for client3");

	ok(client_init(3) == 3, "allocated enough space for 3 client objects");
	ok(!client_find(4), "no client at fd 4");
	ok(!client_addr(4), "no client at fd 4");


	client_new(4, NULL);
	ok(res = client_find(4), "found client for fd 4");
	ok(res && res->fd == 4, "client structure has correct fd");
	ok(strcmp(client_addr(4), "<unknown-peer>") == 0,
			"NULL ip has no known peer address");

	client_close(4);
	ok(!client_find(4), "no client at fd 4");
	ok(!client_addr(4), "no client at fd 4");


	ok(client_new(4, &(client1.sin_addr)), "registered client1 with fd 4");
	ok(client_new(5, &(client2.sin_addr)), "registered client2 with fd 5");
	ok(client_new(6, &(client3.sin_addr)), "registered client3 with fd 6");

	ok(client_find(6), "found client for fd 4");
	ok(strcmp(client_addr(6), "192.168.7.207") == 0,
			"Correct peer address for client3");

	ok(!client_new(89, &(client1.sin_addr)), "cannot register new client; at capacity");
	client_close(141); pass("client_close(<bad fd>) did not segfault");
	client_close(5);
	ok(client_new(89, &(client1.sin_addr)), "resuse of slots in the middle works");
	ok(strcmp(client_addr(89), "10.15.16.17") == 0,
			"Correct peer address for client2 / fd 89");

	return exit_status();
}
