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

int main(int argc, char **argv)
{
	plan_tests(2);
	freopen("/dev/null", ">", stderr);

	char *got;

	got = strdup("\ta test   \t\n\n\r\n ");
	strip(got);
	string_is(got, "\ta test", "strip() drops whitespace at the end");

	got = strdup(" \t \t \t \t");
	strip(got);
	string_is(got, "", "strip(<all whitespace>) returns empty string");

	// this is here for coverage
	log_info("# XYZZY: %s is working!", "logging");

	return exit_status();
}
