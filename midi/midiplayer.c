#include <string.h>
#include <pthread.h>
#include <smf.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

struct midiplayer {
	/* user data */
	char *name;
	char *servername;
	char *midiport;

	/* internal data */
	jack_client_t *client;
	jack_port_t *port;
	jack_ringbuffer_t *rb;
	smf_t *smf;
	int handle_reposition;
	int reposition_handled;
	jack_nframes_t frame;
	pthread_t thread_id;
	int init_done;
	int load_midi;
	int midi_loaded;
	char *midifile;
	pthread_mutex_t sync_mutex;
	pthread_mutex_t load_mutex;
};

static struct midiplayer midiplayer_new = {
	.name = NULL,
	.servername = NULL,
	.midiport = NULL,
	.client = NULL,
	.port = NULL,
	.smf = NULL,
	.handle_reposition = 0,
	.reposition_handled = 0,
	.frame = 0,
	.thread_id = 0,
	.init_done = 0,
	.load_midi = 0,
	.midi_loaded = 0,
	.midifile = NULL,
};

static int midiplayer_process(jack_nframes_t nframes, void *arg)
{
	struct midiplayer *player = (struct midiplayer *) arg;
	return 0;
}

static int midiplayer_sync(jack_transport_state_t state, jack_position_t *pos, void *arg)
{
	struct midiplayer *player = (struct midiplayer *) arg;
	int ret;

	if (pthread_mutex_trylock(&player->sync_mutex) != 0)
		return 0;

	if (player->reposition_handled) {
		player->reposition_handled = 0;
		player->handle_reposition = 0;
		ret = 1;
	} else {
		player->handle_reposition = 1;
		player->frame = pos->frame;
		ret = 0;
	}
	pthread_mutex_unlock(&player->sync_mutex);
	return ret;
}

static void *midiplayer_source(void *arg)
{
	struct midiplayer *player = (struct midiplayer *) arg;
	int samplerate;
	int ret;

	while(!player->init_done);

	samplerate = jack_get_sample_rate(player->client);

	while(1) {
		if (player->load_midi) {
			if (pthread_mutex_trylock(&player->load_mutex) == 0) {
				player->smf = smf_load(player->midifile);
				if (player->smf == NULL) {
					fprintf(stderr, "midiplayer: can't load new midi file %s\n", player->midifile);
					player->midi_loaded = 0;
				} else {
					player->midi_loaded = 1;
				}
				player->load_midi = 0;
				pthread_mutex_unlock(&player->load_mutex);
			}
		}

		if (player->midi_loaded != 1)
			continue;

		if ((player->handle_reposition) && !(player->reposition_handled)) {
			if (pthread_mutex_trylock(&player->sync_mutex) == 0) {
				ret = smf_seek_to_seconds(player->smf, (double) player->frame / samplerate);
				if (ret == 0)
					player->reposition_handled = 1;
				else
					player->reposition_handled = 1; /* return -1 for could not handle */
				pthread_mutex_unlock(&player->sync_mutex);
			}
		}

		/* ... */

	}

	return 0;
}

int midiplayer_load(struct midiplayer *player, char *midifile)
{
	pthread_mutex_lock(&player->load_mutex);
	player->midifile = midifile;
	player->load_midi = 1;
	pthread_mutex_unlock(&player->load_mutex);
	return 0;
}

int midiplayer_init(struct midiplayer *player)
{
	jack_status_t status;
	jack_options_t options = JackNullOption | JackServerName | JackNoStartServer;
	int ret;

	ret = pthread_mutex_init(&player->sync_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't init sync mutex\n");
	}

	ret = pthread_mutex_init(&player->load_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't init load mutex\n");
	}

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

	player->init_done = 1;

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
	smf_event_t *event;
	struct midiplayer player = midiplayer_new;
	player.name = "midiplayer";
	player.servername = "stagepi";
	player.midiport = "system:midi_playback_1";

	if (argc != 2) {
		printf("usage: %s midifile.mid\n", argv[0]);
		return 1;
	}

	midiplayer_init(&player);
	midiplayer_load(&player, argv[1]);

	while(1);

	while ((event = smf_get_next_event(player.smf)) != NULL) {
		if (smf_event_is_metadata(event))
			continue;
		if ((int) (event->time_seconds * samplerate) < sample)
			continue;

		printf("time_seconds=%f sample=%d\n", event->time_seconds, (int) (event->time_seconds * samplerate));
		printf("midi_buffer_length=%d midi_buffer=[%d] [%d] [%d]\n",event->midi_buffer_length , event->midi_buffer[0], event->midi_buffer[1], event->midi_buffer[2]);
		//wait until event->time_seconds.
		//feed_to_midi_output(event->midi_buffer, event->midi_buffer_length);
	}

	return 0;
}
