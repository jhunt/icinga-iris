#include "iris.h"

unsigned long CRC32[256] = {0};

void log_info(const char *fmt, ...)
{
	va_list ap;
#ifdef IRIS_EVENT_BROKER
	char *buf;
	int n;

	va_start(ap, fmt);
	n = vasprintf(&buf, fmt, ap);
	va_end(ap);

	write_to_all_logs(buf, NSLOG_INFO_MESSAGE);
	free(buf);
#else

	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	fprintf(stderr, "\n");
	va_end(ap);
#endif
}

void strip(char *s)
{
	char *p;
	for (p = s+strlen(s)-1; p >= s && isspace(*p); *p-- = '\0')
		;
}

static void crc32_init(void)
{
	unsigned long crc;
	int i, j;
	if (CRC32[0]) return;

	for (i = 0; i < 256; i++) {
		crc = i;
		for (j = 8; j > 0; j--) {
			if (crc & 1) {
				crc = (crc >> 1) ^ 0xedb88320L;
			} else {
				crc >>= 1;
			}
		}
		CRC32[i] = crc;
	}
}

unsigned long crc32(char *buf, int len)
{
	register unsigned long crc;
	int c, i;

	crc32_init();
	crc = 0xFFFFFFFF;
	for (i = 0; i < len; i++) {
		c = (uint8_t)buf[i];
		crc = ((crc >> 8) & 0x00FFFFFF) ^ CRC32[(crc ^ c) & 0xFF];
	}

	return (crc ^ 0xFFFFFFFF);
}

int nonblocking(int fd)
{
	int flags = fcntl(fd, F_GETFL, 0);
	if (flags < 0) return -1;

	flags |= O_NONBLOCK;
	if (fcntl(fd, F_SETFL, flags) < 0) return -1;

	return 0;
}

int pdu_read(int fd, char *buf, size_t *len)
{
	ssize_t n;
	char *off = buf;

	errno = 0;
	while ((n = read(fd, off, *len)) > 0) {
		 off += n; *len -= n;
	}
	*len = off - buf;
	return n;
}

int pdu_unpack(struct pdu *pdu)
{
	uint32_t our_crc, their_crc;
	long age;
	time_t now;

	// check the CRC32
	their_crc = ntohl(pdu->crc32);
	pdu->crc32 = 0L;
	our_crc = crc32((char*)pdu, sizeof(struct pdu));
	if (our_crc != their_crc) {
		log_info("CRC mismatch (calculated %x != %x); discarding packet and closing connection",
				our_crc, their_crc);
		return -1;
	}

	pdu->version = ntohs(pdu->version);
	pdu->rc      = ntohs(pdu->rc);
	pdu->ts      = ntohl(pdu->ts);

	if (pdu->version != IRIS_PROTOCOL_VERSION) {
		log_info("IRIS: incorrect PDU version (got %d, wanted %d)", pdu->version, IRIS_PROTOCOL_VERSION);
		return -1;
	}

	// check packet age
	time(&now);
	if (pdu->ts > (uint32_t)(now)) {
		age = (long)(pdu->ts - now);
	} else {
		age = (long)(now - pdu->ts);
	}
	if (age > 30) {
		log_info("Packet age is %ds in the %s", age,
			(pdu->ts > (uint32_t)(now) ? "future" : "past"));
		return -1;
	}

	// looks good
	return 0;
}

int net_bind(const char *host, const char *port)
{
	int fd, rc;
	struct addrinfo hints, *res, *head;

	memset(&hints, 0, sizeof(hints));
	hints.ai_flags    = AI_PASSIVE;
	hints.ai_family   = AF_INET;
	hints.ai_socktype = SOCK_STREAM;

	rc = getaddrinfo(host, port, &hints, &res);
	if (rc != 0) {
		log_info("IRIS: getaddrinfo failed: %s", gai_strerror(rc));
		return -1;
	}

	head = res;
	do {
		fd = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
		if (fd < 0) continue;

		char on = 1;
		setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &on, sizeof(on));

		if (bind(fd, res->ai_addr, res->ai_addrlen) == 0) {
			// bound; stop trying addrinfo results!
			break;
		}

		close(fd);
	} while ((res = res->ai_next) != NULL);

	freeaddrinfo(head);

	if (nonblocking(fd) != 0) {
		log_info("IRIS: failed to set O_NONBLOCK on network socket");
		close(fd);
		return -1;
	}

	if (listen(fd, SOMAXCONN) < 0) {
		log_info("IRIS: failed to listen() on socket fd %d", fd);
		close(fd);
		return -1;
	}
	return fd;
}
