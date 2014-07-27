#include <pthread.h>
#include <smf.h>
#include <jack/jack.h>

struct midiplayer {
	/* user data */
	char *name;
	char *servername;
	char *midiport;

	/* internal data */
	jack_client_t *client;
	jack_port_t *port;
	smf_t *smf;
	volatile int handle_reposition;
	volatile int reposition_handled;
	volatile jack_nframes_t frame;
	pthread_t thread_id;
};

static int midiplayer_process(jack_nframes_t nframes, void *arg)
{
	struct midiplayer *player = (struct midiplayer *) arg;
	return 0;
}

static int midiplayer_sync(jack_transport_state_t state, jack_position_t *pos, void *arg)
{
	struct midiplayer *player = (struct midiplayer *) arg;

	if (player->handle_reposition) {
		if (player->reposition_handled) {
			player->reposition_handled = 0;
			player->handle_reposition = 0;
			return 1;
		}
		return 0;
	}
	player->handle_reposition = 1;
	player->frame = pos->frame;
	return 0;
}

static void *midiplayer_source(void *arg)
{
	struct midiplayer *player = (struct midiplayer *) arg;

	if (player->handle_reposition) {
		player->reposition_handled = 1;
	}

	return 0;
}

int midiplayer_load(struct midiplayer *player, char *midifile)
{
	player->smf = smf_load(midifile);
	return 0;
}

int midiplayer_init(struct midiplayer *player)
{
	jack_status_t status;
	jack_options_t options = JackNullOption | JackServerName | JackNoStartServer;
	int ret;

	ret = pthread_create(&player->thread_id, NULL, midiplayer_source, player);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't create source thread\n");
	}

	pthread_detach(player->thread_id);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't detach source thread\n");
	}

	player->client = jack_client_open(player->name, options, &status, player->servername);
	if (player->client == NULL) {
		fprintf(stderr, "midiplayer: can't open jack client\n");
		if (status & JackServerFailed) {
			fprintf(stderr, "midiplayer: unable to connect to JACK server\n");
		}
		return -1;
	}

	if (status & JackNameNotUnique) {
		fprintf(stderr, "midiplayer: unique name %s assigned\n", jack_get_client_name(player->client));
	}

	jack_set_process_callback(player->client, midiplayer_process, player);
	jack_set_sync_callback(player->client, midiplayer_sync, player);

	player->port = jack_port_register(player->client, "midiout", JACK_DEFAULT_MIDI_TYPE, JackPortIsOutput, 0);
	if (player->port == NULL) {
		fprintf(stderr, "midiplayer: can't register jack port\n");
		/* TODO: close jack client */
		return -1;
	}

	ret = jack_activate(player->client);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't activate jack\n");
		/* TODO: close jack client */
		return -1;
	}

	ret = jack_connect(player->client, jack_port_name(player->port), player->midiport);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't connect port\n");
		/* TODO: close jack client */
		return -1;
	}

	return 0;
}

int midiplayer_diskthread(smf_t *smf)
{

	return 0;
}

int main(int argc, char *argv[])
{
	int samplerate = 96000;
	int sample = 47000;
	smf_t *smf;
	smf_event_t *event;
	struct midiplayer player = {
		.name = "midiplayer",
		.servername = "stagepi",
		.midiport = "hw1",
	};

	if (argc != 2) {
		printf("usage: %s midifile.mid\n", argv[0]);
		return 1;
	}

	smf = smf_load(argv[1]);
	if (smf == NULL) {
		printf("Whoops, something went wrong.\n");
		return 1;
	}

	midiplayer_init(&player);

	while ((event = smf_get_next_event(smf)) != NULL) {
		if (smf_event_is_metadata(event))
			continue;
		if ((int) (event->time_seconds * samplerate) < sample)
			continue;

		printf("time_seconds=%f sample=%d\n", event->time_seconds, (int) (event->time_seconds * samplerate));
		printf("midi_buffer_length=%d midi_buffer=[%d] [%d] [%d]\n",event->midi_buffer_length , event->midi_buffer[0], event->midi_buffer[1], event->midi_buffer[2]);
		//wait until event->time_seconds.
		//feed_to_midi_output(event->midi_buffer, event->midi_buffer_length);
	}

	smf_delete(smf);
	return 0;
}
