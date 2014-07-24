#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include "m3u_file.h"
#include "playlist.h"

void next_song(struct playlist *list)
{
	list->current = list->current->next;
}

void new_song(struct song **song)
{
	*song = (struct song *) malloc(sizeof(struct song));
	(**song).length = 0;
	(**song).next = NULL;
	(**song).prev = NULL;
}

void add_song(struct playlist *list, struct song *new_song)
{
	if (list->head == NULL) {
		list->head = new_song;
		list->current = new_song;
		new_song->next = new_song;
		new_song->prev = new_song;
	} else {
		list->head->prev->next = new_song;
		new_song->prev = list->head->prev;
		new_song->next = list->head;
		list->head->prev = new_song;
	}
	list->count++;
}

void new_playlist(struct playlist **list)
{
	*list = (struct playlist *) malloc(sizeof(struct playlist));
	(**list).head = NULL;
	(**list).count = 0;
}

void free_playlist(struct playlist *list)
{
	list->current = list->head;
	do {
		free(list->current);
		list->current = list->current->next;
	} while(list->current == list->head);
	free(list);
}

int get_playlist(struct playlist *list, char *filename)
{
	struct song *song;
	struct m3u_file *file;
	struct m3u_block *actual_block;

	new_m3u_file(&file);
	strcpy(file->filename, filename);
	if (get_m3u_file(file) == -1)
		return -1;

	strcpy(list->name, basename(filename));

	actual_block = file->first_block;
	while(actual_block != NULL) {
		new_song(&song);
		if (actual_block->name_valid) {
			strcpy(song->name, actual_block->name); /* TODO: extract name*/
		} else if (actual_block->audio_valid){
			strcpy(song->name, basename(actual_block->audio));
		} else if (actual_block->midi_valid){
			strcpy(song->name, basename(actual_block->midi));
		}
		song->length = 0; /* TODO: get length from name */
		add_song(list, song);
		actual_block = actual_block->next_block;
	}
	free_m3u_file(file);
	return 0;
}

int main(void)
{
	struct playlist *list;
	int ret;

	new_playlist(&list);
	ret = get_playlist(list, "/home/yc/test.m3u");
	if (ret < 0)
		return 1;

	list->current = list->head;
	do{
		printf("name: %s audio: %s midi: %s\n", list->current->name, list->current->audio, list->current->midi);
		next_song(list);
	} while(list->current != list->head);

	free_playlist(list);
	return 0;
}
