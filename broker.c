#define IRIS_EVENT_BROKER
#include "iris.h"

#include <pthread.h>

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

static void *IRIS_MODULE = NULL;
pthread_t tid;
int sockfd, epfd;

/*************************************************************/

void vlog(unsigned int level, const char *fmt, ...)
{
	if (!level) return;
	va_list ap; char *buf;
	va_start(ap, fmt);
	if (vasprintf(&buf, fmt, ap) > 0) {
		write_to_all_logs(buf, level);
		free(buf);
	}
	va_end(ap);
}

void iris_call_submit_result(struct pdu *pdu)
{
	check_result *res = malloc(sizeof(check_result));
	if (init_check_result(res) != OK) {
		vlog(LOG_ERROR, "Failed to initialize Icinga check_result object for submission of"
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
	vdebug("IRIS: submitted result to main process");
}

int iris_call_recv_data(int fd)
{
	return recv_data(fd);
}

void* iris_daemon(void *udata)
{
	vlog(LOG_PROC, "IRIS: starting up the iris daemon on *:%s", IRIS_DEFAULT_PORT);
	vlog(LOG_PROC, "IRIS: maximum concurrent clients is %d",
			client_init(IRIS_MAX_CLIENTS));

	// bind and listen on our port, all interfaces
	if ((sockfd = net_bind(NULL, IRIS_DEFAULT_PORT)) < 0) {
		vlog(LOG_ERROR, "IRIS: Failed to bind to *:%s: %s", IRIS_DEFAULT_PORT, strerror(errno));
		exit(2);
	}

	// start up epoll
	if ((epfd = net_poller(sockfd)) < 0) {
		vlog(LOG_ERROR, "IRIS: Initialization of IO/polling (via epoll) failed: %s", strerror(errno));
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
		vlog(LOG_PROC, "IRIS: v" VERSION " starting up");
		pthread_create(&tid, 0, iris_daemon, data);
		break;

	case NEBTYPE_PROCESS_EVENTLOOPEND:
		vlog(LOG_PROC, "IRIS: v" VERSION " shutting down");

#ifdef WHIMP_OUT_ON_RELOAD
		vlog(LOG_PROC, "IRIS: not properly shutting down - WHIMPING OUT ON RELOAD");
#else
		pthread_cancel(tid);
		pthread_join(tid, NULL);

		vdebug("IRIS: closing sockfd %d and epfd %d", sockfd, epfd);
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

	vdebug("IRIS: init started");
	rc = neb_register_callback(NEBCALLBACK_PROCESS_DATA, IRIS_MODULE, 0, iris_hook);
	if (rc != 0) {
		vlog(LOG_ERROR, "IRIS: PROCESS_DATA event registration failed, error %i", rc);
		return 1;
	}
	return 0;
}

int nebmodule_deinit(int flags, int reason)
{
	neb_deregister_callback(NEBCALLBACK_PROCESS_DATA, iris_hook);
	vdebug("IRIS: deinit complete");
	return 0;
}
