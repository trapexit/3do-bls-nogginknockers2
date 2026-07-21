#include "nk_select.h"

#define NK_SELECT_GRID_COLUMN_MASK       (3)
#define NK_SELECT_GRID_ROW_BIT           (4)
#define NK_SELECT_CURSOR_MASK            (7)
#define NK_SELECT_SCREAM_ATTEMPT_LIMIT   (60)
#define NK_SELECT_SCREAM_QUEUE_TICKS     (50)
#define NK_SELECT_SCREAM_INITIAL_TICKS   (80)
#define NK_SELECT_SCREAM_IDLE_TICKS_MIN  (90)
#define NK_SELECT_SCREAM_IDLE_TICKS_RANGE (80)
#define NK_SELECT_SCREAM_SUPPRESS_TICKS  (3000)
#define NK_SELECT_SCREAM_RETRIGGER_TICKS (200)
#define NK_SELECT_PICK_STEP_TICKS        (10)
#define NK_SELECT_SKIP_MIN               (8)
#define NK_SELECT_SKIP_RANGE             (10)
#define NK_SELECT_INITIAL_PICK_TICKS     (20)
#define NK_SELECT_LOCKED_PICK_TICKS      (150)

const u8    nk_select_scream_masks[NK_CHARACTER_COUNT] =
{
  0x03U,
  0x03U,
  0x03U,
  0x03U,
  0x03U,
  0x07U,
  0x07U,
  0x03U
};


static
void
_nk_select_start_scream(NkSelectState  *state_,
                        nk_rng         *rng_,
                        NkSelectEvents *events_)
{
  s32    character;
  s32    sound;
  int attempts;

  if(state_->remaining == 0)
    {
      return;
    }

  if(state_->pending_screamer < 0)
    {
      do
        {
          character = nk_rng_bounded(rng_, NK_CHARACTER_COUNT);
        }
      while(((state_->defeated_mask & (1L << character)) != 0) ||
            ((state_->remaining > 1) &&
             (state_->old_screamer == character)));
    }
  else
    {
      character = state_->pending_screamer;
      state_->pending_screamer = state_->queued_screamer;
      state_->queued_screamer = -1;
    }

  state_->screamer = character;

  attempts = NK_SELECT_SCREAM_ATTEMPT_LIMIT;
  do
    {
      sound = nk_rng_bounded(rng_, NK_SELECT_SCREAM_VARIANT_COUNT);
      attempts--;
    }
  while(((nk_select_scream_masks[character] & (1U << sound)) == 0U) &&
        (attempts > 0));

  if(attempts == 0)
    {
      state_->screamer = -1;
      return;
    }

  events_->scream_character = (s8)character;
  events_->scream_index = (s8)sound;
}


static
void
_nk_select_scream_step(NkSelectState  *state_,
                       nk_rng         *rng_,
                       int             scream_playing_,
                       NkSelectEvents *events_)
{
  if(state_->screamer < 0)
    {
      if(state_->scream_duration > 0)
        {
          state_->scream_duration--;
        }
      else
        {
          state_->scream_duration = NK_SELECT_SCREAM_RETRIGGER_TICKS;
          _nk_select_start_scream(state_, rng_, events_);
        }
    }
  else if(!scream_playing_)
    {
      state_->old_screamer = state_->screamer;
      state_->screamer = -1;
      if(state_->pending_screamer >= 0)
        {
          state_->scream_duration = NK_SELECT_SCREAM_QUEUE_TICKS;
        }
      else if((state_->control[0] == NK_CONTROL_COMPUTER) &&
              (state_->control[1] == NK_CONTROL_COMPUTER) &&
              (state_->locked[0]) &&
              (!state_->locked[1]))
        {
          /*
           * Player 2 still has an attract-mode pick to make.  Suppress the
           * ambient scream timer so its eventual popup can only be the
           * character it actually confirms.
           */
          state_->scream_duration = NK_SELECT_SCREAM_SUPPRESS_TICKS;
        }
      else
        {
          state_->scream_duration =
            NK_SELECT_SCREAM_IDLE_TICKS_MIN +
            nk_rng_bounded(rng_, NK_SELECT_SCREAM_IDLE_TICKS_RANGE);
        }
    }
}


static
void
_nk_select_queue_scream(NkSelectState *state_,
                        s32            character_)
{
  /* Give both confirmed players a turn, including simultaneous human locks. */
  if(state_->pending_screamer < 0)
    {
      state_->pending_screamer = character_;
    }
  else if(state_->queued_screamer < 0)
    {
      state_->queued_screamer = character_;
    }

  state_->scream_duration = NK_SELECT_SCREAM_QUEUE_TICKS;
}


static
void
_nk_select_human(NkSelectState         *state_,
                 int                    player_index_,
                 const nk_input_sample *input_,
                 NkSelectEvents        *events_)
{
  u8    edge;
  s32    old_cursor;

  if(state_->locked[player_index_])
    {
      return;
    }

  edge = (u8)(input_->stat
                 & (u8) ~state_->previous_stat[player_index_]);
  old_cursor = state_->cursor[player_index_];
  if((edge & NK_DIR_LEFT) != 0U)
    {
      state_->cursor[player_index_] =
        (state_->cursor[player_index_] & NK_SELECT_GRID_ROW_BIT)
        | ((state_->cursor[player_index_] - 1) & NK_SELECT_GRID_COLUMN_MASK);
    }

  if((edge & NK_DIR_RIGHT) != 0U)
    {
      state_->cursor[player_index_] =
        (state_->cursor[player_index_] & NK_SELECT_GRID_ROW_BIT)
        | ((state_->cursor[player_index_] + 1) & NK_SELECT_GRID_COLUMN_MASK);
    }

  if((edge & NK_DIR_UP) != 0U)
    {
      state_->cursor[player_index_] &= NK_SELECT_GRID_COLUMN_MASK;
    }

  if((edge & NK_DIR_DOWN) != 0U)
    {
      state_->cursor[player_index_] |= NK_SELECT_GRID_ROW_BIT;
    }

  if(state_->cursor[player_index_] != old_cursor)
    {
      events_->moved_mask |= (u8)(1U << player_index_);
    }

  if((input_->stat & NK_STAT_BUTTON_MASK) != 0U)
    {
      state_->locked[player_index_] = 1;
      events_->locked_mask |= (u8)(1U << player_index_);
    }

  state_->previous_stat[player_index_] =
    (u8)(input_->stat & NK_DIR_MASK);
}


static
void
_nk_select_computer(NkSelectState  *state_,
                    int             player_index_,
                    NkSelectEvents *events_)
{
  u8    character;

  if((state_->locked[player_index_]) || (state_->pick_time >= 0))
    {
      return;
    }

  state_->pick_time = NK_SELECT_PICK_STEP_TICKS;
  state_->cursor[player_index_]++;
  state_->cursor[player_index_] &= NK_SELECT_CURSOR_MASK;
  events_->moved_mask |= (u8)(1U << player_index_);
  state_->skip[player_index_]--;
  character = nk_cursor_to_character(state_->cursor[player_index_]);
  if((state_->skip[player_index_] <= 0) &&
     ((state_->defeated_mask & (1L << character)) == 0))
    {
      state_->locked[player_index_] = 1;
      events_->locked_mask |= (u8)(1U << player_index_);
    }
}


void
nk_select_begin(NkSelectState *state_,
                const nk_flow *flow_,
                nk_rng        *rng_)
{
  int index;

  for(index = 0; index < NK_PLAYER_COUNT; ++index)
    {
      state_->cursor[index] = flow_->selected_cursor[index];
      state_->locked[index] = flow_->locked[index];
      state_->control[index] = flow_->control[index];
      state_->previous_stat[index] = 0U;
      state_->skip[index] = NK_SELECT_SKIP_MIN + nk_rng_bounded(rng_, NK_SELECT_SKIP_RANGE);
    }

  state_->pick_time = NK_SELECT_INITIAL_PICK_TICKS;
  if((state_->control[0] == NK_CONTROL_HUMAN) && (state_->locked[0]))
    {
      state_->pick_time = NK_SELECT_LOCKED_PICK_TICKS;
    }

  state_->defeated_mask = flow_->defeated_mask;
  state_->remaining = flow_->remaining;
  state_->screamer = -1;
  state_->old_screamer = -1;
  state_->pending_screamer = -1;
  state_->queued_screamer = -1;
  state_->scream_duration = NK_SELECT_SCREAM_INITIAL_TICKS;
  if(((state_->control[0] == NK_CONTROL_COMPUTER) && (!state_->locked[0])) ||
     ((state_->control[1] == NK_CONTROL_COMPUTER) &&
         (!state_->locked[1])))
    {
      state_->scream_duration = NK_SELECT_SCREAM_SUPPRESS_TICKS;
    }

  state_->tick = 1U;
  state_->fade_level = 0U;
  state_->fade_in = 1U;
  state_->fade_out = 0U;
  state_->done = 0U;
  state_->cancelled = 0U;
}


void
nk_select_step(NkSelectState        *state_,
               const nk_input_sample input_[NK_PLAYER_COUNT],
               nk_rng               *rng_,
               int                   scream_playing_,
               NkSelectEvents       *events_)
{
  int index;

  events_->moved_mask = 0U;
  events_->locked_mask = 0U;
  events_->scream_character = -1;
  events_->scream_index = -1;
  if(state_->done)
    {
      return;
    }

  state_->tick++;
  state_->pick_time--;
  _nk_select_scream_step(state_, rng_, scream_playing_, events_);
  if(state_->fade_in)
    {
      if(state_->fade_level < NK_UI_FADE_LEVEL_MAX)
        {
          state_->fade_level++;
        }
      else
        {
          state_->fade_in = 0U;
        }
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

  for(index = 0; index < NK_PLAYER_COUNT; ++index)
    {
      if(state_->control[index] == NK_CONTROL_HUMAN)
        {
          _nk_select_human(state_, index, &input_[index], events_);
        }
      else if(state_->control[index] == NK_CONTROL_COMPUTER)
        {
          /*
           * Attract mode is the only two-computer selection.  Keep player 2
           * from selecting through player 1's confirmation scream so each
           * automatic pick has the same distinct popup as a human pick.
           */
          if((index == 1) &&
             (state_->control[0] == NK_CONTROL_COMPUTER) &&
             (state_->locked[0]) &&
             ((state_->screamer >= 0) ||
                 (state_->pending_screamer >= 0) ||
                 (state_->queued_screamer >= 0)))
            {
              continue;
            }

          _nk_select_computer(state_, index, events_);
        }

      if((events_->locked_mask & (1U << index)) != 0U)
        {
          _nk_select_queue_scream(
            state_,
            nk_cursor_to_character(state_->cursor[index])
            );
        }
    }

  if(((input_[0].commands & NK_COMMAND_CANCEL) != 0U) ||
     ((input_[1].commands & NK_COMMAND_CANCEL) != 0U))
    {
      state_->cancelled = 1U;
      state_->fade_in = 0U;
      state_->fade_out = 1U;
    }

  if((state_->locked[0]) && (state_->locked[1]) &&
     (state_->screamer < 0) &&
     (state_->pending_screamer < 0) &&
     (state_->queued_screamer < 0))
    {
      state_->fade_in = 0U;
      state_->fade_out = 1U;
    }
}


int
nk_select_result(const NkSelectState *state_)
{
  if(!state_->done)
    {
      return NK_SELECT_PENDING;
    }

  if(state_->cancelled)
    {
      return NK_SELECT_CANCELLED;
    }

  return NK_SELECT_COMPLETE;
}
