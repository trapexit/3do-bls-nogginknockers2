#pragma once

#include "nk_game.h"
#include "nk_sound_logic.h"

#include "audio.h"
#include "types.h"

#define NK_AUDIO_VOICE_COUNT (NK_SOUND_VOICE_COUNT)
#define NK_AUDIO_BANK_COUNT (NK_SOUND_BANK_COUNT)
#define NK_AUDIO_COMBAT_SAMPLE_COUNT (NK_SOUND_COMBAT_SAMPLE_COUNT)
#define NK_AUDIO_SELECT_SAMPLE_COUNT (2)
#define NK_AUDIO_SCREAM_VARIANT_COUNT (3)
#define NK_AUDIO_SCREAM_LOGICAL_COUNT \
  (NK_CHARACTER_COUNT * NK_AUDIO_SCREAM_VARIANT_COUNT)

#define NK_AUDIO_BANK_OWNER_NONE (0xffU)
#define NK_AUDIO_BANK_OWNER_HEAD (NK_CHARACTER_COUNT)
#define NK_AUDIO_RATE_F16_ONE ((u32)NK_FIXED_ONE)

typedef struct NkAudioVoice
{
  Item player;
  Item attachment;
  Item left_gain;
  Item right_gain;
} NkAudioVoice;

typedef struct NkAudioBank
{
  Item samples[NK_AUDIO_COMBAT_SAMPLE_COUNT];
  void *storage;
  s32    storage_size;
  u32    available_mask;
  u8    owner;
} NkAudioBank;

typedef struct NkAudio
{
  Item mixer;
  Item clock_owner;
  u32    clock_rate_f16;
  NkAudioVoice voices[NK_AUDIO_VOICE_COUNT];
  NkAudioBank banks[NK_AUDIO_BANK_COUNT];
  Item select_samples[NK_AUDIO_SELECT_SAMPLE_COUNT];
  void *scream_bank;
  s32    scream_bank_size;
  void *scream_buffer;
  s32    scream_buffer_size;
  u32    scream_offsets[NK_AUDIO_SCREAM_LOGICAL_COUNT];
  u32    scream_sizes[NK_AUDIO_SCREAM_LOGICAL_COUNT];
  u32    scream_available_mask;
  Item scream_sample;
  u8    initialized;
} NkAudio;

bool
nk_audio_initialize(NkAudio *audio_);
u32
nk_audio_clock_rate_f16(const NkAudio *audio_);
bool
nk_audio_load_combat(NkAudio *audio_,
                     u8       player_zero_type_,
                     u8       player_one_type_);
void
nk_audio_unload_combat(NkAudio *audio_);
void
nk_audio_unload_opponent(NkAudio *audio_);
bool
nk_audio_load_select(NkAudio *audio_);
void
nk_audio_unload_select(NkAudio *audio_);
bool
nk_audio_load_screams(NkAudio *audio_);
Item
nk_audio_prepare_scream(NkAudio *audio_,
                        s32      character_,
                        s32      scream_index_);
void
nk_audio_release_scream_sample(NkAudio *audio_);
void
nk_audio_leave_select(NkAudio *audio_);
void
nk_audio_unload_screams(NkAudio *audio_);
u32
nk_audio_loaded_sample_count(const NkAudio *audio_);
void
nk_audio_stop_all(NkAudio *audio_);
int
nk_audio_play_item(NkAudio *audio_,
                   Item     sample_);
bool
nk_audio_voice_playing(const NkAudio *audio_,
                       int            voice_index_);
void
nk_audio_release_voice(NkAudio *audio_,
                       int      voice_index_);
bool
nk_audio_set_volume(NkAudio *audio_,
                    s32      level_);
void
nk_audio_play_event(NkAudio           *audio_,
                    const NkGameEvent *event_);
void
nk_audio_play_events(NkAudio      *audio_,
                     const NkGame *game_);
void
nk_audio_shutdown(NkAudio *audio_);
