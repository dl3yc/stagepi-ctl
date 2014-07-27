#include <string.h>
#include <pthread.h>
#include <smf.h>
#include <jack/jack.h>
#include <jack/ringbuffer.h>
#include <jack/midiport.h>

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
	int rb_reset;
	pthread_mutex_t sync_mutex;
	pthread_mutex_t load_mutex;
	pthread_mutex_t reset_mutex;
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
	.rb_reset = 0,
};

static int midiplayer_process(jack_nframes_t nframes, void *arg)
{
	struct midiplayer *player = (struct midiplayer *) arg;
	void* output_buffer = jack_port_get_buffer(player->port, nframes);
	int read_space;
	int i;
	static int repeat_write = 0;
	static uint8_t cmd[5];
	char msg[16];
	int32_t offset;
	jack_transport_state_t state;
	jack_position_t pos;

	jack_midi_clear_buffer(output_buffer);
	read_space = jack_ringbuffer_read_space(player->rb);

	state = jack_transport_query(player->client, &pos);

	if (state != JackTransportRolling)
		return 0;

	if (read_space == 0)
		return 0;

	if (player->rb_reset == 1) {
		if (pthread_mutex_trylock(&player->reset_mutex)) {
			repeat_write = 0;
			player->rb_reset = 0;
			pthread_mutex_unlock(&player->reset_mutex);
		} else {
			return 0;
		}
	}

	i = 0;
	while(i < read_space) {
		if (!repeat_write)
			jack_ringbuffer_read(player->rb, (char *) cmd, 5);
		offset = (uint32_t) cmd[0] + ((uint32_t) cmd[1] << 8) + ((uint32_t) cmd[2] << 16) + ((uint32_t) cmd[3] << 24) - pos.frame;
		if (offset < 0)
			offset = 0;
		if (offset > nframes) {
			repeat_write = 1;
			break;
		}
		repeat_write = 0;

		jack_ringbuffer_read(player->rb, msg, cmd[4]);
		jack_midi_event_write(output_buffer, offset, (jack_midi_data_t *) msg, cmd[4]);
		printf("raw=%d\n", (uint32_t) cmd[0] + ((uint32_t) cmd[1] << 8) + ((uint32_t) cmd[2] << 16) + ((uint32_t) cmd[3] << 24));
		printf("process: offset=%d pos.frame=%d\n", offset, pos.frame);
		printf("cmd[0]=%2.0x cmd[1]=%2.0x cmd[2]=%2.0x cmd[3]=%2.0x\n", cmd[0], cmd[1], cmd[2], cmd[3]);

#if 0
		if (jack_midi_get_lost_event_count(player->port)) {
			printf("process: lost event\n");
		}
#endif
		i += 5 + cmd[4];
	}

	//jack_ringbuffer_reset(player->rb);

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
	smf_event_t *event;
	int samplerate;
	int ret;
	uint8_t msg[5];
	uint32_t offset;
	int repeat_write = 0;
	int rb_reset = 0;

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
				rb_reset = 1;
				printf("source: time:%f\n", (double) player->frame / samplerate);
				if (ret == 0)
					player->reposition_handled = 1;
				else
					player->reposition_handled = 1; /* return -1 for could not handle */
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

		if (!repeat_write) {
			event = smf_get_next_event(player->smf);
			if (event == NULL)
				continue;
			if (smf_event_is_metadata(event))
				continue;
		}

		printf("source: writing buffer...\n");

		if (jack_ringbuffer_write_space(player->rb) < 5 + event->midi_buffer_length) {
			printf("source: not enough space in ringbuf\n");
			repeat_write = 1;
			continue;
		}
		repeat_write = 0;

		offset = (uint32_t) (event->time_seconds * samplerate);
		if (offset < 0)
			offset = 0;

		printf("source: offset=%d\n", offset);

		msg[0] = (uint8_t) (offset);
		msg[1] = (uint8_t) (offset >> 8);
		msg[2] = (uint8_t) (offset >> 16);
		msg[3] = (uint8_t) (offset >> 24);
		msg[4] = (uint8_t) event->midi_buffer_length;
		printf("msg[0]=%2.0x msg[1]=%2.0x msg[2]=%2.0x msg[3]=%2.0x\n", msg[0], msg[1], msg[2], msg[3]);
		ret = jack_ringbuffer_write(player->rb, (char *) msg, 5);
		if (ret < 5) {
			fprintf(stderr, "midiplayer: lost %d bytes\n", 5 - ret);
		}

		ret = jack_ringbuffer_write(player->rb, (const char *) event->midi_buffer, event->midi_buffer_length);
		if (ret < event->midi_buffer_length) {
			fprintf(stderr, "midiplayer: lost %d bytes\n", event->midi_buffer_length - ret);
		}
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

void midiplayer_free(struct midiplayer *player)
{
	jack_ringbuffer_free(player->rb);
	smf_delete(player->smf);
}

int midiplayer_init(struct midiplayer *player)
{
	jack_status_t status;
	jack_options_t options = JackNullOption | JackServerName | JackNoStartServer;
	int ret;

	ret = pthread_mutex_init(&player->sync_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't init sync mutex\n");
		return -1;
	}

	ret = pthread_mutex_init(&player->load_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't init load mutex\n");
		return -1;
	}

	ret = pthread_mutex_init(&player->reset_mutex, NULL);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't init reset mutex\n");
		return -1;
	}

	ret = pthread_create(&player->thread_id, NULL, midiplayer_source, player);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't create source thread\n");
		return -1;
	}

	pthread_detach(player->thread_id);
	if (ret != 0) {
		fprintf(stderr, "midiplayer: can't detach source thread\n");
		return -1;
	}

	player->rb = jack_ringbuffer_create(1024);
	if (player->rb == NULL) {
		fprintf(stderr, "midiplayer: can't create jack ringbuffer\n");
		return -1;
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

	printf("midi loaded\n");

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
