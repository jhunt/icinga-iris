CFLAGS := -Wall -Iicinga -g -O
ifneq ($(DEBUG),)
	CFLAGS += -DDEBUG
endif
ifneq ($(LIMITS),)
	CFLAGS += -DDEBUG_LIMITS
endif

# -e '' tempers prove's insistence that everything is Perl
PROVE := prove -e ''

all: libiris.so send_iris
libiris.so: iris.lo broker.lo
	libtool --mode link gcc $(CFLAGS) -o libiris.la $+ -rpath /usr/lib -lm -lpthread
send_iris: iris.o send_iris.o

test-runners: t/00-crc32.t
test: test-runners
	$(PROVE)
vtest: test-runners
	$(PROVE) -v

clean:
	rm -f *.o *.so *.lo
	rm -f t/*.o t/*.t
	rm -f send_iris

benchmark:
	./perf/longhaul 20 16 30 | tee  perf/long.20.16.30.out
	./perf/longhaul 20 32 30 | tee  perf/long.20.32.30.out
	./perf/longhaul 20 64 30 | tee  perf/long.20.64.30.out

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.lo: %.c
	libtool --mode compile gcc $(CFLAGS) -c $<
