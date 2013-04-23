CFLAGS  := -Wall -Iicinga -O
LCOV    := lcov --directory . --base-directory .
GENHTML := genhtml --prefix $(shell dirname `pwd`)
# -e '' tempers prove's insistence that everything is Perl
PROVE   := prove -e ''

ifneq ($(DEVEL),)
	CFLAGS  += -fprofile-arcs -ftest-coverage

	LDFLAGS += -fprofile-arcs -ftest-coverage -lgcov
endif

ifneq ($(DEBUG),)
	CFLAGS += -DDEBUG
endif
ifneq ($(LIMITS),)
	CFLAGS += -DDEBUG_LIMITS
endif


no_lcov_c := test/*.t


all: libiris.so send_iris
libiris.so: iris.lo broker.lo
	libtool --mode link gcc $(CFLAGS) -o libiris.la $+ -rpath /usr/lib -lm -lpthread
send_iris: iris.o send_iris.o

test-runners: t/00-crc32.t
test: test-runners cleancov
	$(PROVE)
vtest: test-runners
	$(PROVE) -v
coverage:
	$(LCOV) --capture -o $@.tmp
	$(LCOV) --remove $@.tmp $(no_lcov_c) > lcov.info
	rm -f $@.tmp
	rm -rf coverage
	$(GENHTML) -o coverage lcov.info

clean: cleancov
	rm -f *.o *.so *.lo
	rm -f t/*.o t/*.t
	rm -f send_iris

cleancov:
	find . -name '*.gcda' 2>/dev/null | xargs rm -f

benchmark:
	./perf/longhaul 20 16 30 | tee  perf/long.20.16.30.out
	./perf/longhaul 20 32 30 | tee  perf/long.20.32.30.out
	./perf/longhaul 20 64 30 | tee  perf/long.20.64.30.out

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.lo: %.c
	libtool --mode compile gcc $(CFLAGS) -c $<
t/00-crc32.t: t/00-crc32.t.o iris.o
