#include "tap.c"
#include "../iris.h"
#include "dummy-calls.c"

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
	plan_no_plan();
	freopen("/dev/null", "w", stderr);

	ok(server_init(NULL) != 0, "server_init(NULL) fails");

	struct server s;
	ok(server_init(&s) == 0, "initialized struct server");
	string_is(s.port,            "5668",   "default port");
	       ok(s.max_clients   == 16384,    "default max_clients");
	string_is(s.syslog_ident,    "iris",   "default syslog_ident");
	string_is(s.syslog_facility, "daemon", "default syslog_facility");

	char* files[] = {
		"t/conf/overrides.conf",
		"t/conf/fancy.conf",
		"t/conf/unknown-directive.conf",
		NULL
	};
	char **f;
	for (f = files; *f; f++) {
		server_init(&s);
		ok(parse_config_file(*f, &s) == 0, *f);

		string_is(s.port,            "1234",   "overridden port");
			   ok(s.max_clients   == 1024,     "overridden max_clients");
		string_is(s.syslog_ident,    "mon",    "overridden syslog_ident");
		string_is(s.syslog_facility, "local3", "overridden syslog_facility");
	}

	char *fail[] = {
		"t/conf/fail/01-leading-special.conf",
		"t/conf/fail/02-trailing-equal-sign.conf",
		"t/conf/fail/03-bare-directive.conf",
		"t/conf/fail/05-non-numeric-max-clients.conf",
		NULL
	};
	for (f = fail; *f; f++) {
		ok(parse_config_file(*f, &s) != 0, *f);
	}

	return exit_status();
}
