#pragma once

#include "nk_collision.h"
#include "nk_effects.h"
#include "nk_ui.h"

#define NK_GAME_PLAYER_COUNT (NK_PLAYER_COUNT)
#define NK_GAME_PROJECTILE_COUNT (10)
#define NK_GAME_EVENT_COUNT (64)

#define NK_GAME_STATE_WAITING        (-2)
#define NK_GAME_STATE_PREGAME        (-1)
#define NK_GAME_STATE_ACTIVE         (0)
#define NK_GAME_STATE_ROUND_ANIMATION (1)
#define NK_GAME_STATE_ROUND_COMPLETE (3)
#define NK_GAME_STATE_MATCH_COMPLETE (4)

#define NK_GAME_ACTOR_BALL    (NK_GAME_PLAYER_COUNT)
#define NK_GAME_ACTOR_CHOPPER (NK_GAME_PLAYER_COUNT + 1)

#define NK_PROJECTILE_STATE_FLYING    (0U)
#define NK_PROJECTILE_STATE_HIT_PLAYER (1U)
#define NK_PROJECTILE_STATE_HIT_BALL  (2U)
#define NK_PROJECTILE_STATE_ATTACHED  (3U)

#define NK_MOVE_STANCE (0)
#define NK_MOVE_UP (1)
#define NK_MOVE_DOWN (2)
#define NK_MOVE_WIN (3)
#define NK_MOVE_LOSE (4)
#define NK_MOVE_TAP_RECOVERY (5)
#define NK_MOVE_SPECIAL_ONE (6)
#define NK_MOVE_SPECIAL_TWO (7)
#define NK_MOVE_HOLD (8)
#define NK_MOVE_RELEASE (9)
#define NK_MOVE_PROJECTILE_FLY (20)
#define NK_MOVE_PROJECTILE_HIT (21)
#define NK_MOVE_PROJECTILE_REPEAT (22)
#define NK_MOVE_DIZZY (23)

#define NK_PLAYER_DONE_ATTACK (0x0001U)

#define NK_GAME_EVENT_SOUND (1U)
#define NK_GAME_EVENT_SCORE (2U)
#define NK_GAME_EVENT_BALL_RELEASE (3U)
#define NK_GAME_EVENT_PROJECTILE (4U)
#define NK_GAME_EVENT_SOUND_TYPE_MASK (0x01U)

#define NK_GAME_DIALOGUE_NONE (0U)
#define NK_GAME_DIALOGUE_WIN  (1U)
#define NK_GAME_DIALOGUE_LOSS (2U)

typedef struct NkGameEvent
{
  u8    type;
  u8    actor;
  u8    value;
  u8    flags;
  s32    x;
  s32    y;
} NkGameEvent;

typedef struct NkProjectile
{
  NkAnimCursor animation;
  u8    active;
  u8    state;
  u8    type;
  u8    facing;
  s32    x;
  s32    y;
  s32    fraction_x;
  s32    fraction_y;
  s32    shake;
  s32    stick_x;
  s32    stick_y;
} NkProjectile;

typedef struct NkGamePlayer
{
  u8    character_type;
  u8    animation_bank;
  u8    control_type;
  u8    facing;
  u8    draw_me;
  u8    new_move;
  u8    projectile_exists;
  u8    reserved;

  s32    x;
  s32    y;
  s32    move;
  s32    status;
  s32    control_ball;
  s32    stuck;
  s32    freeze;
  s32    fraction_x;
  s32    fraction_y;
  s32    presentation_previous_x;
  s32    presentation_previous_y;
  s32    energy;
  s32    maximum_energy;
  s32    dizzy;
  s32    dizzy_duration;
  s32    pain;
  u32    pain_duration;
  s32    destination_x;
  s32    destination_y;
  s32    strategy;
  s32    strategy_duration;

  u8    input_stat;
  u8    input_buttons;
  u8    input_attack;
  u8    input_commands;
  u8    presentation_draw_me;
  u8    presentation_frozen;

  NkAnimCursor animation;
  nk_vector vector;
  NkProjectile projectiles[NK_GAME_PROJECTILE_COUNT];
  s32    projectile_previous_x[NK_GAME_PROJECTILE_COUNT];
  s32    projectile_previous_y[NK_GAME_PROJECTILE_COUNT];
  u8    projectile_interpolation_ready[NK_GAME_PROJECTILE_COUNT];
} NkGamePlayer;

typedef struct NkGame
{
  NkGamePlayer players[NK_GAME_PLAYER_COUNT];
  NkGamePlayer ball;
  NkGamePlayer chopper;
  NkEffectPool effects;
  nk_rng rng;

  s32    game_state;
  s32    game_over;
  s32    winner;
  s32    outcome_dialogue_index;
  s32    score[NK_GAME_PLAYER_COUNT];
  s32    bar_size[NK_GAME_PLAYER_COUNT];
  s32    round_target;
  s32    ball_pain;
  s32    electrocute;
  s32    electrocute_duration;
  s32    draw_priority;
  s32    ready;
  s32    multiplayer;
  s32    multiplayer_master;
  s32    sticky_blood;
  s32    difficulty_y_miss;
  s32    difficulty_idle_time;
  s32    difficulty_special_time;
  s32    fix_orig_bugs;
  u32    tick;
  u8    effect_clock_phase;
  u8    new_frame;
  u8    outcome_dialogue_kind;

  NkGameEvent events[NK_GAME_EVENT_COUNT];
  u32    event_count;
} NkGame;

bool
nk_game_configure(NkGame *game_,
                  u8      player_zero_type_,
                  u8      player_zero_control_,
                  u8      player_one_type_,
                  u8      player_one_control_,
                  s32     round_target_,
                  s32     difficulty_,
                  s32     fix_orig_bugs_,
                  u32     seed_);
/*
 * Reconfigure an already initialized campaign game while preserving the
 * source's four global paindur timestamps across opponent matches.
 */
bool
nk_game_reconfigure(NkGame *game_,
                    u8      player_zero_type_,
                    u8      player_zero_control_,
                    u8      player_one_type_,
                    u8      player_one_control_,
                    s32     round_target_,
                    s32     difficulty_,
                    s32     fix_orig_bugs_,
                    u32     seed_);
void
nk_game_begin_match(NkGame *game_);
void
nk_game_seed_match_directions(NkGame     *game_,
                              const u8    directions_[NK_GAME_PLAYER_COUNT]);
void
nk_game_set_input(NkGame                *game_,
                  u8                     player_index_,
                  const nk_input_sample *input_);
void
nk_game_clear_events(NkGame *game_);
void
nk_game_step(NkGame *game_);
/*
 * Apply the state changes performed at the top of one source painter pass.
 * The -2 clause intentionally precedes the state-3 clause, so a non-final
 * state 3 becomes -2 for one presentation before the next pass starts the
 * chopper.
 */
void
nk_game_prepare_presentation(NkGame *game_);

const
NkAnimFrame *
nk_game_player_frame(const NkGamePlayer *player_);
bool
nk_game_try_ball_hit(NkGame *game_,
                     u8      player_index_);
bool
nk_game_try_pain_drip(NkGame       *game_,
                      NkGamePlayer *owner_,
                      int           paused_,
                      s32           x_,
                      s32           y_,
                      s32           image_width_,
                      s32           image_height_);
bool
nk_game_validate(const NkGame *game_);
