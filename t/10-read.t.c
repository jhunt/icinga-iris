#include "tap.c"
#include "../iris.h"
#include "dummy-calls.c"

int main(int argc, char **argv)
{
	plan_no_plan();

	int pipefd[2];
	ssize_t len;
	char buf[4300];

	/** send exactly one packet **/
	ok(pipe(pipefd) == 0, "[sanity] created pipe structure");
	memset(buf, 0, 4300);
	if (fork() == 0) {
		close(pipefd[0]);
		close(0); close(1); close(2);

		strcpy(buf, "a solitary packet");
		write(pipefd[1], buf, 4300);
		close(pipefd[1]);

		exit(0);
	}
	close(pipefd[1]);
	len = pdu_read(pipefd[0], (uint8_t*)buf, 0);

	ok(len  == 4300, "len (%i) == 4300 after call to read exactly 4300 bytes", len);
	ok(strcmp(buf, "a solitary packet") == 0, "read correct packet");
	close(pipefd[0]);

	/** send two packets **/
	ok(pipe(pipefd) == 0, "[sanity] created pipe structure");
	memset(buf, 0, 4300);
	if (fork() == 0) {
		close(pipefd[0]);
		close(0); close(1); close(2);

		strcpy(buf, "packet the first!");
		write(pipefd[1], buf, 4300);
		strcpy(buf, "packet the second!");
		write(pipefd[1], buf, 4300);
		close(pipefd[1]);

		exit(0);
	}
	close(pipefd[1]);
	len = pdu_read(pipefd[0], (uint8_t*)buf, 0);

	ok(len == 4300, "len (%i) == 4300 after call to read exactly 4300 bytes (1 of 2)", len);
	ok(strcmp(buf, "packet the first!") == 0, "read correct packet (1 of 2)");

	len = pdu_read(pipefd[0], (uint8_t*)buf, 0);

	ok(len == 4300, "len (%i) == 4300 after call to read exactly 4300 bytes (2 of 2)", len);
	ok(strcmp(buf, "packet the second!") == 0, "read correct packet (2 of 2)");
	close(pipefd[0]);

	/** short-stroke the write (<4300 bytes) **/
	ok(pipe(pipefd) == 0, "[sanity] created pipe structure");
	memset(buf, 0, 4300);
	if (fork() == 0) {
		close(pipefd[0]);
		close(0); close(1); close(2);

		strcpy(buf, "It's Monty Python's Flying Circus");
		write(pipefd[1], buf, 4);
		close(pipefd[1]);

		exit(0);
	}
	close(pipefd[1]);
	len = pdu_read(pipefd[0], (uint8_t*)buf, 0);

	ok(len == 4, "len (%i) == 4 after call to read exactly 4300 bytes (short write)", len);
	ok(strcmp(buf, "It's") == 0, "read '%s' == expected 'It\\'s' (first 4 bytes of truncated string)", buf);
	close(pipefd[0]);

	/** write NOTHING to the pipe **/
	ok(pipe(pipefd) == 0, "[sanity] created pipe structure");
	memset(buf, 0, 4300);
	if (fork() == 0) {
		close(pipefd[0]);
		close(0); close(1); close(2);
		// write nothing
		close(pipefd[1]);
		exit(0);
	}
	close(pipefd[1]);
	len = pdu_read(pipefd[0], (uint8_t*)buf, 0);

	ok(len == 0, "len (%i) == 0 after call to read exactly 4300 bytes (no write)", len);
	ok(!buf[0], "buf remains empty on no write");
	close(pipefd[0]);

	/** write 1 packet, 2800 bytes at a time */
	ok(pipe(pipefd) == 0, "[sanity] created pipe structure");
	ok(nonblocking(pipefd[0]) == 0, "set nonblocking on read end of pipe");
	memset(buf, 0, 4300);
	if (fork() == 0) {
		close(pipefd[0]);
		close(0); close(1); close(2);

		strcpy(buf+8, "starts at offset 8");
		strcpy(buf+450, "and this one is at 450");
		strcpy(buf+2600, "middle of the packet (or so)");

		write(pipefd[1], buf, 2800);
		sleep(1);
		write(pipefd[1], buf+2800, 4300-2800);
		close(pipefd[1]);
		exit(0);
	}
	close(pipefd[1]);
	len = pdu_read(pipefd[0], (uint8_t*)buf, 0);

	ok(len == 2800, "len (%i) == 2800 after call to read exactly 4300 bytes (no write)", len);
	close(pipefd[0]);


	/** write 100 packets in a loop **/
	ok(pipe(pipefd) == 0, "[sanity] created pipe structure");
	memset(buf, 0, 4300);
	if (fork() == 0) {
		close(pipefd[0]);
		close(0); close(1); close(2);

		int i;
		for (i = 0; i < 100; i++) {
			snprintf(buf, 4300, "packet #%i!", i+1);
			write(pipefd[1], buf, 4300);
		}

		close(pipefd[1]);
		exit(0);
	}
	close(pipefd[1]);
	int i;
	ssize_t total = 0;
	for (i = 0; i < 100; i++) {
		len = pdu_read(pipefd[0], (uint8_t*)buf, 0);
		total += len;
	}
	ok(total == 4300 * 100, "read 100 packets worth of data  (%i == %i)", 4300 * 100, total);
	len = pdu_read(pipefd[0], (uint8_t*)buf, 0);

	ok(len == 0, "len (%i) == 0 after call to read exactly 4300 bytes (end of write loop)", len);
	close(pipefd[0]);

	freopen("/dev/null", "w", stderr);
	vdebug("%s: starting", __FILE__);

	return exit_status();
}
