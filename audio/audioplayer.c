/* Audio Player for StagePi
 * Author: Sebastian Weiss <dl3yc@darc.de>
 */

#include <string.h>
#include <pthread.h>
#include <sndfile.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>

struct audioplayer {
	/* user data */
	char *name;
	char *servername;
	char *audioport;

	/* internal data */
	jack_client_t *client;
	jack_port_t *port;
	jack_ringbuffer_t *rb;
	SNDFILE *file;
	SF_INFO info;
	int handle_reposition;
	int reposition_handled;
	jack_nframes_t frame;
	pthread_t thread_id;
	int init_done;
	int load_file;
	int file_loaded;
	char *audiofile;
	int rb_reset;
	pthread_mutex_t sync_mutex;
	pthread_mutex_t load_mutex;
	pthread_mutex_t reset_mutex;
};

static struct audioplayer audioplayer_new = {
	.name = NULL,
	.servername = NULL,
	.audioport = NULL,
	.client = NULL,
	.port = NULL,
	.file = NULL,
	.handle_reposition = 0,
	.reposition_handled = 0,
	.frame = 0,
	.thread_id = 0,
	.init_done = 0,
	.load_file = 0,
	.file_loaded = 0,
	.audiofile = NULL,
	.rb_reset = 0,
};

static int audioplayer_process(jack_nframes_t nframes, void *arg)
{
	struct audioplayer *player = (struct audioplayer *) arg;

	return 0;
}

static int audioplayer_sync(jack_transport_state_t state, jack_position_t *pos, void *arg)
{
	struct audioplayer *player = (struct audioplayer *) arg;
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

static void *audioplayer_source(void *arg)
{
	struct audioplayer *player = (struct audioplayer *) arg;
	int samplerate;
	int rb_reset = 0;
	int ret;

	while(!player->init_done);

	samplerate = jack_get_sample_rate(player->client);

	while(1) {
		if (player->load_file) {
			if (pthread_mutex_trylock(&player->load_mutex) == 0) {
				player->file = sf_open(player->audiofile, SFM_WRITE, &player->info);
				if (player->file == NULL) {
					fprintf(stderr, "audioplayer: can't load new audio file %s\n", player->audiofile);
					player->file_loaded = 0;
				} else {
					player->file_loaded = 1;
				}
				player->load_file = 0;
				pthread_mutex_unlock(&player->load_mutex);
			}
		}

		if (player->file_loaded != 1)
			continue;

		if ((player->handle_reposition) && !(player->reposition_handled)) {
			if (pthread_mutex_trylock(&player->sync_mutex) == 0) {
				//ret = smf_seek_to_seconds(player->smf, (double) player->frame / samplerate);
				rb_reset = 1;
				printf("source: time:%f\n", (double) player->frame / samplerate);
				//if (ret == 0)
					player->reposition_handled = 1;
				//else
				//	player->reposition_handled = 1; /* return -1 for could not handle */
				pthread_mutex_unlock(&player->sync_mutex);
			}
		}

		if (rb_reset) {
			if (pthread_mutex_trylock(&player->reset_mutex) == 0) {
				player->rb_reset = 1;
				jack_ringbuffer_reset(player->rb);
				rb_reset = 0;
				pthread_mutex_unlock(&player->reset_mutex);
			}
		}

	}
	return 0;
}

int audioplayer_load(struct audioplayer *player, char *audiofile)
{
	pthread_mutex_lock(&player->load_mutex);
	player->audiofile = audiofile;
	player->load_file = 1;
	pthread_mutex_unlock(&player->load_mutex);
	return 0;
}

void audioplayer_free(struct audioplayer *player)
{
	jack_ringbuffer_free(player->rb);
	sf_close(player->file);
}

int audioplayer_init(struct audioplayer *player)
{
	jack_status_t status;
	jack_options_t options = JackNullOption | JackServerName | JackNoStartServer;
	int ret;

	ret = pthread_mutex_init(&player->sync_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "audioplayer: can't init sync mutex\n");
		return -1;
	}

	ret = pthread_mutex_init(&player->load_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "audioplayer: can't init load mutex\n");
		return -1;
	}

	ret = pthread_mutex_init(&player->reset_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "audioplayer: can't init reset mutex\n");
		return -1;
	}

	ret = pthread_create(&player->thread_id, NULL, audioplayer_source, player);
	if (ret != 0) {
		fprintf(stderr, "audioplayer: can't create source thread\n");
		return -1;
	}

	pthread_detach(player->thread_id);
	if (ret != 0) {
		fprintf(stderr, "audioplayer: can't detach source thread\n");
		return -1;
	}

	player->rb = jack_ringbuffer_create(1024);
	if (player->rb == NULL) {
		fprintf(stderr, "audioplayer: can't create jack ringbuffer\n");
		return -1;
	}

	player->client = jack_client_open(player->name, options, &status, player->servername);
	if (player->client == NULL) {
		fprintf(stderr, "audioplayer: can't open jack client\n");
		if (status & JackServerFailed) {
			fprintf(stderr, "audioplayer: unable to connect to JACK server\n");
		}
		return -1;
	}

	if (status & JackNameNotUnique) {
		fprintf(stderr, "audioplayer: unique name %s assigned\n", jack_get_client_name(player->client));
	}

	jack_set_process_callback(player->client, audioplayer_process, player);
	jack_set_sync_callback(player->client, audioplayer_sync, player);

	player->port = jack_port_register(player->client, "audioout", JACK_DEFAULT_AUDIO_TYPE, JackPortIsOutput, 0);
	if (player->port == NULL) {
		fprintf(stderr, "audioplayer: can't register jack port\n");
		/* TODO: close jack client */
		return -1;
	}

	ret = jack_activate(player->client);
	if (ret != 0) {
		fprintf(stderr, "audioplayer: can't activate jack\n");
		/* TODO: close jack client */
		return -1;
	}

	ret = jack_connect(player->client, jack_port_name(player->port), player->audioport);
	if (ret != 0) {
		fprintf(stderr, "audioplayer: can't connect port\n");
		/* TODO: close jack client */
		return -1;
	}

	player->init_done = 1;

	return 0;
}

int main(int argc, char *argv[])
{
	struct audioplayer player = audioplayer_new;
	player.name = "audioplayer";
	player.servername = "stagepi";
	player.audioport = "system:";

	if (argc != 2) {
		printf("usage: %s audiofile.wav\n", argv[0]);
		return 1;
	}

	audioplayer_init(&player);
	audioplayer_load(&player, argv[1]);

	printf("file loaded\n");

	while(1);

	audioplayer_free(&player);
	return 0;
}