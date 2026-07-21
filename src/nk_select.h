#pragma once

#include "nk_flow.h"

#define NK_SELECT_PENDING (0)
#define NK_SELECT_COMPLETE (1)
#define NK_SELECT_CANCELLED (2)

#define NK_SELECT_SCREAM_VARIANT_COUNT (3)

extern const u8    nk_select_scream_masks[NK_CHARACTER_COUNT];

typedef struct NkSelectEvents
{
  u8    moved_mask;
  u8    locked_mask;
  s8    scream_character;
  s8    scream_index;
} NkSelectEvents;

typedef struct NkSelectState
{
  s32    cursor[NK_PLAYER_COUNT];
  s32    locked[NK_PLAYER_COUNT];
  s32    skip[NK_PLAYER_COUNT];
  s32    pick_time;
  s32    defeated_mask;
  s32    remaining;
  s32    scream_duration;
  s32    screamer;
  s32    old_screamer;
  s32    pending_screamer;
  s32    queued_screamer;
  u32    tick;
  u8    control[NK_PLAYER_COUNT];
  u8    previous_stat[NK_PLAYER_COUNT];
  u8    fade_level;
  u8    fade_in;
  u8    fade_out;
  u8    done;
  u8    cancelled;
} NkSelectState;

void
nk_select_begin(NkSelectState *state_,
                const nk_flow *flow_,
                nk_rng        *rng_);
void
nk_select_step(NkSelectState        *state_,
               const nk_input_sample input_[NK_PLAYER_COUNT],
               nk_rng               *rng_,
               int                   scream_playing_,
               NkSelectEvents       *events_);
int
nk_select_result(const NkSelectState *state_);
