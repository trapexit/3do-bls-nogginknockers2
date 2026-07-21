#pragma once

#include "nk_game.h"

/*
 * Platform-neutral form of the v0.78 PlaySE()/PlayMixedSoundEffect()
 * request.  PLAY means allocate another first-free voice, even when the
 * logical index is already active.  STOP and RETRIGGER are representable for
 * source call sites that use stop_sound(), but NkGameEvent combat effects
 * currently produce PLAY requests only.
 */
#define NK_SOUND_REQUEST_NONE (0U)
#define NK_SOUND_REQUEST_PLAY (1U)
#define NK_SOUND_REQUEST_STOP (2U)
#define NK_SOUND_REQUEST_RETRIGGER (3U)

#define NK_SOUND_VOICE_COUNT (7)
#define NK_SOUND_BANK_COUNT (3)
#define NK_SOUND_COMBAT_SAMPLE_COUNT (14)
#define NK_SOUND_SAMPLES_PER_TYPE (7)

#define NK_SOUND_EFFECT_LOGICAL_INDEX (1)
#define NK_SOUND_EFFECT_VOLUME (255U)

typedef struct NkSoundRequest
{
  u8    operation;
  u8    bank_index;
  u8    sample_index;
  u8    volume;
  u8    loop;
  s8    logical_index;
  /* Keep the C89 cross-target record an explicit eight bytes. */
  u8    reserved_zero;
  u8    reserved_one;
} NkSoundRequest;

nk_static_assert(sizeof(NkSoundRequest) == 8U,
                 nk_sound_request_size_is_8);

void
nk_sound_request_clear(NkSoundRequest *request_);

/*
 * Translate one portable game event using the v0.78 PlaySE() selector rules.
 * Returns true only when a playable player-specific sample was selected.
 * Invalid or non-sound events leave a deterministic NONE request.
 */
bool
nk_sound_request_from_game_event(const NkGameEvent *event_,
                                 NkSoundRequest    *request_);

/*
 * Convert the persisted 0..7 sound-volume option to the Portfolio mixer gain
 * used by the port.  Out-of-range values are clamped exactly as the runtime
 * option adapter did before this logic was extracted.
 */
s32
nk_sound_gain_from_option(s32    level_);

/*
 * Convert the persisted 0..7 music-volume option to the Portfolio mixer gain.
 * Level zero is silent; nonzero levels preserve the retail DOS volume curve.
 */
s32
nk_music_gain_from_option(s32    level_);
