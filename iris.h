#ifndef IRIS_H

#define VERSION "1.0.1"
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include <ctype.h>
#include <time.h>

#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <netdb.h>
#include <netinet/in.h>
#include <arpa/inet.h>


#define IRIS_DEFAULT_PORT          5667
#define IRIS_DEFAULT_PORT_STRING  "5667"
#define IRIS_DEFAULT_TIMEOUT         10
#define IRIS_PROTOCOL_VERSION         1

#define IRIS_PDU_HOST_LEN      64
#define IRIS_PDU_SERVICE_LEN  128
#define IRIS_PDU_OUTPUT_LEN  4096

#define IRIS_PDU_V1  1

struct pdu {
	uint32_t crc32;
	uint32_t ts;
	uint16_t version;
	uint16_t rc;
	char     host[IRIS_PDU_HOST_LEN];
	char     service[IRIS_PDU_SERVICE_LEN];
	char     output[IRIS_PDU_OUTPUT_LEN];
};

void log_info(const char *fmt, ...);
#ifdef DEBUG
#define log_debug log_info
#else
static void log_debug(const char *fmt, ...) { }
#endif

void strip(char *s);
void init_crc32(void);
unsigned long crc32(char *buf, int len);
int nonblocking(int fd);

int pdu_read(int fd, char *buf, size_t *len);
int pdu_unpack(struct pdu *pdu);

#endif
