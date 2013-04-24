#include "tap.c"
#include "../iris.h"
#include "dummy-calls.c"

int main(int argc, char **argv)
{
	plan_no_plan();

	ok(crc32("throw that hammer!\n", 19) == 0x98f4ec08,
		"crc32('throw that hammer!\\n') == 98f4ec08");

	ok(crc32("0x1a.0x1a.0x1a.0x1a.0x1a.0x1a.0x1a.0x1a", 39) == 0xacc76b10,
		"crc32(0x1a x 8) == acc76b10");

	freopen("/dev/null", "w", stderr);
	log_debug("%s: starting", __FILE__);

	return exit_status();
}

