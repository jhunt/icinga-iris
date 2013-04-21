#ifndef IRIS_H

#define VERSION "1.8.7"

#include <sys/types.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <ctype.h>
#include <time.h>

#include <signal.h>
#include <errno.h>
#include <unistd.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define IRIS_DEFAULT_PORT          5668
#define IRIS_DEFAULT_PORT_STRING  "5668"
#define IRIS_DEFAULT_TIMEOUT         10
#define IRIS_PROTOCOL_VERSION         1

#define IRIS_PDU_HOST_LEN      64
#define IRIS_PDU_SERVICE_LEN  128
#define IRIS_PDU_OUTPUT_LEN  4096

#define IRIS_PDU_V1  1

struct pdu {
	uint32_t crc32;
	uint32_t ts;
	int16_t  version;
	int16_t  rc;
	char     host[IRIS_PDU_HOST_LEN];
	char     service[IRIS_PDU_SERVICE_LEN];
	char     output[IRIS_PDU_OUTPUT_LEN];
};

unsigned long CRC32[256];

void iris_init_crc32(void);
unsigned long iris_crc32(char *buf, int len);

#endif
