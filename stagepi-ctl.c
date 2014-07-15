/* StagePi Controller
 *
 * Author: Sebastian Weiss <dl3yc@darc.de>
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <signal.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>

#include <linux/input.h>
#include <sys/select.h>

#include <jack/jack.h>
#include <jack/transport.h>

jack_client_t *client;
jack_nframes_t samplerate;
jack_nframes_t buffersize;
jack_nframes_t xrun_cnt;

#define KCLN  "\033[F\033[J"
#define KNRM  "\x1B[0m"
#define KRED  "\x1B[31m"
#define KGRN  "\x1B[32m"
#define KYEL  "\x1B[33m"
#define KMAG  "\x1B[35m"

void checkuser(int fd)
{
	int i;
	size_t rb;
	struct input_event ev[2];

	rb = read(fd,ev,sizeof(struct input_event)*2);

	if (rb < (int) sizeof(struct input_event)) {
		printf("evtest: short read");
		return;
	}

	for (i = 0; i < (int) (rb / sizeof(struct input_event)); i++) {
		if (ev[i].type == EV_KEY) {
			/* Start/Pause Button */
			if ((ev[i].code == 164) && (ev[i].value == 1)) {
				if (jack_transport_query(client, NULL) == JackTransportRolling)
					jack_transport_stop(client);
				else
					jack_transport_start(client);
			}
			/* Stop Button */
			if ((ev[i].code == 166) && (ev[i].value == 1)) {
				jack_transport_stop(client);
				jack_transport_locate(client, 0);
			}
			//printf("type %d code %d value %d\n", ev[i].type, ev[i].code, ev[i].value);
		}
	}
}

void showstate()
{
	jack_position_t current;
	jack_transport_state_t transport_state;

	transport_state = jack_transport_query(client, &current);

	printf(KCLN);
	printf(KCLN);

	switch(transport_state) {
		case(JackTransportRolling):	printf(KGRN "play \t" KNRM); break;
		case(JackTransportStopped):	printf(KRED "stop \t" KNRM); break;
		case(JackTransportLooping):	printf(KYEL "loop \t" KNRM); break;
		case(JackTransportStarting):	printf(KYEL "start \t" KNRM); break;
		case(JackTransportNetStarting):	printf(KYEL "start \t" KNRM); break;
	}

	int time_raw = current.frame / samplerate;
	int secs = time_raw % 60;
	int mins = time_raw / 60 % 60;
	int hours = time_raw / (60*60);
	printf("%02d:%02d:%02d\t", hours, mins, secs);
	printf("%dHz %d %3.02f%% %d\n", samplerate, buffersize, jack_cpu_load(client), xrun_cnt);
	printf("\n");
}

void jack_shutdown (void *arg)
{
	exit(1);
}

void signal_handler (int sig)
{
	jack_client_close(client);
	fprintf(stderr, "signal received, exiting ...\n");
	exit(0);
}

void xrun_cb (void *arg)
{
	xrun_cnt++;
	exit(0);
}

int main (int argc, char *argv[])
{
	int fd_input_event;

	/* try to become a client of the JACK server */
	client = jack_client_open("stagepi-ctl", JackNullOption, 0);
	if (client == NULL) {
		fprintf(stderr, "error: jack server not running\n");
		return 1;
	}

	samplerate = jack_get_sample_rate(client);
	buffersize = jack_get_buffer_size(client);

	signal (SIGQUIT, signal_handler);
	signal (SIGTERM, signal_handler);
	signal (SIGHUP, signal_handler);
	signal (SIGINT, signal_handler);

	jack_on_shutdown(client, jack_shutdown, 0);

	xrun_cnt = 0;
	jack_set_xrun_callback(client, (JackXRunCallback) xrun_cb, 0);

	/* tell the JACK server that we are ready to roll */
	if(jack_activate (client)) {
		fprintf (stderr, "error: cannot activate client");
		return 1;
	}

	if ((fd_input_event = open("/dev/input/event2", O_RDONLY | O_NONBLOCK)) < 0) {
		fprintf(stderr, "error: cannot open input event");
		return 1;
	}

	fcntl(fd_input_event, F_SETFL, fcntl(fd_input_event, F_GETFL) | O_NONBLOCK);

	while (1) {
		usleep(50000);
		checkuser(fd_input_event);
		showstate();
	}

	close(fd_input_event);
	jack_client_close(client);
	exit(0);
}
