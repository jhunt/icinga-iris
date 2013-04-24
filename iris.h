#ifndef IRIS_H

#define VERSION "1.0.2"
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


// FIXME: default port needs to be 5668
#define IRIS_DEFAULT_PORT          5667
#define IRIS_DEFAULT_PORT_STRING  "5667"
#define IRIS_DEFAULT_TIMEOUT         10
#define IRIS_PROTOCOL_VERSION         1

/* Maximum number of file descriptors that a single epoll_wait
   will return.  This is *not* the max of pollable FDs. */
#define IRIS_EPOLL_MAXFD       64

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

typedef void (*evhandler)(struct pdu*);
typedef int (*fdhandler)(int,evhandler);

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

size_t pdu_read(int fd, char *buf);
size_t pdu_write(int fd, const char *buf);

int pdu_pack(struct pdu *pdu);
int pdu_unpack(struct pdu *pdu);

int net_bind(const char *host, const char *port);
int net_poller(int sockfd);
int net_accept(int sockfd, int epfd);

int net_connect(const char *host, unsigned short port);
int fd_sink(int fd);
int read_packets(FILE *io, struct pdu **packets, const char *delim);

void mainloop(int sockfd, int epfd, fdhandler fn, evhandler evfn);
int recv_data(int fd, evhandler fn);

#endif
