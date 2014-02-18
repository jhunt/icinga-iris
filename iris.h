#ifndef IRIS_H

#define VERSION "1.1.7"
#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/socket.h>

#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdarg.h>

#include <syslog.h>

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

#define IRIS_DEFAULT_CONFIG_FILE  "/etc/icinga/iris.conf"

#define IRIS_PROTOCOL_VERSION         1

/* Maximum number of file descriptors that a single epoll_wait
   will return.  This is *not* the max of pollable FDs. */
#define IRIS_EPOLL_MAXFD       64

#define IRIS_PDU_HOST_LEN      64
#define IRIS_PDU_SERVICE_LEN  128
#define IRIS_PDU_OUTPUT_LEN  4096

#define IRIS_PDU_V1  1

#define LOG_ERROR  LOG_ERR
#define LOG_WARN   LOG_WARNING
#define LOG_PROC   LOG_INFO
#define LOG_RESULT LOG_NOTICE

struct pdu {
	uint32_t crc32;
	uint32_t ts;
	uint16_t version;
	uint16_t rc;
	char     host[IRIS_PDU_HOST_LEN];
	char     service[IRIS_PDU_SERVICE_LEN];
	char     output[IRIS_PDU_OUTPUT_LEN];
};

struct server {
	char     *port;
	uint8_t   timeout;
	uint32_t  max_clients;

	char     *syslog_ident;
	char     *syslog_facility;
};

struct client {
	int        fd;
	int        offset;
	char       addr[INET_ADDRSTRLEN];
	struct pdu pdu;
	size_t     bytes;
};

#ifdef DEBUG
#define vdebug(...) syslog(LOG_DEBUG, __VA_ARGS__)
#define vdump(pdu)  pdu_dump(pdu)
#else
#define vdebug(...)
#define vdump(pdu)
#endif

void strip(char *s);
unsigned long crc32(void *buf, int len);
int nonblocking(int fd);

int server_init(struct server *s);
int parse_config_file(const char *file, struct server *s);
int parse_config(FILE *io, struct server *s);

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
void client_deinit(void);
struct client *client_find(int fd);
struct client *client_new(int fd, void *ip);
void client_close(int fd);
const char *client_addr(int fd);

#endif
