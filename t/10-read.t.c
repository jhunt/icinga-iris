#include "tap.c"
#include "../iris.h"

int main(int argc, char **argv)
{
	plan_tests(23);

	int rc, pipefd[2];
	size_t len;
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
	len = 4300;
	rc = pdu_read(pipefd[0], buf, &len);

	ok(rc  == 4300, "rc (%i) == 4300 after call to read exactly 4300 bytes", rc);
	ok(len == 4300, "value-result `len' param is size of packet buffer");
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
	len = 4300;
	rc = pdu_read(pipefd[0], buf, &len);

	ok(rc == 4300, "rc (%i) == 4300 after call to read exactly 4300 bytes (1 of 2)", rc);
	ok(len == 4300, "value-result `len' param is size of packet buffer");
	ok(strcmp(buf, "packet the first!") == 0, "read correct packet (1 of 2)");

	len = 4300;
	rc = pdu_read(pipefd[0], buf, &len);

	ok(rc == 4300, "rc (%i) == 4300 after call to read exactly 4300 bytes (2 of 2)", rc);
	ok(len == 4300, "value-result `len' param is size of packet buffer");
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
	len = 4300;
	rc = pdu_read(pipefd[0], buf, &len);

	ok(rc == 4, "rc (%i) == 4 after call to read exactly 4300 bytes (short write)", rc);
	ok(len == 4, "value-result `len' param %lu should == 4", len);
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
	len = 4300;
	rc = pdu_read(pipefd[0], buf, &len);

	ok(rc == 0, "rc (%i) == 0 after call to read exactly 4300 bytes (no write)", rc);
	ok(len == 0, "value-result `len' param %lu should == 0 (no write)", len);
	ok(!buf[0], "buf remains empty on no write");
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
	size_t total = 0;
	for (i = 0; i < 100; i++) {
		len = 4300;
		rc = pdu_read(pipefd[0], buf, &len);
		total += len;
	}
	ok(total == 4300 * 100, "read 100 packets worth of data  (%i == %i)", 4300 * 100, total);
	len = 4300;
	rc = pdu_read(pipefd[0], buf, &len);

	ok(rc == 0, "rc (%i) == 0 after call to read exactly 4300 bytes (end of write loop)", rc);
	ok(len == 0, "value-result `len' param %lu should == 0 (end of write loop)", len);
	close(pipefd[0]);

	return exit_status();
}
