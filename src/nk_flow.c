#include "nk_flow.h"

static const u8    g_CHARACTER_BY_CURSOR[NK_CHARACTER_COUNT] =
{
  NK_CHARACTER_KLUBBOR,
  NK_CHARACTER_FETUS,
  NK_CHARACTER_GURDIP,
  NK_CHARACTER_SINAMMON,
  NK_CHARACTER_BUDDY,
  NK_CHARACTER_GONZOLES,
  NK_CHARACTER_ED,
  NK_CHARACTER_HENRY
};

static const char *g_CHARACTER_NAMES[NK_CHARACTER_COUNT] =
{
  "klubbor",
  "fetus",
  "henry",
  "gurdip",
  "ed",
  "sinammon",
  "buddy",
  "gonzoles"
};

u8
nk_cursor_to_character(s32    cursor_)
{
  return g_CHARACTER_BY_CURSOR[
    (u32)cursor_ & NK_CHARACTER_INDEX_MASK
  ];
}


const
char *
nk_character_name(u32    character_)
{
  if(character_ >= NK_CHARACTER_COUNT)
    {
      return "unknown";
    }

  return g_CHARACTER_NAMES[character_];
}


void
nk_flow_init(nk_flow *flow_)
{
  nk_options_defaults(&flow_->options);
  flow_->scene = NK_SCENE_LOGO;
  flow_->mode = NK_MODE_ONE_PLAYER;
  flow_->control[0] = NK_CONTROL_HUMAN;
  flow_->control[1] = NK_CONTROL_COMPUTER;
  flow_->selected_cursor[0] = 0U;
  flow_->selected_cursor[1] = 3U;
  flow_->selected_character[0] = nk_cursor_to_character(0U);
  flow_->selected_character[1] = nk_cursor_to_character(3U);
  flow_->locked[0] = 0U;
  flow_->locked[1] = 0U;
  flow_->defeated_mask = 0U;
  flow_->remaining = NK_CHARACTER_COUNT;
  flow_->winner = 0U;
  flow_->ending_number = 0U;
}


void
nk_flow_cinematic_complete(nk_flow *flow_)
{
  if(flow_->scene == NK_SCENE_LOGO)
    {
      flow_->scene = NK_SCENE_CREDITS;
    }
  else if(flow_->scene == NK_SCENE_CREDITS)
    {
      flow_->scene = NK_SCENE_PORT_CREDIT;
    }
  else if((flow_->scene == NK_SCENE_PORT_CREDIT)
          || (flow_->scene == NK_SCENE_ENDING))
    {
      flow_->scene = NK_SCENE_TITLE;
    }
}


void
nk_flow_cinematic_skip(nk_flow *flow_)
{
  if((flow_->scene == NK_SCENE_LOGO)
     || (flow_->scene == NK_SCENE_CREDITS)
     || (flow_->scene == NK_SCENE_PORT_CREDIT)
     || (flow_->scene == NK_SCENE_ENDING))
    {
      flow_->scene = NK_SCENE_TITLE;
    }
}


void
nk_flow_title_complete(nk_flow *flow_,
                       int      title_result_)
{
  if(title_result_ == NK_TITLE_CANCELLED)
    {
      flow_->scene = NK_SCENE_EXIT;
      return;
    }

  if(title_result_ == NK_TITLE_OPTIONS)
    {
      flow_->scene = NK_SCENE_OPTIONS;
      return;
    }

  if((title_result_ != NK_TITLE_ONE_PLAYER) && (title_result_ != NK_TITLE_TWO_PLAYER)
     && (title_result_ != NK_TITLE_ATTRACT))
    {
      return;
    }

  if(title_result_ == NK_TITLE_ATTRACT)
    {
      flow_->mode = NK_MODE_ATTRACT;
      flow_->control[0] = NK_CONTROL_COMPUTER;
      flow_->control[1] = NK_CONTROL_COMPUTER;
    }
  else
    {
      flow_->mode = (title_result_ == NK_TITLE_ONE_PLAYER)
            ? NK_MODE_ONE_PLAYER : NK_MODE_TWO_PLAYER;
      flow_->control[0] = NK_CONTROL_HUMAN;
      flow_->control[1] = (flow_->mode == NK_MODE_ONE_PLAYER)
            ? NK_CONTROL_COMPUTER : NK_CONTROL_HUMAN;
    }

  flow_->defeated_mask = 0U;
  flow_->remaining = NK_CHARACTER_COUNT;
  flow_->selected_cursor[0] = 0U;
  flow_->selected_cursor[1] = 3U;
  flow_->selected_character[0] = nk_cursor_to_character(0U);
  flow_->selected_character[1] = nk_cursor_to_character(3U);
  flow_->locked[0] = 0U;
  flow_->locked[1] = 0U;
  flow_->ending_number = 0U;
  flow_->scene = NK_SCENE_SELECT;
}


void
nk_flow_options_complete(nk_flow          *flow_,
                         const nk_options *options_)
{
  flow_->options = *options_;
  flow_->scene = NK_SCENE_TITLE;
}


void
nk_flow_selection_complete(nk_flow *flow_,
                           s32      player_zero_cursor_,
                           s32      player_one_cursor_)
{
  if(flow_->scene != NK_SCENE_SELECT)
    {
      return;
    }

  flow_->selected_cursor[0] = (s32)((u32)player_zero_cursor_ & 7U);
  flow_->selected_cursor[1] = (s32)((u32)player_one_cursor_ & 7U);
  flow_->selected_character[0] = nk_cursor_to_character(player_zero_cursor_);
  flow_->selected_character[1] = nk_cursor_to_character(player_one_cursor_);
  flow_->scene = NK_SCENE_MATCH;
}


s32
nk_flow_match_difficulty(const nk_flow *flow_,
                         nk_rng        *rng_)
{
  if(flow_->mode == NK_MODE_ATTRACT)
    {
      return NK_ATTRACT_DIFFICULTY_MIN + nk_rng_bounded(
        rng_,
        NK_DIFFICULTY_MAX - NK_ATTRACT_DIFFICULTY_MIN + 1
        );
    }

  return flow_->options.difficulty;
}


void
nk_flow_match_complete(nk_flow *flow_,
                       s32      winner_)
{
  s32    loser;

  if((flow_->scene != NK_SCENE_MATCH) || (winner_ < 0) || (winner_ > 1))
    {
      return;
    }

  flow_->winner = winner_;
  if(flow_->mode == NK_MODE_ATTRACT)
    {
      flow_->scene = NK_SCENE_TITLE;
      return;
    }

  loser = winner_ ^ 1;

  if(flow_->control[loser] != NK_CONTROL_HUMAN)
    {
      flow_->locked[0] = 1U;
      flow_->locked[1] = 0U;
      flow_->defeated_mask |= 1 << flow_->selected_character[loser];
      flow_->remaining--;
    }

  flow_->locked[loser] = 0U;
  flow_->locked[winner_] = 1U;

  if(flow_->remaining == 0U)
    {
      flow_->ending_number = (s32)flow_->selected_character[0] + 1;
      flow_->scene = NK_SCENE_ENDING;
    }
  else
    {
      flow_->scene = NK_SCENE_SELECT;
    }
}


void
nk_flow_abort_to_title(nk_flow *flow_)
{
  if((flow_->scene == NK_SCENE_SELECT) || (flow_->scene == NK_SCENE_MATCH))
    {
      flow_->scene = NK_SCENE_TITLE;
    }
}
