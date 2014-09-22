#define IRIS_EVENT_BROKER
#include "iris.h"

#include <pthread.h>
#include <syslog.h>

#define NSCORE
#include "nebmodules.h"
#include "nebcallbacks.h"

#include "nebstructs.h"
#include "broker.h"

#include "config.h"
#include "common.h"
#include "icinga.h"

#define WHIMP_OUT_ON_RELOAD

NEB_API_VERSION(CURRENT_NEB_API_VERSION);

/*************************************************************/

static int _facility(const char *name)
{
	return strcmp(name, "local0") == 0 ? LOG_LOCAL0
	     : strcmp(name, "local1") == 0 ? LOG_LOCAL1
	     : strcmp(name, "local2") == 0 ? LOG_LOCAL2
	     : strcmp(name, "local3") == 0 ? LOG_LOCAL3
	     : strcmp(name, "local4") == 0 ? LOG_LOCAL4
	     : strcmp(name, "local5") == 0 ? LOG_LOCAL5
	     : strcmp(name, "local6") == 0 ? LOG_LOCAL6
	     : strcmp(name, "local7") == 0 ? LOG_LOCAL7
	                                   : LOG_DAEMON;
}

/*************************************************************/

static void *IRIS_MODULE = NULL;
pthread_t tid;
int sockfd, epfd;

/*************************************************************/

void iris_call_submit_result(struct pdu *pdu)
{
	check_result *res = malloc(sizeof(check_result));
	if (init_check_result(res) != OK) {
		syslog(LOG_ERROR, "Failed to initialize Icinga check_result object for submission of"
				" %s/%s %d '%s'", pdu->host, pdu->service, pdu->rc, pdu->output);
		return;
	}

	res->next           = NULL;
	res->output_file    = NULL;
	res->output_file_fd = -1;

	res->host_name = strndup(pdu->host, IRIS_PDU_HOST_LEN-1);
	if (strcmp(pdu->service, "HOST") != 0) {
		res->service_description = strndup(pdu->service, IRIS_PDU_SERVICE_LEN-1);
		res->object_check_type = SERVICE_CHECK;
	}

	res->output = strndup(pdu->output, IRIS_PDU_OUTPUT_LEN-1);

	res->return_code = pdu->rc;
	res->exited_ok = 1;
	res->check_type = SERVICE_CHECK_PASSIVE;

	res->start_time.tv_sec  = res->finish_time.tv_sec  = pdu->ts;
	res->start_time.tv_usec = res->finish_time.tv_usec = 0;

	// Icinga is now responsible for malloc'd _res_ memory
	add_check_result_to_list(res);
	vdebug("submitted result to main process");
}

int iris_call_recv_data(int fd)
{
	return recv_data(fd);
}

int iris_call_register_fd(int fd)
{
	return register_fd(fd);
}

void* iris_daemon(void *udata)
{
	struct server s;
	server_init(&s);
	openlog(s.syslog_ident, LOG_PID|LOG_CONS, _facility(s.syslog_facility));

	syslog(LOG_PROC, "v" VERSION " starting up");

	struct stat st;
	if (stat(IRIS_DEFAULT_CONFIG_FILE, &st) != 0) {
		syslog(LOG_INFO, "Skipping configuration, %s not found", IRIS_DEFAULT_CONFIG_FILE);

	} else {
		syslog(LOG_PROC, "reading configuration from %s", IRIS_DEFAULT_CONFIG_FILE);
		if (parse_config_file(IRIS_DEFAULT_CONFIG_FILE, &s) != 0) {
			syslog(LOG_EMERG, "failed to read configuration file: %s", strerror(errno));
			exit(2);
		}
	}

	closelog(); /* reopen syslog handle, with our new settings */
	openlog(s.syslog_ident, LOG_PID|LOG_CONS, _facility(s.syslog_facility));
	syslog(LOG_PROC, "maximum concurrent clients is %d",
			client_init(s.max_clients));

	// bind and listen on our port, all interfaces
	syslog(LOG_PROC, "binding on *:%s", s.port);
	if ((sockfd = net_bind(NULL, s.port)) < 0) {
		syslog(LOG_ERROR, "Failed to bind to *:%s: %s", s.port, strerror(errno));
		exit(2);
	}

	// start up epoll
	if ((epfd = net_poller(sockfd)) < 0) {
		syslog(LOG_ERROR, "Initialization of IO/polling (via epoll) failed: %s", strerror(errno));
		exit(2);
	}

	// and loop
	mainloop(sockfd, epfd);
	return NULL;
}

int iris_hook(int event, void *data)
{
	if (event != NEBCALLBACK_PROCESS_DATA) return 0;

	nebstruct_process_data *proc = (nebstruct_process_data*)data;
	switch (proc->type) {
	case NEBTYPE_PROCESS_EVENTLOOPSTART:
		pthread_create(&tid, 0, iris_daemon, data);
		break;

	case NEBTYPE_PROCESS_EVENTLOOPEND:
		syslog(LOG_PROC, "v" VERSION " shutting down");

#ifdef WHIMP_OUT_ON_RELOAD
		syslog(LOG_PROC, "not properly shutting down - WHIMPING OUT ON RELOAD");
#else
		pthread_cancel(tid);
		pthread_join(tid, NULL);

		client_deinit();
		vdebug("closing sockfd %d and epfd %d", sockfd, epfd);
		close(sockfd); close(epfd);
#endif

		break;

	default:
		break;
	}

	return 0;
}

/*************************************************************/

int nebmodule_init(int flags, char *args, nebmodule *mod)
{
	int rc;
	IRIS_MODULE = mod;

	vdebug("init started");
	rc = neb_register_callback(NEBCALLBACK_PROCESS_DATA, IRIS_MODULE, 0, iris_hook);
	if (rc != 0) {
		syslog(LOG_ERROR, "PROCESS_DATA event registration failed, error %i", rc);
		return 1;
	}
	return 0;
}

int nebmodule_deinit(int flags, int reason)
{
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, iris_hook);
	vdebug("deinit complete");
	return 0;
}
