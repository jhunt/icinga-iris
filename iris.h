#ifndef IRIS_H

#define VERSION "1.1.0"
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

#include <sys/epoll.h>

/* Define EPOLLRDHUP ourselves, if the kernel didn't do it already.
   see http://sourceware.org/bugzilla/show_bug.cgi?id=5040 */
#ifndef EPOLLRDHUP
#  define EPOLLRDHUP 0x2000
#endif


#define IRIS_DEFAULT_PORT         "5668"
#define IRIS_DEFAULT_TIMEOUT         10
#define IRIS_PROTOCOL_VERSION         1

/* Maximum number of file descriptors that a single epoll_wait
   will return.  This is *not* the max of pollable FDs. */
#define IRIS_EPOLL_MAXFD       64

#define IRIS_PDU_HOST_LEN      64
#define IRIS_PDU_SERVICE_LEN  128
#define IRIS_PDU_OUTPUT_LEN  4096

#define IRIS_PDU_V1  1

#define IRIS_MAX_CLIENTS 16 * 1024

// these match the NSLOG_* constants in icinga/logging.h
#define LOG_ERROR     1
#define LOG_WARN      2
#define LOG_PROC      64
#define LOG_RESULT    131072
#define LOG_INFO      262144

struct pdu {
	uint32_t crc32;
	uint32_t ts;
	uint16_t version;
	uint16_t rc;
	char     host[IRIS_PDU_HOST_LEN];
	char     service[IRIS_PDU_SERVICE_LEN];
	char     output[IRIS_PDU_OUTPUT_LEN];
};

struct client {
	int        fd;
	int        offset;
	char       addr[INET_ADDRSTRLEN];
	struct pdu pdu;
	size_t     bytes;
};

#ifdef DEBUG
#define vdebug(...) vlog(LOG_INFO, __VA_ARGS__)
#define vdump(pdu)  pdu_dump(pdu)
#else
#define vdebug(...)
#define vdump(pdu)
#endif

void strip(char *s);
unsigned long crc32(void *buf, int len);
int nonblocking(int fd);

ssize_t pdu_read(int fd, uint8_t *buf, size_t start);
ssize_t pdu_write(int fd, const uint8_t *buf);
void pdu_dump(const struct pdu *pdu);

int pdu_pack(struct pdu *pdu);
int pdu_unpack(struct pdu *pdu);

int net_bind(const char *host, const char *port);
int net_poller(int sockfd);
int net_accept(int sockfd, int epfd);

int net_connect(const char *host, unsigned short port);
int fd_sink(int fd);
int read_packets(FILE *io, struct pdu **packets, const char *delim);

void mainloop(int sockfd, int epfd);
int recv_data(int fd);

int client_init(int n);
struct client *client_find(int fd);
struct client *client_new(int fd, void *ip);
void client_close(int fd);
const char *client_addr(int fd);

#endif
