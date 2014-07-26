/* Jack Audio Server
 * Author: Sebastian Weiss <dl3yc@darc.de>
 */

#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <unistd.h>
#include <getopt.h>
#include <string.h>

#include <jack/jack.h>
#include <jack/control.h>

#include "jackserver.h"

static jackctl_driver_t * jackctl_server_get_driver(jackctl_server_t *server,
						    const char *driver_name)
{
	const JSList * node_ptr = jackctl_server_get_drivers_list(server);

	while (node_ptr) {
		if (strcmp(jackctl_driver_get_name((jackctl_driver_t *)node_ptr->data), driver_name) == 0) {
			return (jackctl_driver_t *)node_ptr->data;
		}
		node_ptr = jack_slist_next(node_ptr);
	}
	return NULL;
}

static jackctl_parameter_t *jackctl_get_parameter(const JSList *parameters, const char *parameter)
{
	int ret;

	while (parameters) {
		ret = strcmp(jackctl_parameter_get_name((jackctl_parameter_t *)parameters->data), parameter);
		if (ret == 0)
			return (jackctl_parameter_t *)parameters->data;
		parameters = jack_slist_next(parameters);
	}
	return NULL;
}

static jackctl_parameter_t *jackctl_server_get_parameter(jackctl_server_t *server, const char *parameter)
{
	const JSList *parameters = jackctl_server_get_parameters(server);
	return jackctl_get_parameter(parameters, parameter);
}

static jackctl_parameter_t *jackctl_driver_get_parameter(jackctl_driver_t *driver, const char *parameter)
{
	const JSList *parameters = jackctl_driver_get_parameters(driver);
	return jackctl_get_parameter(parameters, parameter);
}

int jackctl_server_set_value(jackctl_server_t *server, const char *parameter_name, union jackctl_parameter_value *value)
{
	int ret;
	jackctl_parameter_t *parameter;

	parameter = jackctl_server_get_parameter(server, parameter_name);
	if (parameter == NULL) {
		fprintf(stderr, "jackserver: can't get parameter %s\n", parameter_name);
		return -1;
	}

	ret = jackctl_parameter_set_value(parameter, value);
	if (ret == 0) {
		fprintf(stderr, "jackserver: can't set parameter %s\n", parameter_name);
		return -1;
	}
	return 0;
}

int jackctl_driver_set_value(jackctl_driver_t *driver, const char *parameter_name, union jackctl_parameter_value *value)
{
	int ret;
	jackctl_parameter_t *parameter;

	parameter = jackctl_driver_get_parameter(driver, parameter_name);
	if (parameter == NULL) {
		fprintf(stderr, "jackserver: can't get parameter %s\n", parameter_name);
		return -1;
	}

	ret = jackctl_parameter_set_value(parameter, value);
	if (ret == 0) {
		fprintf(stderr, "jackserver: can't set parameter %s\n", parameter_name);
		return -1;
	}
	return 0;
}

int jackserver_init(struct jackserver *jackserver)
{
	int ret;
	jackctl_server_t *server;
	jackctl_driver_t *driver;
	union jackctl_parameter_value value = { .i = 0 };

	server = jackctl_server_create(NULL, NULL);
	if (server == NULL) {
		fprintf(stderr, "jackserver: can't create jack server\n");
		return -1;
	}

	strncpy(value.str, jackserver->name, JACK_PARAM_STRING_MAX);
	ret = jackctl_server_set_value(server, "name", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	value.ui = jackserver->realtime;
	ret = jackctl_server_set_value(server, "realtime", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	value.ui = jackserver->realtime_prio;
	ret = jackctl_server_set_value(server, "realtime-priority", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	driver = jackctl_server_get_driver(server, jackserver->audiodriver);
	if (driver == NULL) {
		fprintf(stderr, "jackserver: can't find audio driver %s\n", jackserver->audiodriver);
		jackctl_server_destroy(server);
		return -1;
	}

	strncpy(value.str, jackserver->audiodevice, JACK_PARAM_STRING_MAX);
	ret = jackctl_driver_set_value(driver, "device", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	value.ui = jackserver->samplerate;
	ret = jackctl_driver_set_value(driver, "rate", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	strncpy(value.str, jackserver->playback, JACK_PARAM_STRING_MAX);
	ret = jackctl_driver_set_value(driver, "playback", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	strncpy(value.str, jackserver->capture, JACK_PARAM_STRING_MAX);
	ret = jackctl_driver_set_value(driver, "capture", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	value.ui = jackserver->duplex;
	ret = jackctl_driver_set_value(driver, "duplex", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	value.ui = jackserver->ins;
	ret = jackctl_driver_set_value(driver, "inchannels", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	value.ui = jackserver->outs;
	ret = jackctl_driver_set_value(driver, "outchannels", &value);
	if (ret < 0) {
		jackctl_server_destroy(server);
		return -1;
	}

	ret = jackctl_server_open(server, driver);
	if (ret == 0) {
		fprintf(stderr, "jackserver: can't open server\n");
		jackctl_server_destroy(server);
		return -1;
	}

	driver = jackctl_server_get_driver(server, jackserver->mididriver);
	if (driver == NULL) {
		fprintf(stderr, "jackserver: can't find midi driver %s\n", jackserver->mididriver);
		jackctl_server_destroy(server);
		return -1;
	}

	ret = jackctl_server_add_slave(server, driver);
	if (ret == 0) {
		fprintf(stderr, "jackserver: can't add midi slave\n");
		jackctl_server_destroy(server);
		return -1;
	}

	ret = jackctl_server_start(server);
	if (ret == 0) {
		fprintf(stderr, "jackserver: can't start server\n");
		jackctl_server_destroy(server);
		return -1;
	}

	jackserver->server = server;
	return 0;
}

void jackserver_exit(struct jackserver *jackserver)
{
	jackctl_server_destroy(jackserver->server);
}

int main(void)
{
	int ret;
	static struct jackserver jackserver = {
		.name = "stagepi",
		.audiodriver = "alsa",
		.audiodevice = "hw:0",
		.mididriver = "alsarawmidi",
		.samplerate = 96000,
		.playback = "hw:0,0",
		.capture = "none",
		.ins = 0,
		.outs = 2,
		.duplex = 0,
		.realtime = 1,
		.realtime_prio = 10,
	};

	ret = jackserver_init(&jackserver);
	if (ret < 0)
		return ret;

	printf("main: server started successfully\n");

	/* ... */
	while(1);

	printf("main: server exit...\n");
	jackserver_exit(&jackserver);
	return 0;
}
