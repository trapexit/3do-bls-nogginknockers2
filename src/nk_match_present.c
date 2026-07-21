#include "nk_match_present.h"

#include "stddef.h"
#include "string.h"

#define NK_MATCH_PRESENT_MAX_DELTA \
  (64 * NK_SUBPIXEL_ONE)
#define NK_MATCH_PRESENT_COORDINATE(whole_, fraction_)        \
  ((whole_) * NK_SUBPIXEL_ONE +                               \
   (s32)((u32)(fraction_) & NK_SUBPIXEL_MASK))


static
void
_nk_match_present_position(
  const NkMatchPresentationPosition *previous_,
  const NkMatchPresentationPosition *current_,
  u32                               phase_,
  int                               continuous_,
  s32                              *x_,
  s32                              *y_);

static
bool
_nk_match_present_prepare_effect_range(NkMatchPresentation *presentation_,
                                       NkGame              *game_,
                                       u32                  first_,
                                       u32                  count_,
                                       int                  materialize_)
{
  u32    sticky_indices[NK_EFFECT_POOL_HALF];
  u32    sticky_count;
  u32    position;
  u8    *draw_modes;

  draw_modes = materialize_
               ? presentation_->effect_draw_modes : NULL;

  if(!nk_effect_prepare_draw_range(
       &game_->effects,
       first_,
       count_,
       game_->sticky_blood,
       &game_->rng,
       draw_modes,
       sticky_indices,
       &sticky_count))
    {
      return false;
    }

  for(position = 0U; position < sticky_count; ++position)
    {
      if(!nk_sticky_queue_effect(
           &presentation_->sticky_pending,
           &game_->effects.effects[sticky_indices[position]]))
        {
          return false;
        }
    }

  return true;
}


static
bool
_nk_match_present_prepare_pain(NkGame            *game_,
                               NkGamePlayer      *owner_,
                               const NkAnimFrame *frame_,
                               u32                bank_index_,
                               s32                origin_x_,
                               s32                origin_y_,
                               u8                 facing_,
                               int                paused_)
{
  const NkAnimImageRef *image;
  const NkAnimImageRef *pain_image;
  const NkAnimImageSize *base_size;
  const NkAnimImageSize *pain_size;
  const NkAnimPainLayout *layout;
  NkAnimProjectedImage projected;
  s32    pain_x;
  s32    pain_y;
  u32    image_index;
  u32    pain_index;

  if((frame_ == NULL) || (owner_->pain <= 0))
    {
      return true;
    }

  for(image_index = 0U;
      image_index < frame_->image_count;
      ++image_index)
    {
      image = nk_anim_frame_image(frame_, image_index);
      if(image == NULL)
        {
          return false;
        }

      layout = nk_anim_pain_layout(bank_index_, image->image);
      if(layout == NULL)
        {
          continue;
        }

      nk_anim_project_image(
        image,
        origin_x_,
        origin_y_,
        -80,
        facing_,
        &projected
        );
      base_size = nk_anim_image_size(bank_index_, projected.image);
      if(base_size == NULL)
        {
          return false;
        }

      for(pain_index = 0U;
          pain_index < layout->image_count;
          ++pain_index)
        {
          pain_image = nk_anim_pain_layout_image(layout, pain_index);
          if(pain_image == NULL)
            {
              return false;
            }

          if(((pain_image->orientation >> 2) & 3U) !=
             (u8)owner_->pain)
            {
              continue;
            }

          pain_size = nk_anim_pain_image_size(
            bank_index_,
            pain_image->image
            );
          if(pain_size == NULL)
            {
              return false;
            }

          if((projected.orientation & 2U) != 0U)
            {
              pain_x = projected.x + pain_image->x_flipped;
            }
          else
            {
              pain_x = projected.x + pain_image->x_normal;
            }

          pain_y = pain_image->y;
          if((projected.orientation & 1U) != 0U)
            {
              pain_y = -pain_y + base_size->height -
                       pain_size->height;
            }

          pain_y += projected.y;
          (void)nk_game_try_pain_drip(
            game_,
            owner_,
            paused_,
            pain_x,
            pain_y,
            pain_size->width,
            pain_size->height
            );
        }
    }

  return true;
}


static
bool
_nk_match_present_prepare_player(NkGame       *game_,
                                 NkGamePlayer *player_,
                                 int           paused_)
{
  const NkAnimFrame *frame;

  if(player_->pain <= 0)
    {
      return true;
    }

  frame = nk_game_player_frame(player_);
  if(frame == NULL)
    {
      return true;
    }

  if(player_->pain > 3)
    {
      player_->pain = 3;
    }

  return _nk_match_present_prepare_pain(
    game_,
    player_,
    frame,
    player_->animation_bank,
    player_->x,
    player_->y,
    player_->facing,
    paused_
    );
}


static
bool
_nk_match_present_prepare_projectiles(NkGame       *game_,
                                      NkGamePlayer *owner_,
                                      int           paused_)
{
  NkProjectile *projectile;
  const NkAnimFrame *frame;
  u32    index;

  if(owner_->pain <= 0)
    {
      return true;
    }

  for(index = 0U; index < NK_GAME_PROJECTILE_COUNT; ++index)
    {
      projectile = &owner_->projectiles[index];
      if(projectile->active == 0U)
        {
          continue;
        }

      frame = nk_anim_cursor_frame(&projectile->animation);
      if(!_nk_match_present_prepare_pain(
           game_,
           owner_,
           frame,
           owner_->animation_bank,
           projectile->x,
           projectile->y,
           projectile->facing,
           paused_))
        {
          return false;
        }
    }

  return true;
}


static
bool
_nk_match_present_prepare_actors(NkGame *game_,
                                 int     paused_)
{
  if(game_->ready == 0)
    {
      return true;
    }

  if((game_->chopper.pain <= 0) && (game_->ball.pain <= 0) &&
     (game_->players[0].pain <= 0) && (game_->players[1].pain <= 0))
    {
      return true;
    }

  if((game_->chopper.draw_me != 0U) &&
     (!_nk_match_present_prepare_player(game_, &game_->chopper, paused_)))
    {
      return false;
    }

  if((game_->ball.freeze == 0) && (game_->ball.draw_me != 0U) &&
     (!_nk_match_present_prepare_player(game_, &game_->ball, paused_)))
    {
      return false;
    }

  if(game_->draw_priority != 0)
    {
      if((!_nk_match_present_prepare_player(
            game_,
            &game_->players[0],
            paused_)) ||
         (!_nk_match_present_prepare_player(
            game_,
            &game_->players[1],
            paused_)))
        {
          return false;
        }
    }
  else
    {
      if((!_nk_match_present_prepare_player(
            game_,
            &game_->players[1],
            paused_)) ||
         (!_nk_match_present_prepare_player(
            game_,
            &game_->players[0],
            paused_)))
        {
          return false;
        }
    }

  if((game_->ball.freeze != 0) && (game_->ball.draw_me != 0U) &&
     (!_nk_match_present_prepare_player(game_, &game_->ball, paused_)))
    {
      return false;
    }

  if((!_nk_match_present_prepare_projectiles(
        game_,
        &game_->players[0],
        paused_)) ||
     (!_nk_match_present_prepare_projectiles(
        game_,
        &game_->players[1],
        paused_)))
    {
      return false;
    }

  return true;
}


void
nk_match_present_reset(NkMatchPresentation *presentation_)
{
  if(presentation_ != NULL)
    {
      memset(presentation_, 0, sizeof(*presentation_));
      presentation_->render_phase = NK_MATCH_PRESENT_PHASE_ONE;
    }
}


bool
nk_match_present_prepare_tick(NkMatchPresentation *presentation_,
                              NkGame              *game_,
                              NkDialogueState     *dialogue_,
                              int                  outcome_started_,
                              int                  talking_,
                              int                  background_hidden_,
                              int                  quit_,
                              int                  paused_,
                              int                  materialize_)
{
  u8    dialogue_mode;

  if((presentation_ == NULL) || (game_ == NULL) ||
     ((dialogue_ == NULL) && (talking_) && (!background_hidden_) && (!quit_)))
    {
      return false;
    }

  if(materialize_)
    {
      presentation_->prepared = 0U;
    }

  presentation_->outcome_began = 0U;
  nk_game_prepare_presentation(game_);
  if((game_->game_state == NK_GAME_STATE_MATCH_COMPLETE) && (!outcome_started_))
    {
      if(dialogue_ == NULL)
        {
          return false;
        }

      dialogue_mode = game_->outcome_dialogue_kind
                      == NK_GAME_DIALOGUE_LOSS
            ? NK_DIALOGUE_MODE_LOSS : NK_DIALOGUE_MODE_WIN;
      if(!nk_dialogue_begin_outcome(
           dialogue_,
           dialogue_mode,
           game_->outcome_dialogue_index))
        {
          return false;
        }

      presentation_->outcome_began = 1U;
    }

  if(materialize_)
    {
      if(dialogue_ != NULL)
        {
          presentation_->dialogue_snapshot = *dialogue_;
        }
      else
        {
          memset(
            &presentation_->dialogue_snapshot,
            0,
            sizeof(presentation_->dialogue_snapshot)
            );
        }
    }

  if((!_nk_match_present_prepare_effect_range(
        presentation_,
        game_,
        0U,
        NK_EFFECT_POOL_HALF,
        materialize_)) ||
     (!_nk_match_present_prepare_actors(game_, paused_)))
    {
      return false;
    }

  if((dialogue_ != NULL) && (talking_) && (!background_hidden_) && (!quit_))
    {
      nk_dialogue_present(dialogue_, &game_->rng);
    }

  if(!_nk_match_present_prepare_effect_range(
       presentation_,
       game_,
       NK_EFFECT_POOL_HALF,
       NK_EFFECT_POOL_HALF,
       materialize_))
    {
      return false;
    }

  if(!presentation_->interpolation_initialized)
    {
      presentation_->interpolation_initialized = 1U;
    }

  if(materialize_)
    {
      presentation_->prepared_tick = game_->tick;
      presentation_->prepared = 1U;
    }

  return true;
}


bool
nk_match_present_prepare_frame(NkMatchPresentation *presentation_,
                               NkGame              *game_,
                               NkDialogueState     *dialogue_,
                               int                  outcome_started_,
                               int                  talking_,
                               int                  background_hidden_,
                               int                  quit_,
                               int                  paused_)
{
  return nk_match_present_prepare_tick(
    presentation_,
    game_,
    dialogue_,
    outcome_started_,
    talking_,
    background_hidden_,
    quit_,
    paused_,
    true
    );
}


bool
nk_match_present_prepare(NkMatchPresentation *presentation_,
                         NkGame              *game_,
                         int                  paused_)
{
  if(presentation_ != NULL)
    {
      nk_sticky_queue_reset(&presentation_->sticky_pending);
    }

  return nk_match_present_prepare_frame(
    presentation_,
    game_,
    NULL,
    true,
    false,
    true,
    false,
    paused_
    );
}


void
nk_match_present_set_phase(NkMatchPresentation *presentation_,
                           u32                   phase_)
{
  if(presentation_ == NULL)
    {
      return;
    }

  presentation_->render_phase = phase_ > NK_MATCH_PRESENT_PHASE_ONE
                                ? NK_MATCH_PRESENT_PHASE_ONE
                                : phase_;
}


void
nk_match_present_player_position(
  const NkMatchPresentation *presentation_,
  const NkGamePlayer         *player_,
  s32                       *x_,
  s32                       *y_)
{
  NkMatchPresentationPosition current;
  NkMatchPresentationPosition previous;

  if((presentation_ == NULL) || (!presentation_->interpolation_initialized) ||
     (player_ == NULL) ||
     (x_ == NULL) || (y_ == NULL))
    {
      return;
    }

  previous.x = player_->presentation_previous_x;
  previous.y = player_->presentation_previous_y;
  current.x = NK_MATCH_PRESENT_COORDINATE(player_->x, player_->fraction_x);
  current.y = NK_MATCH_PRESENT_COORDINATE(player_->y, player_->fraction_y);
  _nk_match_present_position(
    &previous,
    &current,
    presentation_->render_phase,
    true,
    x_,
    y_
    );
}


void
nk_match_present_ball_position(const NkMatchPresentation *presentation_,
                               const NkGamePlayer         *ball_,
                               s32                        *x_,
                               s32                        *y_)
{
  NkMatchPresentationPosition current;
  NkMatchPresentationPosition previous;
  int continuous;

  if((presentation_ == NULL) || (!presentation_->interpolation_initialized) ||
     (ball_ == NULL) ||
     (x_ == NULL) || (y_ == NULL))
    {
      return;
    }

  previous.x = ball_->presentation_previous_x;
  previous.y = ball_->presentation_previous_y;
  current.x = NK_MATCH_PRESENT_COORDINATE(ball_->x, ball_->fraction_x);
  current.y = NK_MATCH_PRESENT_COORDINATE(ball_->y, ball_->fraction_y);
  continuous = ball_->presentation_draw_me && ball_->draw_me &&
    ((ball_->presentation_frozen == (u8)(ball_->freeze != 0)) ||
     (ball_->presentation_frozen && (ball_->freeze == 0)));
  _nk_match_present_position(
    &previous,
    &current,
    presentation_->render_phase,
    continuous,
    x_,
    y_
    );
}


void
nk_match_present_chopper_position(const NkMatchPresentation *presentation_,
                                  const NkGamePlayer         *chopper_,
                                  s32                        *x_,
                                  s32                        *y_)
{
  NkMatchPresentationPosition current;
  NkMatchPresentationPosition previous;
  int continuous;

  if((presentation_ == NULL) || (!presentation_->interpolation_initialized) ||
     (chopper_ == NULL) ||
     (x_ == NULL) || (y_ == NULL))
    {
      return;
    }

  previous.x = chopper_->presentation_previous_x;
  previous.y = chopper_->presentation_previous_y;
  current.x = NK_MATCH_PRESENT_COORDINATE(
    chopper_->x,
    chopper_->fraction_x
    );
  current.y = NK_MATCH_PRESENT_COORDINATE(
    chopper_->y,
    chopper_->fraction_y
    );
  continuous = chopper_->presentation_draw_me && chopper_->draw_me;
  _nk_match_present_position(
    &previous,
    &current,
    presentation_->render_phase,
    continuous,
    x_,
    y_
    );
}


void
nk_match_present_projectile_position(
  const NkMatchPresentation *presentation_,
  const NkGamePlayer         *owner_,
  u32                        projectile_index_,
  s32                       *x_,
  s32                       *y_)
{
  NkMatchPresentationPosition current;
  NkMatchPresentationPosition previous;
  const NkProjectile *projectile;

  if((presentation_ == NULL) || (!presentation_->interpolation_initialized) ||
     (owner_ == NULL) ||
     (projectile_index_ >= NK_GAME_PROJECTILE_COUNT) ||
     (x_ == NULL) || (y_ == NULL))
    {
      return;
    }

  projectile = &owner_->projectiles[projectile_index_];
  previous.x = owner_->projectile_previous_x[projectile_index_];
  previous.y = owner_->projectile_previous_y[projectile_index_];
  current.x = NK_MATCH_PRESENT_COORDINATE(
    projectile->x,
    projectile->fraction_x
    );
  current.y = NK_MATCH_PRESENT_COORDINATE(
    projectile->y,
    projectile->fraction_y
    );
  _nk_match_present_position(
    &previous,
    &current,
    presentation_->render_phase,
    (projectile->active != 0U) &&
    (owner_->projectile_interpolation_ready[projectile_index_] != 0U),
    x_,
    y_
    );
}


static
void
_nk_match_present_position(
  const NkMatchPresentationPosition *previous_,
  const NkMatchPresentationPosition *current_,
  u32                               phase_,
  int                               continuous_,
  s32                              *x_,
  s32                              *y_)
{
  s32    delta;
  s32    fixed;
  u32    magnitude;
  u32    quotient;
  u32    scaled;

  if((!continuous_) || (phase_ >= NK_MATCH_PRESENT_PHASE_ONE))
    {
      fixed = current_->x;
    }
  else
    {
      delta = current_->x - previous_->x;
      magnitude = delta < 0 ? 0U - (u32)delta : (u32)delta;
      if(magnitude > (u32)NK_MATCH_PRESENT_MAX_DELTA)
        {
          fixed = current_->x;
        }
      else
        {
          scaled = (magnitude * (phase_ >> NK_SUBPIXEL_SHIFT)) >> NK_SUBPIXEL_SHIFT;
          fixed = delta < 0
                  ? previous_->x - (s32)scaled
                  : previous_->x + (s32)scaled;
        }
    }

  if(fixed >= 0)
    {
      *x_ = (s32)((u32)fixed >> NK_SUBPIXEL_SHIFT);
    }
  else
    {
      magnitude = 0U - (u32)fixed;
      quotient = magnitude >> NK_SUBPIXEL_SHIFT;
      if((magnitude & NK_SUBPIXEL_MASK) != 0U)
        {
          quotient++;
        }

      *x_ = -(s32)quotient;
    }

  if((!continuous_) || (phase_ >= NK_MATCH_PRESENT_PHASE_ONE))
    {
      fixed = current_->y;
    }
  else
    {
      delta = current_->y - previous_->y;
      magnitude = delta < 0 ? 0U - (u32)delta : (u32)delta;
      if(magnitude > (u32)NK_MATCH_PRESENT_MAX_DELTA)
        {
          fixed = current_->y;
        }
      else
        {
          scaled = (magnitude * (phase_ >> NK_SUBPIXEL_SHIFT)) >> NK_SUBPIXEL_SHIFT;
          fixed = delta < 0
                  ? previous_->y - (s32)scaled
                  : previous_->y + (s32)scaled;
        }
    }

  if(fixed >= 0)
    {
      *y_ = (s32)((u32)fixed >> NK_SUBPIXEL_SHIFT);
    }
  else
    {
      magnitude = 0U - (u32)fixed;
      quotient = magnitude >> NK_SUBPIXEL_SHIFT;
      if((magnitude & NK_SUBPIXEL_MASK) != 0U)
        {
          quotient++;
        }

      *y_ = -(s32)quotient;
    }
}
