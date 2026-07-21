#include "nk_ui.h"

#define NK_OPTION_DEFAULT_DIFFICULTY    (3)
#define NK_OPTION_DEFAULT_SOUND_VOLUME  (7)
#define NK_OPTION_DEFAULT_MUSIC_VOLUME  (0)
#define NK_OPTION_DEFAULT_TALKING       (NK_TOGGLE_ON)
#define NK_OPTION_DEFAULT_ROUND_SETTING (3)

#define NK_TITLE_VOICE_START_TICK  (90U)
#define NK_TITLE_CHOICES_START_TICK (200U)
#define NK_TITLE_MOVE_PUSH_TICKS   (30U)
#define NK_TITLE_ACCEPT_PUSH_TICKS (200U)

#define NK_TITLE_LEAVE_IDLE             (0U)
#define NK_TITLE_LEAVE_SELECTION_VOICE  (1U)
#define NK_TITLE_LEAVE_EXIT_VOICE       (2U)
#define NK_TITLE_LEAVE_FADE             (3U)


static
void
_nk_title_emit_voice(nk_ui_events *events_,
                     s8            voice_index_,
                     int           sound_enabled_,
                     int          *voice_playing_)
{
  if((sound_enabled_) && (!*voice_playing_))
    {
      events_->flags |= NK_UI_EVENT_VOICE;
      events_->voice_index = voice_index_;
      *voice_playing_ = true;
    }
}


void
nk_input_filter_reset(nk_input_filter *filter_)
{
  filter_->previous_buttons = 0U;
}


void
nk_input_sample_make(nk_input_filter *filter_,
                     u8               directions_,
                     u8               buttons_,
                     u8               commands_,
                     nk_input_sample *sample_)
{
  u8 pressed;
  u8 stat;
  u8 attack_code;

  directions_ &= NK_DIR_MASK;
  buttons_    &= NK_BUTTON_MASK;
  pressed      = ((u8)(buttons_ & (u8)~filter_->previous_buttons));
  stat         = ((u8)(directions_ | (u8)(pressed << 4)));

  attack_code = NK_ATTACK_CODE_NONE;
  if((stat & NK_STAT_BUTTON_MASK) != 0U)
    {
      attack_code = NK_ATTACK_CODE_PRIMARY_NEUTRAL;
      if((stat & NK_DIR_RIGHT) != 0U)
        attack_code = NK_ATTACK_CODE_PRIMARY_RIGHT;

      if((stat & NK_DIR_LEFT) != 0U)
        attack_code = NK_ATTACK_CODE_PRIMARY_LEFT;

      if((stat & NK_STAT_BUTTON_TWO) != 0U)
        attack_code = (u8)(attack_code + NK_ATTACK_CODE_SECONDARY_OFFSET);
    }

  sample_->stat = stat;
  sample_->buttons = buttons_;
  sample_->attack_code = attack_code;
  sample_->commands = commands_;
  filter_->previous_buttons = buttons_;
}


void
nk_input_sample_consume_edges(nk_input_sample *sample_)
{
  if(sample_ == NULL)
    return;

  sample_->stat &= NK_DIR_MASK;
  sample_->attack_code = NK_ATTACK_CODE_NONE;
}


void
nk_display_interpolation_toggle_step(u8 *mode_,
                                     u8 *down_,
                                     u8  commands_)
{
  u8 pressed;

  if((mode_ == NULL) || (down_ == NULL))
    return;

  pressed = (u8)((commands_ & NK_COMMAND_INTERPOLATION_TOGGLE) != 0U);
  if((pressed) && (!*down_))
    {
      if(*mode_ >= NK_DISPLAY_INTERPOLATION_BOTH)
        *mode_ = NK_DISPLAY_INTERPOLATION_OFF;
      else
        (*mode_)++;
    }

  *down_ = pressed;
}


void
nk_ui_events_clear(nk_ui_events *events_)
{
  events_->flags = 0U;
  events_->menu_sound = NK_MENU_SOUND_NONE;
  events_->voice_index = -1;
}


void
nk_options_defaults(nk_options *options_)
{
  /* Port defaults used when NOG2.CFG does not yet exist. */
  options_->difficulty = NK_OPTION_DEFAULT_DIFFICULTY;
  options_->sound_volume = NK_OPTION_DEFAULT_SOUND_VOLUME;
  options_->music_volume = NK_OPTION_DEFAULT_MUSIC_VOLUME;
  options_->talking = NK_OPTION_DEFAULT_TALKING;
  options_->round_setting = NK_OPTION_DEFAULT_ROUND_SETTING;
  options_->fix_orig_bugs = false;
}


s32
nk_options_round_target(const nk_options *options_)
{
  return options_->round_setting;
}


void
nk_options_begin(nk_options_state *state_,
                 const nk_options *options_)
{
  state_->pending       = *options_;
  state_->cursor        = NK_OPTION_DIFFICULTY;
  state_->previous_stat = 0U;
  state_->fade_level = NK_UI_FADE_LEVEL_MAX;
  state_->fade_out      = 0U;
  state_->done          = 0U;
}


void
nk_options_step(nk_options_state      *state_,
                const nk_input_sample *input_,
                nk_ui_events          *events_)
{
  u8   stat;
  s32 *value;
  s32  minimum;
  s32  maximum;

  nk_ui_events_clear(events_);
  if(state_->done)
    return;

  if(state_->fade_out)
    {
      if(state_->fade_level > 0U)
        {
          state_->fade_level--;
        }
      else
        {
          state_->done = 1U;
          return;
        }
    }

  stat = input_->stat;
  if(state_->previous_stat != stat)
    {
      if(((stat & NK_DIR_UP) != 0U) && (state_->cursor > 0U))
        {
          state_->cursor--;
          events_->flags |= NK_UI_EVENT_MOVE;
          events_->menu_sound = NK_MENU_SOUND_MOVE;
        }
      else if(((stat & NK_DIR_DOWN) != 0U) &&
              (state_->cursor < NK_OPTION_EXIT))
        {
          state_->cursor++;
          events_->flags |= NK_UI_EVENT_MOVE;
          events_->menu_sound = NK_MENU_SOUND_MOVE;
        }
      else if(state_->cursor < NK_OPTION_EXIT)
        {
          value = &state_->pending.difficulty;
          minimum = NK_DIFFICULTY_MIN;
          maximum = NK_DIFFICULTY_MAX;
          if(state_->cursor == NK_OPTION_SOUND)
            {
              value = &state_->pending.sound_volume;
              minimum = NK_VOLUME_MIN;
              maximum = NK_VOLUME_MAX;
            }
          else if(state_->cursor == NK_OPTION_MUSIC)
            {
              value = &state_->pending.music_volume;
              minimum = NK_VOLUME_MIN;
              maximum = NK_VOLUME_MAX;
            }
          else if(state_->cursor == NK_OPTION_TALKING)
            {
              value = &state_->pending.talking;
              maximum = NK_TOGGLE_ON;
            }
          else if(state_->cursor == NK_OPTION_ROUND)
            {
              value = &state_->pending.round_setting;
              minimum = NK_ROUND_WIN_MIN;
              maximum = NK_ROUND_WIN_MAX;
            }
          else if(state_->cursor == NK_OPTION_FIX_ORIG_BUGS)
            {
              value = &state_->pending.fix_orig_bugs;
              maximum = NK_TOGGLE_ON;
            }

          if(((stat & NK_DIR_LEFT) != 0U) && (*value > minimum))
            {
              (*value)--;
              events_->flags |= NK_UI_EVENT_ADJUST;
              events_->menu_sound = NK_MENU_SOUND_ADJUST;
            }
          else if(((stat & NK_DIR_RIGHT) != 0U) && (*value < maximum))
            {
              (*value)++;
              events_->flags |= NK_UI_EVENT_ADJUST;
              events_->menu_sound = NK_MENU_SOUND_ADJUST;
            }
        }

      if((state_->cursor == NK_OPTION_EXIT) &&
         ((stat & NK_STAT_BUTTON_MASK) != 0U))
        {
          state_->fade_out = 1U;
          events_->flags |= NK_UI_EVENT_ACCEPT;
        }

      state_->previous_stat = (u8)(stat & NK_DIR_MASK);
    }

  if((input_->commands & NK_COMMAND_CANCEL) != 0U)
    {
      state_->fade_out = 1U;
      events_->flags |= NK_UI_EVENT_CANCEL;
    }
}


void
nk_title_begin(nk_title_state *state_)
{
  state_->tick               = 0U;
  state_->attract_idle_ticks = 0U;
  state_->choice = NK_TITLE_ONE_PLAYER;
  state_->show_choice        = 0U;
  state_->edge_mask          = 0U;
  state_->talk_leave = NK_TITLE_LEAVE_IDLE;
  state_->button_push        = 0U;
  state_->fade_level         = 0U;
  state_->fade_out           = 0U;
  state_->done               = 0U;
  state_->cancelled          = 0U;
  state_->attract_requested  = 0U;
}


void
nk_title_step(nk_title_state        *state_,
              const nk_input_sample *input_,
              int                    controller_active_,
              int                    sound_enabled_,
              int                    voice_playing_,
              nk_ui_events          *events_)
{
  u8  direction_edge;
  int voice_now;

  nk_ui_events_clear(events_);
  if(state_->done)
    return;

  voice_now = voice_playing_;
  state_->tick++;
  if(controller_active_)
    {
      state_->attract_idle_ticks = 0U;
    }
  else
    {
      state_->attract_idle_ticks++;
      if(state_->attract_idle_ticks >= NK_TITLE_ATTRACT_TIMEOUT_TICKS)
        {
          state_->attract_requested = 1U;
          state_->done = 1U;
          return;
        }
    }

  if(state_->tick == NK_TITLE_VOICE_START_TICK)
    _nk_title_emit_voice(events_, 0, sound_enabled_, &voice_now);

  if(state_->tick == NK_TITLE_CHOICES_START_TICK)
    state_->show_choice = 1U;

  if(state_->button_push > 0U)
    state_->button_push--;

  if(state_->tick < NK_UI_FADE_LEVEL_MAX)
    {
      state_->fade_level = (u8)state_->tick;
    }
  else if(state_->fade_out)
    {
      if(state_->fade_level > 0U)
        {
          state_->fade_level--;
        }
      else
        {
          state_->done = 1U;
          return;
        }
    }

  if(state_->show_choice)
    {
      direction_edge = (u8)(input_->stat & state_->edge_mask);
      state_->edge_mask = (u8) ~input_->stat;

      if((direction_edge & NK_DIR_UP) != 0U)
        {
          if(state_->choice > NK_TITLE_ONE_PLAYER)
            {
              state_->choice--;
              state_->button_push = NK_TITLE_MOVE_PUSH_TICKS;
              events_->flags |= NK_UI_EVENT_MOVE;
              voice_now = false;
              _nk_title_emit_voice(events_,
                                   (s8)(state_->choice + 1U),
                                   sound_enabled_,
                                   &voice_now);
            }
        }

      if((direction_edge & NK_DIR_DOWN) != 0U)
        {
          if(state_->choice < NK_TITLE_OPTIONS)
            {
              state_->choice++;
              state_->button_push = NK_TITLE_MOVE_PUSH_TICKS;
              events_->flags |= NK_UI_EVENT_MOVE;
              voice_now = false;
              _nk_title_emit_voice(events_,
                                   (s8)(state_->choice + 1U),
                                   sound_enabled_,
                                   &voice_now);
            }
        }

      if((state_->talk_leave == NK_TITLE_LEAVE_IDLE) &&
         ((input_->stat & NK_STAT_BUTTON_MASK) != 0U))
        {
          state_->talk_leave = NK_TITLE_LEAVE_SELECTION_VOICE;
          state_->button_push = NK_TITLE_ACCEPT_PUSH_TICKS;
          state_->show_choice  = 0U;
          events_->flags      |= NK_UI_EVENT_ACCEPT;
        }
    }

  if(state_->talk_leave == NK_TITLE_LEAVE_SELECTION_VOICE)
    {
      if((!sound_enabled_) || (state_->choice == NK_TITLE_OPTIONS))
        {
          state_->talk_leave = NK_TITLE_LEAVE_FADE;
        }
      else if(!voice_now)
        {
          _nk_title_emit_voice(events_, 4, sound_enabled_, &voice_now);
          state_->talk_leave = NK_TITLE_LEAVE_EXIT_VOICE;
        }
    }

  if((state_->talk_leave == NK_TITLE_LEAVE_EXIT_VOICE) && (!voice_now))
    state_->talk_leave = NK_TITLE_LEAVE_FADE;

  if(state_->talk_leave == NK_TITLE_LEAVE_FADE)
    state_->fade_out = 1U;

  if((input_->commands & NK_COMMAND_CANCEL) != 0U)
    {
      state_->cancelled = 1U;
      state_->fade_out = 1U;
      events_->flags |= NK_UI_EVENT_CANCEL;
    }
}


int
nk_title_result(const nk_title_state *state_)
{
  if(!state_->done)
    return NK_TITLE_PENDING;

  if(state_->cancelled)
    return NK_TITLE_CANCELLED;

  if(state_->attract_requested)
    return NK_TITLE_ATTRACT;

  return (int)state_->choice;
}
