#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <libgen.h>

#include "m3u_file.h"

int readline(char **line, FILE *fp, ssize_t *read)
{
	size_t len = 0;
	*read = getline(line, &len, fp);
	if (*read == -1) {
		free(*line);
		return -1;
	}
	if ((*line)[*read-1] == '\n') {
		(*line)[*read-1] = '\0';
		read--;
	}
	return 0;
}

void new_m3u_block(struct m3u_block **block)
{
	struct m3u_block *new_block;
	new_block = (struct m3u_block *) malloc(sizeof(struct m3u_block));

	new_block->name_valid = 0;
	new_block->midi_valid = 0;
	new_block->audio_valid = 0;
	new_block->next_block = NULL;

	if (*block != NULL)
		(**block).next_block = new_block;
	*block=new_block;
}

void new_m3u_file(struct m3u_file **file)
{
	*file = (struct m3u_file *) malloc(sizeof(struct m3u_file));
	(**file).is_m3uext = 0;
	(**file).first_block = NULL;
}

void free_m3u_file(struct m3u_file *file)
{
	struct m3u_block *block;
	while(file->first_block != NULL) {
		block = file->first_block;
		file->first_block = block->next_block;
		free(block);
	}
	free(file);
}

int get_m3u_file(struct m3u_file *file)
{
	FILE *fp;
	char *line;
	ssize_t read;
	struct m3u_block *block = NULL;
	int ret;

	fp = fopen(file->filename, "r");
	if (fp == NULL) {
		fclose(fp);
		fprintf(stderr, "get_m3u_file: error while opening file\n");
		return -1;
	}

	ret = readline(&line, fp, &read);
	if (ret == -1)
		return -1;
	if (strncmp(line, "#EXTM3U", 7) == 0) {
		file->is_m3uext = 1;
		ret = readline(&line, fp, &read);
		if (ret == -1)
			return -1;
	} else {
		file->is_m3uext = 0;
	}

	new_m3u_block(&block);
	file->first_block = block;

	do {
		if (line[0] == '#') {
			/* meta data */
			if (strncmp(line, "#EXTINFO:", 9) == 0) {
				if (block->audio_valid)
					new_m3u_block(&block);
				line += 9;
				strncpy(block->name, line, read);
				block->name_valid = 1;
			} else if (strncmp(line, "#EXTMIDI:", 9) == 0) {
				if (block->midi_valid)
					new_m3u_block(&block);
				line += 9;
				strncpy(block->midi, line, read);
				block->midi_valid = 1;
			}
		} else {
			/* audio file */
			if (block->audio_valid)
				new_m3u_block(&block);
			strncpy(block->audio, line, read);
			block->audio_valid = 1;
		}
	} while(readline(&line, fp, &read) == 0);
	fclose(fp);
	return 0;
}
