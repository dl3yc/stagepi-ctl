#ifndef PLAYLIST_H
#define PLAYLIST_H

#define PLAYLIST_STR_SIZE 128

struct song {
	char name[PLAYLIST_STR_SIZE];
	char audio[PLAYLIST_STR_SIZE];
	char midi[PLAYLIST_STR_SIZE];
	int length; /* seconds */
	struct song *next;
	struct song *prev;
};
struct playlist {
	char name[PLAYLIST_STR_SIZE];
	struct song *head;
	struct song *current;
	int count;
};

extern void add_song(struct playlist *list, struct song *new_song);
extern void new_playlist(struct playlist **list);
extern int get_playlist(struct playlist *list, char *filename);

#endif
