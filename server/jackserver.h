#ifndef JACKSERVER_H
#define JACKSERVER_H

struct jackserver {
	char *name;
	jackctl_server_t *server;
	char *audiodriver;
	char *audiodevice;
	char *mididriver;
	int samplerate;
	char *playback;
	char *capture;
	int ins;
	int outs;
	int duplex;
	int realtime;
	int realtime_prio;
};

#endif /* JACKSERVER_H */
