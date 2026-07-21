#pragma once

#include "nk_audio.h"

#include "soundplayer.h"
#include "types.h"

#define NK_MUSIC_TRACK_NONE        (0U)
#define NK_MUSIC_TRACK_TITLE_MIDI  (1U)
#define NK_MUSIC_TRACK_TITLE_AWE32 (2U)
#define NK_MUSIC_TRACK_MATCH_CMF   (3U)
#define NK_MUSIC_TRACK_COUNT       (3U)

typedef struct NkMusic
{
  SPPlayer *player;
  SPSound *sounds[NK_MUSIC_TRACK_COUNT];
  Item sampler;
  Item mixer;
  Item left_gain;
  Item right_gain;
  Item thread;
  Item parent_task;
  int32 parent_signal;
  int32 command_signal;
  int32 signals_needed;
  volatile int32 command;
  volatile Err command_result;
  volatile Err service_result;
  volatile s32    command_argument;
  volatile s32    volume;
  volatile s32    active_track;
  volatile s32    initialized;
  volatile s32    playing;
  volatile s32    paused;
  s32    thread_folio_open;
} NkMusic;

bool
nk_music_initialize(NkMusic       *music_,
                    const NkAudio *audio_,
                    s32            volume_);
bool
nk_music_play(NkMusic *music_,
              u8       track_);
bool
nk_music_set_volume(NkMusic *music_,
                    s32      volume_);
bool
nk_music_set_paused(NkMusic *music_,
                    int      paused_);
bool
nk_music_service(NkMusic *music_);
bool
nk_music_stop(NkMusic *music_);
void
nk_music_shutdown(NkMusic *music_);
