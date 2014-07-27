CC=gcc
CFLAGS= -Wall `pkg-config --cflags --libs smf jack` -pthread

default: midiplayer

midiplayer: midiplayer.o
	$(CC) -o midiplayer midiplayer.c $(CFLAGS)

clean:
	rm *.o midiplayer
