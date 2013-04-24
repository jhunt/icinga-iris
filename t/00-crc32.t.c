#include "tap.c"
#include "../iris.h"

extern unsigned long CRC32[256];

int main(int argc, char **argv)
{
	plan_tests(4);

	// pick a random CRC slot and check it
	ok(CRC32[144] == 0, "random crc32 slot (144) is initially 0");
	crc32("test", 4);
	ok(CRC32[144] != 0, "post-init, crc32 slot (144) is no longer 0");

	// known good CRC32 calculations
	ok(crc32("throw that hammer!\n", 19) == 0x98f4ec08,
		"crc32('throw that hammer!\\n') == 98f4ec08");

	ok(crc32("0x1a.0x1a.0x1a.0x1a.0x1a.0x1a.0x1a.0x1a", 39) == 0xacc76b10,
		"crc32(0x1a x 8) == acc76b10");

	freopen("/dev/null", "w", stderr);
	log_debug("%s: starting", __FILE__);

	return exit_status();
}

