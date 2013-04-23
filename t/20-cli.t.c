#include "tap.c"
#include "../iris.h"

void _string_is(const char *got, const char *want,
		const char *fn, char *file, unsigned int line, char *test)
{
	if ((!got && !want) || (got && want && strcmp(got, want) == 0)) {
		pass("%s", test);
		return;
	}

	fail("%s", test);
	diag("    Failed test %s", test);
	diag("       got: '%s'", got);
	diag("  expected: '%s'", want);
}
#define string_is(got,want,test) _string_is((got),(want),__func__,__FILE__,__LINE__,(test))

#define TMP "t/tmp/packets"

int main(int argc, char **argv)
{
	//plan_tests(3);
	plan_no_plan();

	int rc = system("./t/util/gen-packets " TMP);
	if (rc != 0) diag("gen-packets exited abnormally.  Get ready for the FAIL!");
	ok(rc == 0, "generate source files for read_packets");

	int n;
	struct pdu *packets;
	FILE *io;

	io = fopen(TMP "/normal", "r");
	ok(io, "Opened %s/normal for reading", TMP);
	ok(!feof(io), "%s/normal is not at EOF", TMP);

	n = read_packets(io, &packets, "\t");
	ok(n == 3, "read %d packets (expect 3) from %s/normal", n, TMP);

	ok(packets[0].rc == 0, "pdu[0].rc");
	string_is(packets[0].host,    "host1",                          "pdu[0].host");
	string_is(packets[0].service, "cpu",                            "pdu[0].service");
	string_is(packets[0].output,  "OK - CPU is good",               "pdu[0].output");

	ok(packets[1].rc == 1, "pdu[1].rc");
	string_is(packets[1].host,    "host1",                          "pdu[1].host");
	string_is(packets[1].service, "disk",                           "pdu[1].service");
	string_is(packets[1].output,  "WARNING - disks are filling up", "pdu[1].output");

	ok(packets[2].rc == 2, "pdu[2].rc");
	string_is(packets[2].host,    "host1",                          "pdu[2].host");
	string_is(packets[2].service, "load",                           "pdu[2].service");
	string_is(packets[2].output,  "CRITICAL - load is too high",    "pdu[2].output");
	ok(feof(io), "%s/normal is at EOF", TMP);
	fclose(io);
	free(packets); packets = NULL;


	io = fopen(TMP "/garbage", "r");
	ok(io, "Opened %s/garbage for reading", TMP);
	ok(!feof(io), "%s/garbage is not at EOF", TMP);

	n = read_packets(io, &packets, "\t");
	ok(n == 0, "read %d packets (expect 0) from %s/garbage", n, TMP);
	ok(feof(io), "%s/garbage is at EOF", TMP);
	fclose(io);
	free(packets); packets = NULL;


	io = fopen(TMP "/extra", "r");
	ok(io, "Opened %s/extra for reading", TMP);
	ok(!feof(io), "%s/extra is not at EOF", TMP);

	n = read_packets(io, &packets, "\t");
	ok(n == 1, "read %d packets (expect 1) from %s/extra", n, TMP);

	ok(packets[0].rc == 0, "pdu[0].rc");
	string_is(packets[0].host,    "host",                   "pdu[0].host");
	string_is(packets[0].service, "service",                "pdu[0].service");
	string_is(packets[0].output,  "OK\tmessage with a tab", "pdu[0].output");
	ok(feof(io), "%s/extra is at EOF", TMP);
	fclose(io);
	free(packets); packets = NULL;


	io = fopen(TMP "/missing", "r");
	ok(io, "Opened %s/missing for reading", TMP);
	ok(!feof(io), "%s/missing is not at EOF", TMP);

	n = read_packets(io, &packets, "\t");
	ok(n == 0, "read %d packets (expect 0) from %s/missing", n, TMP);
	ok(feof(io), "%s/missing is at EOF", TMP);
	fclose(io);
	free(packets); packets = NULL;


	io = fopen(TMP "/jumbo", "r");
	ok(io, "Opened %s/jumbo for reading", TMP);
	ok(!feof(io), "%s/jumbo is not at EOF", TMP);

	n = read_packets(io, &packets, "\t");
	ok(n == 1, "read %d packets (expect 1) from %s/jumbo", n, TMP);

	int i;
	char buf[8192]; // bigger than necessary, cuz its easier

	memset(buf, 'x', 8192); buf[8191] = '\0';
	for (i = 0; i < 100; i += 9)
		memcpy(buf+i, "host.a41.", 9);
	buf[63] = '\0';
	string_is(packets[0].host, buf, "pdu host is capped at 64-1 characters");

	memset(buf, 'x', 8192); buf[8191] = '\0';
	for (i = 0; i < 200; i += 14)
		memcpy(buf+i, "service.v^v^v:", 14);
	buf[127] = '\0';
	string_is(packets[0].service, buf, "pdu service is capped at 128-1 characters");

	memset(buf, 'x', 8192); buf[8191] = '\0';
	for (i = 0; i < 6000; i += 43)
		memcpy(buf+i, "all work and no play make jack a dull boy. ", 43);
	buf[4095] = '\0';
	string_is(packets[0].output, buf, "pdu output is capped at 4096-1 characters");

	ok(feof(io), "%s/jumbo is at EOF", TMP);
	fclose(io);
	free(packets); packets = NULL;

	return exit_status();
}
