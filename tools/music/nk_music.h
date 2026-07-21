#ifndef NK_MUSIC_H
#define NK_MUSIC_H

#include "nk_assets.h"

#define NK_EVENT_NOTE_OFF 0
#define NK_EVENT_NOTE_ON 1
#define NK_EVENT_PROGRAM 2
#define NK_EVENT_CONTROL 3
#define NK_EVENT_PITCH 4
#define NK_EVENT_TEMPO 5
#define NK_EVENT_END 6

typedef struct NkSongEvent {
    unsigned long tick;
    unsigned long sample;
    unsigned long order;
    NkU8 type;
    NkU8 channel;
    NkU8 a;
    NkU8 b;
    NkU32 value;
} NkSongEvent;

typedef struct NkSong {
    NkSongEvent *events;
    size_t event_count;
    size_t event_capacity;
    unsigned long length_samples;
    NkTimbre instruments[256];
    int instrument_count;
} NkSong;

void nk_song_free(NkSong *song);
int nk_song_load(NkSong *song, const NkBlob *block,
                 const NkTimbre *fallback, int fallback_count,
                 char *error, size_t error_size);

#endif
