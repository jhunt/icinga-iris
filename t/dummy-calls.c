// make iris.o happy
void iris_call_submit_result(struct pdu *pdu) { }
int iris_call_recv_data(int fd) { return 0; }
int iris_call_register_fd(int fd) { return 0; }
