CC=gcc
CFLAGS= -Wall `pkg-config --cflags --libs sndfile jack` -pthread

default: audioplayer

audioplayer: audioplayer.o
	$(CC) -o audioplayer audioplayer.c $(CFLAGS)

clean:
	rm *.o audioplayer
