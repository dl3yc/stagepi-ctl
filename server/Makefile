CC=gcc
CFLAGS= -Wall -ljack -ljackserver

default: server-ctl

server-ctl: server-ctl.o
	$(CC) -o jackserver jackserver.c $(CFLAGS)

clean:
	rm *.o jackserver
