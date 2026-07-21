#include <stdlib.h>
#include <string.h>

#include "nk_music.h"

static int nk_event_push(NkSong *song, const NkSongEvent *event)
{
    NkSongEvent *new_events;
    size_t new_capacity;
    if (song->event_count == song->event_capacity) {
        new_capacity = song->event_capacity == 0 ? 256u : song->event_capacity * 2u;
        new_events = (NkSongEvent *)realloc(song->events,
                                            new_capacity * sizeof(NkSongEvent));
        if (new_events == NULL) {
            return 0;
        }
        song->events = new_events;
        song->event_capacity = new_capacity;
    }
    song->events[song->event_count++] = *event;
    return 1;
}

static int nk_read_variable(const NkU8 *data, size_t end, size_t *position,
                            unsigned long *value)
{
    int count;
    NkU8 byte;
    unsigned long result;
    result = 0;
    count = 0;
    do {
        if (*position >= end || count == 4) {
            return 0;
        }
        byte = data[(*position)++];
        result = (result << 7) | (unsigned long)(byte & 0x7fu);
        ++count;
    } while ((byte & 0x80u) != 0);
    *value = result;
    return 1;
}

static int nk_parse_event_stream(NkSong *song, const NkU8 *data,
                                 size_t start, size_t end,
                                 unsigned long *order,
                                 char *error, size_t error_size)
{
    size_t position;
    unsigned long tick;
    unsigned long delta;
    unsigned long length;
    NkU8 running_status;
    NkU8 status;
    NkU8 first;
    NkU8 second;
    NkU8 meta_type;
    int channel;
    int message;
    int parameter_count;
    NkSongEvent event;
    position = start;
    tick = 0;
    running_status = 0;
    while (position < end) {
        if (!nk_read_variable(data, end, &position, &delta)) {
            nk_set_error(error, error_size, "music delta time is truncated");
            return 0;
        }
        tick += delta;
        if (position >= end) {
            nk_set_error(error, error_size, "music event is truncated");
            return 0;
        }
        status = data[position];
        if (status < 0x80u) {
            if (running_status == 0) {
                nk_set_error(error, error_size, "music running status is invalid");
                return 0;
            }
            status = running_status;
        } else {
            ++position;
            if (status < 0xf0u) {
                running_status = status;
            }
        }
        if (status == 0xffu) {
            running_status = 0;
            if (position >= end) {
                nk_set_error(error, error_size, "music meta event is truncated");
                return 0;
            }
            meta_type = data[position++];
            if (!nk_read_variable(data, end, &position, &length) ||
                length > (unsigned long)(end - position)) {
                nk_set_error(error, error_size, "music meta payload is truncated");
                return 0;
            }
            if (meta_type == 0x51u && length == 3u) {
                memset(&event, 0, sizeof(event));
                event.tick = tick;
                event.order = (*order)++;
                event.type = NK_EVENT_TEMPO;
                event.value = ((NkU32)data[position] << 16) |
                              ((NkU32)data[position + 1u] << 8) |
                              (NkU32)data[position + 2u];
                if (!nk_event_push(song, &event)) {
                    nk_set_error(error, error_size, "out of memory parsing music");
                    return 0;
                }
            }
            position += (size_t)length;
            if (meta_type == 0x2fu) {
                break;
            }
            continue;
        }
        if (status == 0xf0u || status == 0xf7u) {
            running_status = 0;
            if (!nk_read_variable(data, end, &position, &length) ||
                length > (unsigned long)(end - position)) {
                nk_set_error(error, error_size, "music SysEx event is truncated");
                return 0;
            }
            position += (size_t)length;
            continue;
        }
        if (status >= 0xf0u) {
            running_status = 0;
            parameter_count = (status == 0xf1u || status == 0xf3u) ? 1 :
                              (status == 0xf2u ? 2 : 0);
            if ((size_t)parameter_count > end - position) {
                nk_set_error(error, error_size, "music system event is truncated");
                return 0;
            }
            position += (size_t)parameter_count;
            continue;
        }
        channel = (int)(status & 0x0fu);
        message = (int)(status & 0xf0u);
        parameter_count = (message == 0xc0 || message == 0xd0) ? 1 : 2;
        if ((size_t)parameter_count > end - position) {
            nk_set_error(error, error_size, "music channel event is truncated");
            return 0;
        }
        first = data[position++];
        second = parameter_count == 2 ? data[position++] : 0;
        memset(&event, 0, sizeof(event));
        event.tick = tick;
        event.order = (*order)++;
        event.channel = (NkU8)channel;
        event.a = first;
        event.b = second;
        if (message == 0x80) {
            event.type = NK_EVENT_NOTE_OFF;
        } else if (message == 0x90) {
            event.type = second == 0 ? NK_EVENT_NOTE_OFF : NK_EVENT_NOTE_ON;
        } else if (message == 0xb0) {
            event.type = NK_EVENT_CONTROL;
        } else if (message == 0xc0) {
            event.type = NK_EVENT_PROGRAM;
        } else if (message == 0xe0) {
            event.type = NK_EVENT_PITCH;
            event.value = (NkU32)first | ((NkU32)second << 7);
        } else {
            continue;
        }
        if (!nk_event_push(song, &event)) {
            nk_set_error(error, error_size, "out of memory parsing music");
            return 0;
        }
    }
    return 1;
}

static int nk_event_compare(const void *left, const void *right)
{
    const NkSongEvent *a;
    const NkSongEvent *b;
    a = (const NkSongEvent *)left;
    b = (const NkSongEvent *)right;
    if (a->tick < b->tick) {
        return -1;
    }
    if (a->tick > b->tick) {
        return 1;
    }
    if (a->order < b->order) {
        return -1;
    }
    if (a->order > b->order) {
        return 1;
    }
    return 0;
}

static void nk_convert_midi_ticks(NkSong *song, unsigned int division)
{
    size_t i;
    unsigned long last_tick;
    double sample_position;
    double samples_per_tick;
    NkU32 tempo;
    unsigned long delta;
    last_tick = 0;
    sample_position = 0.0;
    tempo = 500000u;
    samples_per_tick = (double)tempo * 44100.0 /
                       ((double)division * 1000000.0);
    for (i = 0; i < song->event_count; ++i) {
        delta = song->events[i].tick - last_tick;
        sample_position += (double)delta * samples_per_tick;
        song->events[i].sample = (unsigned long)(sample_position + 0.5);
        last_tick = song->events[i].tick;
        if (song->events[i].type == NK_EVENT_TEMPO &&
            song->events[i].value != 0) {
            tempo = song->events[i].value;
            samples_per_tick = (double)tempo * 44100.0 /
                               ((double)division * 1000000.0);
        }
    }
    song->length_samples = song->event_count == 0 ? 0 :
        song->events[song->event_count - 1u].sample + 44100ul;
}

static void nk_convert_cmf_ticks(NkSong *song, unsigned int ticks_per_second)
{
    size_t i;
    double scale;
    if (ticks_per_second == 0) {
        ticks_per_second = 60u;
    }
    scale = 44100.0 / (double)ticks_per_second;
    for (i = 0; i < song->event_count; ++i) {
        song->events[i].sample =
            (unsigned long)((double)song->events[i].tick * scale + 0.5);
    }
    song->length_samples = song->event_count == 0 ? 0 :
        song->events[song->event_count - 1u].sample + 44100ul;
}

static void nk_copy_fallback_instruments(NkSong *song,
                                         const NkTimbre *fallback,
                                         int fallback_count)
{
    int i;
    if (fallback == NULL || fallback_count <= 0) {
        return;
    }
    if (fallback_count > 256) {
        fallback_count = 256;
    }
    for (i = 0; i < fallback_count; ++i) {
        song->instruments[i] = fallback[i];
    }
    song->instrument_count = fallback_count;
}

static int nk_load_midi(NkSong *song, const NkBlob *block,
                        char *error, size_t error_size)
{
    size_t position;
    size_t header_end;
    size_t track_end;
    NkU32 header_length;
    NkU32 track_length;
    NkU16 track_count;
    NkU16 division;
    int track;
    unsigned long order;
    if (block->size < 14u || memcmp(block->data, "MThd", 4u) != 0) {
        nk_set_error(error, error_size, "invalid MIDI header");
        return 0;
    }
    header_length = nk_read_be32(block->data + 4u);
    header_end = 8u + (size_t)header_length;
    if (header_length < 6u || header_end > block->size) {
        nk_set_error(error, error_size, "truncated MIDI header");
        return 0;
    }
    track_count = nk_read_be16(block->data + 10u);
    division = nk_read_be16(block->data + 12u);
    if (track_count == 0 || (division & 0x8000u) != 0 || division == 0) {
        nk_set_error(error, error_size, "unsupported MIDI timing format");
        return 0;
    }
    position = header_end;
    order = 0;
    for (track = 0; track < (int)track_count; ++track) {
        if (position + 8u > block->size ||
            memcmp(block->data + position, "MTrk", 4u) != 0) {
            nk_set_error(error, error_size, "MIDI track header is missing");
            return 0;
        }
        track_length = nk_read_be32(block->data + position + 4u);
        position += 8u;
        if ((size_t)track_length > block->size - position) {
            nk_set_error(error, error_size, "MIDI track is truncated");
            return 0;
        }
        track_end = position + (size_t)track_length;
        if (!nk_parse_event_stream(song, block->data, position, track_end,
                                   &order, error, error_size)) {
            return 0;
        }
        position = track_end;
    }
    qsort(song->events, song->event_count, sizeof(NkSongEvent),
          nk_event_compare);
    nk_convert_midi_ticks(song, (unsigned int)division);
    return 1;
}

static int nk_load_cmf(NkSong *song, const NkBlob *block,
                       char *error, size_t error_size)
{
    unsigned int instrument_offset;
    unsigned int music_offset;
    unsigned int ticks_per_second;
    unsigned int instrument_count;
    unsigned int i;
    unsigned long order;
    const NkU8 *record;
    if (block->size < 40u || memcmp(block->data, "CTMF", 4u) != 0) {
        nk_set_error(error, error_size, "invalid CMF header");
        return 0;
    }
    instrument_offset = (unsigned int)nk_read_u16(block->data + 6u);
    music_offset = (unsigned int)nk_read_u16(block->data + 8u);
    ticks_per_second = (unsigned int)nk_read_u16(block->data + 12u);
    instrument_count = (unsigned int)nk_read_u16(block->data + 36u);
    if (instrument_offset >= block->size || music_offset >= block->size ||
        instrument_offset > music_offset ||
        (size_t)instrument_count * 16u >
            (size_t)music_offset - (size_t)instrument_offset) {
        nk_set_error(error, error_size, "CMF offsets are invalid");
        return 0;
    }
    if (instrument_count > 256u) {
        instrument_count = 256u;
    }
    for (i = 0; i < instrument_count; ++i) {
        record = block->data + instrument_offset + i * 16u;
        song->instruments[i].id = (NkU8)i;
        memcpy(song->instruments[i].opl, record, 13u);
        song->instruments[i].present = 1;
    }
    song->instrument_count = (int)instrument_count;
    order = 0;
    if (!nk_parse_event_stream(song, block->data, (size_t)music_offset,
                               block->size, &order, error, error_size)) {
        return 0;
    }
    qsort(song->events, song->event_count, sizeof(NkSongEvent),
          nk_event_compare);
    nk_convert_cmf_ticks(song, ticks_per_second);
    return 1;
}

void nk_song_free(NkSong *song)
{
    if (song != NULL) {
        free(song->events);
        memset(song, 0, sizeof(*song));
    }
}

int nk_song_load(NkSong *song, const NkBlob *block,
                 const NkTimbre *fallback, int fallback_count,
                 char *error, size_t error_size)
{
    int result;
    if (song == NULL || block == NULL || block->data == NULL ||
        block->size < 4u) {
        nk_set_error(error, error_size, "empty music block");
        return 0;
    }
    memset(song, 0, sizeof(*song));
    nk_copy_fallback_instruments(song, fallback, fallback_count);
    if (memcmp(block->data, "MThd", 4u) == 0) {
        result = nk_load_midi(song, block, error, error_size);
    } else if (memcmp(block->data, "CTMF", 4u) == 0) {
        result = nk_load_cmf(song, block, error, error_size);
    } else {
        nk_set_error(error, error_size, "unknown music stream format");
        result = 0;
    }
    if (!result) {
        nk_song_free(song);
    }
    return result;
}
