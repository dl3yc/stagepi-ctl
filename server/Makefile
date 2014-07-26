CC=gcc
CFLAGS= -Wall -ljack -ljackserver

default: jackserver

jackserver: jackserver.o
	$(CC) -o jackserver jackserver.c $(CFLAGS)

clean:
	rm *.o jackserver
