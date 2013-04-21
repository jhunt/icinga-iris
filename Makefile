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
