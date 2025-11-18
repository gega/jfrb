#

test:	test.c jfrb.h
	gcc -Wall -o test test.c

clean:
	rm -f test *.o
