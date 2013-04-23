#include "tap.c"
#include "../iris.h"

int main(int argc, char **argv)
{
	plan_tests(8);

	struct pdu pdu;
	time_t now;

	/* successful, well-formed packet */
	time(&now);
	memset(&pdu, 0, sizeof(pdu));

	strcpy(pdu.host,    "the-host-name");
	strcpy(pdu.service, "name-of-the-service");
	strcpy(pdu.output,  "CRITICAL - something went wrong... :/");
	pdu.ts      = htonl(now);
	pdu.version = htons(1);
	pdu.rc      = htons(2);
	pdu.crc32   = htonl(crc32((char*)&pdu, sizeof(pdu)));

	ok(pdu_unpack(&pdu) == 0, "unpacked ther PDU");
	ok(pdu.ts == now,    "pdu_unpack decoded the timestamp");
	ok(pdu.version == 1, "pdu_unpack deocded the version");
	ok(pdu.rc == 2,      "pdu_unpack decoded the return code");

	/* screw up the crc32 */
	diag("You should see a message about CRC mismatch...");
	pdu.ts      = htonl(now);
	pdu.version = htons(1);
	pdu.rc      = htons(2);
	pdu.crc32   = htonl(crc32("xyzzy", 5));
	ok(pdu_unpack(&pdu) != 0, "unpack fails, because of CRC mismatch");

	/* PDU from the far past (<30s ago) */
	pdu.ts      = htonl(now - 31);
	pdu.version = htons(1);
	pdu.rc      = htons(2);
	pdu.crc32   = htonl(crc32((char*)&pdu, sizeof(pdu)));
	diag("You should see a message about the PDU being too old");
	ok(pdu_unpack(&pdu) != 0, "unpack fails, due to packet age");

	/* PDU from the future! (>30s from now) */
	pdu.ts      = htonl(now + 31);
	pdu.version = htons(1);
	pdu.rc      = htons(2);
	pdu.crc32   = htonl(crc32((char*)&pdu, sizeof(pdu)));
	diag("You should see a message about the PDU being from the future");
	ok(pdu_unpack(&pdu) != 0, "unpack fails, due to packet futuriness");

	/* Incorrect PDU version (i.e., !1) */
	pdu.ts      = htonl(now);
	pdu.version = htons(2);
	pdu.rc      = htons(2);
	pdu.crc32   = htonl(crc32((char*)&pdu, sizeof(pdu)));
	diag("You should see a message about PDU version mismatch");
	ok(pdu_unpack(&pdu) != 0, "unpack fails, due to PDU version");

	return exit_status();
}
