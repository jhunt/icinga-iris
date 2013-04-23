#include "tap.c"
#include "../iris.h"

int main(int argc, char **argv)
{
	plan_no_plan();
	freopen("/dev/null", ">", stderr);

	struct pdu pdu, copy;
	time_t now;

	/* successful, well-formed packet */
	time(&now);
	memset(&pdu, 0, sizeof(pdu));

	pdu.ts = now;
	pdu.rc = 2;
	strcpy(pdu.host,    "the-host-name");
	strcpy(pdu.service, "name-of-the-service");
	strcpy(pdu.output,  "CRITICAL - something went wrong... :/");

	ok(pdu_pack(&pdu) == 0, "packed the PDU");
	memcpy(&copy, &pdu, sizeof(copy));
	copy.crc32 = 0x0000;
	ok(pdu.crc32 == htonl(crc32((char*)&copy, sizeof(copy))), "CRC calculated");

	ok(pdu_unpack(&pdu) == 0, "unpacked the PDU");
	ok(pdu.ts == now,    "pdu_unpack decoded the timestamp");
	ok(pdu.version == 1, "pdu_unpack deocded the version");
	ok(pdu.rc == 2,      "pdu_unpack decoded the return code");

	/* screw up the crc32 */
	ok(pdu_pack(&pdu) == 0, "repacked the PDU");
	pdu.crc32 = htonl(crc32("xyzzy", 5));
	ok(pdu_unpack(&pdu) != 0, "unpack fails, because of CRC mismatch");

	/* PDU from the far past (<30s ago) */
	pdu.ts      = now - 31;
	ok(pdu_pack(&pdu) == 0, "repacked the PDU");
	ok(pdu_unpack(&pdu) != 0, "unpack fails, due to packet age");

	/* PDU from the future! (>30s from now) */
	pdu.ts      = now + 31;
	ok(pdu_pack(&pdu) == 0, "repacked the PDU");
	ok(pdu_unpack(&pdu) != 0, "unpack fails, due to packet futuriness");

	/* Incorrect PDU version (i.e., !1) */
	pdu.ts      = now;
	ok(pdu_pack(&pdu) == 0, "repacked the PDU");
	pdu.version = htons(2);
	pdu.crc32   = htonl(crc32((char*)&pdu, sizeof(pdu))); // since we meddled with version
	ok(pdu_unpack(&pdu) != 0, "unpack fails, due to PDU version");

	return exit_status();
}
