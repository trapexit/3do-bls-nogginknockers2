#pragma once

#include "nk_math.h"

#define NK_BUTTON_ONE (0x01U)
#define NK_BUTTON_TWO (0x02U)
#define NK_BUTTON_MASK (0x03U)

#define NK_ATTACK_CODE_NONE             (0U)
#define NK_ATTACK_CODE_PRIMARY_RIGHT    (1U)
#define NK_ATTACK_CODE_PRIMARY_NEUTRAL  (2U)
#define NK_ATTACK_CODE_PRIMARY_LEFT     (3U)
#define NK_ATTACK_CODE_SECONDARY_OFFSET (3U)
#define NK_ATTACK_CODE_SECONDARY_LEFT   (6U)

#define NK_STAT_BUTTON_ONE (0x10U)
#define NK_STAT_BUTTON_TWO (0x20U)
#define NK_STAT_BUTTON_MASK (0x30U)

#define NK_COMMAND_CANCEL               (0x01U)
#define NK_COMMAND_PAUSE                (0x02U)
#define NK_COMMAND_INTERPOLATION_TOGGLE (0x04U)
#define NK_COMMAND_ASPECT_TOGGLE        (0x08U)

#define NK_DISPLAY_INTERPOLATION_OFF        (0U)
#define NK_DISPLAY_INTERPOLATION_VERTICAL   (1U)
#define NK_DISPLAY_INTERPOLATION_HORIZONTAL (2U)
#define NK_DISPLAY_INTERPOLATION_BOTH       (3U)

#define NK_PLAYER_COUNT (2)

#define NK_CONTROL_HUMAN    (1U)
#define NK_CONTROL_REMOTE   (2U)
#define NK_CONTROL_COMPUTER (3U)
#define NK_CONTROL_BALL     (4U)
#define NK_CONTROL_CHOPPER  (5U)

#define NK_UI_EVENT_MOVE   (0x01U)
#define NK_UI_EVENT_ADJUST (0x02U)
#define NK_UI_EVENT_ACCEPT (0x04U)
#define NK_UI_EVENT_CANCEL (0x08U)
#define NK_UI_EVENT_VOICE  (0x10U)

#define NK_MENU_SOUND_NONE   (0U)
#define NK_MENU_SOUND_ADJUST (1U)
#define NK_MENU_SOUND_MOVE   (2U)

#define NK_TITLE_PENDING    (-2)
#define NK_TITLE_CANCELLED  (-1)
#define NK_TITLE_ONE_PLAYER (0)
#define NK_TITLE_TWO_PLAYER (1)
#define NK_TITLE_OPTIONS    (2)
#define NK_TITLE_ATTRACT    (3)

#define NK_TITLE_ATTRACT_TIMEOUT_TICKS (1500U)
#define NK_TITLE_VOICE_COUNT (5)

#define NK_UI_FADE_LEVEL_MAX (32U)

#define NK_OPTION_DIFFICULTY    (0)
#define NK_OPTION_SOUND         (1)
#define NK_OPTION_MUSIC         (2)
#define NK_OPTION_TALKING       (3)
#define NK_OPTION_ROUND         (4)
#define NK_OPTION_FIX_ORIG_BUGS (5)
#define NK_OPTION_EXIT          (6)
#define NK_OPTION_COUNT         (6)
#define NK_OPTION_LEVEL_COUNT (3)

#define NK_DIFFICULTY_MIN (0)
#define NK_DIFFICULTY_MAX (7)
#define NK_VOLUME_MIN     (0)
#define NK_VOLUME_MAX     (7)
#define NK_TOGGLE_OFF     (0)
#define NK_TOGGLE_ON      (1)
#define NK_ROUND_WIN_MIN  (1)
#define NK_ROUND_WIN_MAX  (25)

typedef struct nk_input_filter
{
  u8    previous_buttons;
} nk_input_filter;

typedef struct nk_input_sample
{
  u8    stat;
  u8    buttons;
  u8    attack_code;
  u8    commands;
} nk_input_sample;

typedef struct nk_ui_events
{
  u8    flags;
  u8    menu_sound;
  s8    voice_index;
} nk_ui_events;

typedef struct nk_options
{
  s32    difficulty;
  s32    sound_volume;
  s32    music_volume;
  s32    talking;
  s32    round_setting;
  s32    fix_orig_bugs;
} nk_options;

typedef struct nk_options_state
{
  nk_options pending;
  u8    cursor;
  u8    previous_stat;
  u8    fade_level;
  u8    fade_out;
  u8    done;
} nk_options_state;

typedef struct nk_title_state
{
  u32    tick;
  u32    attract_idle_ticks;
  u8    choice;
  u8    show_choice;
  u8    edge_mask;
  u8    talk_leave;
  u8    button_push;
  u8    fade_level;
  u8    fade_out;
  u8    done;
  u8    cancelled;
  u8    attract_requested;
} nk_title_state;

void
nk_input_filter_reset(nk_input_filter *filter_);
void
nk_input_sample_make(nk_input_filter *filter_,
                     u8               directions_,
                     u8               buttons_,
                     u8               commands_,
                     nk_input_sample *sample_);
void
nk_input_sample_consume_edges(nk_input_sample *sample_);

/* Cycle once per shared command edge; a held command never repeats. */
void
nk_display_interpolation_toggle_step(u8    *mode_,
                                     u8    *down_,
                                     u8     commands_);

void
nk_ui_events_clear(nk_ui_events *events_);

void
nk_options_defaults(nk_options *options_);
s32
nk_options_round_target(const nk_options *options_);
void
nk_options_begin(nk_options_state *state_,
                 const nk_options *options_);
void
nk_options_step(nk_options_state      *state_,
                const nk_input_sample *input_,
                nk_ui_events          *events_);

void
nk_title_begin(nk_title_state *state_);
void
nk_title_step(nk_title_state        *state_,
              const nk_input_sample *input_,
              int                    controller_active_,
              int                    sound_enabled_,
              int                    voice_playing_,
              nk_ui_events          *events_);
int
nk_title_result(const nk_title_state *state_);
