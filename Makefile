CC=gcc
CFLAGS= -Wall `pkg-config --cflags --libs jack` -ljack

default: stagepi-ctl

stagepi-ctl: stagepi-ctl.o
	$(CC) -o stagepi-ctl stagepi-ctl.c $(CFLAGS)

clean:
	rm *.o stagepi-ctl
