#include "tap.c"
#include "../iris.h"
#include "dummy-calls.c"

int main(int argc, char **argv)
{
	plan_tests(9);

	int rfd = open("/dev/zero", O_RDONLY);
	int wfd = open("/dev/null", O_WRONLY);
	FILE *rio = fopen("/dev/zero", "r");

	crc32(NULL, 0);
	pass("crc32(NULL, 0) did not segfault");

	crc32(NULL, 187);
	pass("crc32(NULL, 187) did not segfault");

	pdu_read(rfd, NULL, 0);
	pass("pdu_read(rfd, NULL, 0) did not segfault");

	pdu_read(rfd, NULL, 187);
	pass("pdu_read(rfd, NULL, 187) did not segfault");

	pdu_write(wfd, NULL);
	pass("pdu_write(2, NULL) did not segfault");

	pdu_dump(NULL);
	pass("pdu_dump(NULL) did not segfault");

	pdu_pack(NULL);
	pass("pdu_pack(NULL) did not segfault");

	pdu_unpack(NULL);
	pass("pdu_unpack(NULL) did not segfault");

	read_packets(rio, NULL, "\t");
	pass("read_packets(rio, NULL, \"\\t\") did not segfault");

	return exit_status();
}
