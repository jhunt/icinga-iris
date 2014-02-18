#include "iris.h"
#include <getopt.h>
// make iris.o happy
void iris_call_submit_result(struct pdu *pdu) { }
int iris_call_recv_data(int fd) { return 0; }

void usage(const char *prog)
{
	fprintf(stderr, "USAGE: %s -t /path/to/iris.conf\n", prog);
}

int main(int argc, char **argv)
{
	const char *short_opts = "h?t:";
	struct option long_opts[] = {
		{ "help",       no_argument, NULL, 'h' },
		{ "test", required_argument, NULL, 't' },
		{ 0, 0, 0, 0 }
	};

	int opt, idx = 0;
	char *conf = strdup("/etc/icinga/iris.conf");

	while ( (opt = getopt_long(argc, argv, short_opts, long_opts, &idx)) != -1 ) {
		switch (opt) {
			case 't':
				free(conf);
				conf = strdup(optarg);
				break;

			case 'h':
			case '?':
			default:
				usage(argv[0]);
				return 1;
				break;
		}
	}

	if (optind < argc) {
		fprintf(stderr, "Extra (ignored) arguments found.\n");
		usage(argv[0]);
		return 1;
	}

	struct server s;
	if (server_init(&s) != 0) {
		perror("Failed to initialize Iris defaults");
		return 2;
	}

	struct stat st;
	if (stat(conf, &st) != 0) {
		perror(conf);
		return 2;
	}

	if (parse_config_file(conf, &s) != 0) {
		fprintf(stderr, "%s: errors were encountered.\n", conf);
		return 4;
	}

	printf("port            = %s\n", s.port);
	printf("timeout         = %i\n", s.timeout);
	printf("max_clients     = %i\n", s.max_clients);
	printf("syslog_ident    = %s\n", s.syslog_ident);
	printf("syslog_facility = %s\n", s.syslog_facility);
	return 0;
}
