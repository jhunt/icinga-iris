default: libiris.so
libiris.so: iris.lo
	libtool gcc -DDEBUG -Iicinga -g -O -o libiris.la iris.lo -rpath /usr/lib -lm -lpthread
iris.lo: iris.c
	libtool gcc -DDEBUG -Iicinga -g -O -c iris.c

clean:
	rm -f *.o *.so *.lo
