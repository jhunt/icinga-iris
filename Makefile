CFLAGS  := -Wall -Iicinga -O
LCOV    := lcov --directory . --base-directory .
GENHTML := genhtml --prefix $(shell dirname `pwd`)
# -e '' tempers prove's insistence that everything is Perl
PROVE   := prove -e ''

ifneq ($(DEVEL),)
	CFLAGS  += -fprofile-arcs -ftest-coverage
	CFLAGS  += -DDEBUG

	LDFLAGS += -fprofile-arcs -ftest-coverage -lgcov
endif


test_runners := $(subst .t.c,.t,$(shell ls -1 t/*.t.c))
no_lcov_c := test/*.t


all: libiris.so send_iris
libiris.so: iris.lo broker.lo
	libtool --mode link gcc $(CFLAGS) -o libiris.la $+ -rpath /usr/lib -lm -lpthread
send_iris: iris.o send_iris.o


test-runners: $(test_runners)
.PHONY: test-runners
test: test-runners cleancov
	$(PROVE)
.PHONY: test
vtest: test-runners
	$(PROVE) -v
.PHONY: vtest
coverage:
	$(LCOV) --capture -o $@.tmp
	$(LCOV) --remove $@.tmp $(no_lcov_c) > lcov.info
	rm -f $@.tmp
	rm -rf coverage
	$(GENHTML) -o coverage lcov.info
.PHONY: coverage

clean: cleancov
	rm -f *.o *.so *.lo
	rm -f t/*.o t/*.t
	rm -f send_iris
.PHONY: clean
cleancov:
	find . -name '*.gcda' -o -name '*.gcno' 2>/dev/null | xargs rm -f
.PHONY: cleancov

benchmark:
	./perf/longhaul 20 16 30 | tee  perf/long.20.16.30.out
	./perf/longhaul 20 32 30 | tee  perf/long.20.32.30.out
	./perf/longhaul 20 64 30 | tee  perf/long.20.64.30.out

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.lo: %.c
	libtool --mode compile gcc $(CFLAGS) -c $<
t/00-crc32.t: t/00-crc32.t.o iris.o
t/01-text.t: t/01-text.t.o iris.o
t/02-nonblocking.t: t/02-nonblocking.t.o iris.o
t/03-pdu.t: t/03-pdu.t.o iris.o
t/10-read.t: t/10-read.t.o iris.o
t/15-bind.t: t/15-bind.t.o iris.o
t/20-cli.t: t/20-cli.t.o iris.o
t/45-net-server.t: t/45-net-server.t.o iris.o
t/45-net-client.t: t/45-net-client.t.o iris.o
t/70-stressmem.t: t/70-stressmem.t.o iris.o
