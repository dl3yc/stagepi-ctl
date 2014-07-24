#ifndef M3U_FILE_H
#define M3U_FILE_H

#define M3U_STR_SIZE 128

struct m3u_block {
	int name_valid;
	int audio_valid;
	int midi_valid;
	char name[M3U_STR_SIZE];
	char audio[M3U_STR_SIZE];
	char midi[M3U_STR_SIZE];
	struct m3u_block *next_block;
};

struct m3u_file {
	char filename[M3U_STR_SIZE];
	int is_m3uext;
	struct m3u_block *first_block;
};

extern void new_m3u_block(struct m3u_block **block);
extern int get_m3u_file(struct m3u_file *file);
extern void new_m3u_file(struct m3u_file **file);
extern void free_m3u_file(struct m3u_file *file);

#endif
