CFLAGS := -Wall -Iicinga -g -O
#CFLAGS += -DDEBUG

default: libiris.so send_iris
libiris.so: iris.lo broker.lo
	libtool --mode link gcc $(CFLAGS) -o libiris.la $+ -rpath /usr/lib -lm -lpthread
send_iris: iris.o send_iris.o

%.o: %.c %.h
	$(CC) $(CFLAGS) -c -o $@ $<
%.lo: %.c
	libtool --mode compile gcc $(CFLAGS) -c $<

clean:
	rm -f *.o *.so *.lo
	rm -f send_iris

benchmark:
	./perf/longhaul 20 16 30 | tee  perf/long.20.16.30.out
	./perf/longhaul 20 32 30 | tee  perf/long.20.32.30.out
	./perf/longhaul 20 64 30 | tee  perf/long.20.64.30.out
