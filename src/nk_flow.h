#pragma once

#include "nk_ui.h"

#define NK_SCENE_LOGO    (0U)
#define NK_SCENE_CREDITS (1U)
#define NK_SCENE_TITLE   (2U)
#define NK_SCENE_OPTIONS (3U)
#define NK_SCENE_SELECT  (4U)
#define NK_SCENE_MATCH   (5U)
#define NK_SCENE_ENDING  (6U)
#define NK_SCENE_EXIT    (7U)
#define NK_SCENE_PORT_CREDIT (8U)

#define NK_MODE_ONE_PLAYER (0U)
#define NK_MODE_TWO_PLAYER (1U)
#define NK_MODE_ATTRACT    (2U)

#define NK_ATTRACT_DIFFICULTY_MIN (3)

typedef struct nk_flow
{
  nk_options options;
  u8    scene;
  u8    mode;
  u8    control[NK_PLAYER_COUNT];
  s32    selected_cursor[NK_PLAYER_COUNT];
  u8    selected_character[NK_PLAYER_COUNT];
  s32    locked[NK_PLAYER_COUNT];
  s32    defeated_mask;
  s32    remaining;
  s32    winner;
  s32    ending_number;
} nk_flow;

u8
nk_cursor_to_character(s32    cursor_);
const
char *
nk_character_name(u32    character_);
void
nk_flow_init(nk_flow *flow_);
void
nk_flow_cinematic_complete(nk_flow *flow_);
void
nk_flow_cinematic_skip(nk_flow *flow_);
void
nk_flow_title_complete(nk_flow *flow_,
                       int      title_result_);
void
nk_flow_options_complete(nk_flow          *flow_,
                         const nk_options *options_);
void
nk_flow_selection_complete(nk_flow *flow_,
                           s32      player_zero_cursor_,
                           s32      player_one_cursor_);
s32
nk_flow_match_difficulty(const nk_flow *flow_,
                         nk_rng        *rng_);
void
nk_flow_match_complete(nk_flow *flow_,
                       s32      winner_);
void
nk_flow_abort_to_title(nk_flow *flow_);
