#include "nk_cinema.h"

#include "nk_math.h"

#include "stddef.h"
#include "string.h"

#define NK_CINEMA_FADE_EVENT_TICKS (33)
#define NK_CINEMA_FADE_LEVEL_MAX   (31U)

const
NkCinemaBank *
nk_cinema_bank(u8    bank_index_)
{
  if(bank_index_ >= NK_CINEMA_BANK_COUNT)
    {
      return NULL;
    }

  return &nk_cinema_banks[bank_index_];
}


static
const
NkCinemaSprite *
_nk_cinema_sprite_def(const NkCinemaState *state_,
                      u8                   sprite_index_)
{
  const NkCinemaBank *bank;

  bank = nk_cinema_bank(state_->bank_index);
  if((bank == NULL) || (sprite_index_ >= NK_CINEMA_SPRITES_PER_BANK))
    {
      return NULL;
    }

  return &nk_cinema_sprites[bank->first_sprite + sprite_index_];
}


const
NkCinemaFrame *
nk_cinema_active_frame(const NkCinemaState *state_,
                       u8                   active_index_)
{
  const NkCinemaActiveSprite *active;
  const NkCinemaSprite *sprite;

  if((state_ == NULL) || (!state_->valid) ||
     (active_index_ >= NK_CINEMA_ACTIVE_SPRITE_COUNT))
    {
      return NULL;
    }

  active = &state_->sprites[active_index_];
  if(!active->active)
    {
      return NULL;
    }

  sprite = _nk_cinema_sprite_def(state_, active->sprite_index);
  if((sprite == NULL) || (active->frame_index >= sprite->frame_count))
    {
      return NULL;
    }

  return &nk_cinema_frames[sprite->first_frame + active->frame_index];
}


const
NkCinemaImageRef *
nk_cinema_frame_image(const NkCinemaFrame *frame_,
                      u8                   image_index_)
{
  if((frame_ == NULL) || (image_index_ >= frame_->image_count))
    {
      return NULL;
    }

  return &nk_cinema_image_refs[frame_->first_image + image_index_];
}


const
char *
nk_cinema_current_text(const NkCinemaState *state_)
{
  const NkCinemaBank *bank;

  if((state_ == NULL) || (state_->current_text < 0))
    {
      return NULL;
    }

  bank = nk_cinema_bank(state_->bank_index);
  if((bank == NULL) || (state_->current_text >= NK_CINEMA_TEXTS_PER_BANK))
    {
      return NULL;
    }

  return nk_cinema_texts[bank->first_text + state_->current_text];
}


bool
nk_cinema_begin(NkCinemaState *state_,
                u8             bank_index_)
{
  if((state_ == NULL) || (nk_cinema_bank(bank_index_) == NULL))
    {
      return false;
    }

  memset(state_, 0, sizeof(*state_));
  state_->bank_index = bank_index_;
  state_->current_background = -1;
  state_->current_text = -1;
  state_->pending_sound = -1;
  state_->pending_text = -1;
  state_->valid = 1U;
  return true;
}


static
void
_nk_cinema_clear_sprites(NkCinemaState *state_)
{
  int index;

  for(index = 0;
      index < NK_CINEMA_ACTIVE_SPRITE_COUNT;
      ++index)
    {
      state_->sprites[index].active = 0U;
    }
}


static
void
_nk_cinema_start_sprite(NkCinemaState       *state_,
                        const NkCinemaEvent *event_)
{
  NkCinemaActiveSprite *active;
  const NkCinemaSprite *sprite;
  const NkCinemaFrame *frame;
  u8    sprite_index;
  int index;

  sprite_index = (u8)(event_->data & NK_CINEMA_SPRITE_INDEX_MASK);
  sprite = _nk_cinema_sprite_def(state_, sprite_index);
  if((sprite == NULL) || (sprite->frame_count == 0U))
    {
      return;
    }

  for(index = 0;
      index < NK_CINEMA_ACTIVE_SPRITE_COUNT;
      ++index)
    {
      active = &state_->sprites[index];
      if(!active->active)
        {
          active->x = event_->x;
          active->y = event_->y;
          active->fraction_x = 0;
          active->fraction_y = 0;
          active->active = 1U;
          active->sprite_index = sprite_index;
          active->frame_index = 0U;
          active->loop = (u8)((event_->data & NK_CINEMA_SPRITE_LOOP_FLAG) != 0U);
          frame = &nk_cinema_frames[sprite->first_frame];
          active->remaining = frame->duration_100hz;
          return;
        }
    }
}


static
void
_nk_cinema_process_event(NkCinemaState       *state_,
                         const NkCinemaEvent *event_)
{
  switch(event_->type)
    {
    case NK_CINEMA_EVENT_DELAY:
      state_->remaining = event_->x;
      break;
    case NK_CINEMA_EVENT_FADE:
      state_->fade_active = 1U;
      state_->remaining = NK_CINEMA_FADE_EVENT_TICKS;
      break;
    case NK_CINEMA_EVENT_BACKGROUND:
      state_->current_background = event_->data;
      state_->current_text = -1;
      _nk_cinema_clear_sprites(state_);
      break;
    case NK_CINEMA_EVENT_START_SPRITE:
      _nk_cinema_start_sprite(state_, event_);
      break;
    case NK_CINEMA_EVENT_PLAY_SOUND:
      state_->pending_sound = event_->data;
      break;
    case NK_CINEMA_EVENT_SHOW_TEXT:
      state_->current_text = event_->data;
      state_->pending_text = event_->data;
      break;
    default:
      /* Type 4 is present in the format but has no release-player action. */
      break;
    }
}


static
void
_nk_cinema_tick_sprite(NkCinemaState        *state_,
                       NkCinemaActiveSprite *active_)
{
  const NkCinemaSprite *sprite;
  const NkCinemaFrame *frame;

  frame = nk_cinema_active_frame(
    state_,
    (u8)(active_ - state_->sprites)
    );
  if(frame == NULL)
    {
      active_->active = 0U;
      return;
    }

  active_->fraction_x = nk_wrap_add(
    active_->fraction_x,
    frame->delta_x_256
    );
  active_->x = nk_wrap_add(
    active_->x,
    nk_floor_shift_right(active_->fraction_x, NK_SUBPIXEL_SHIFT)
    );
  active_->fraction_x = (s32)((u32)active_->fraction_x & NK_SUBPIXEL_MASK);
  active_->fraction_y = nk_wrap_add(
    active_->fraction_y,
    frame->delta_y_256
    );
  active_->y = nk_wrap_add(
    active_->y,
    nk_floor_shift_right(active_->fraction_y, NK_SUBPIXEL_SHIFT)
    );
  active_->fraction_y = (s32)((u32)active_->fraction_y & NK_SUBPIXEL_MASK);

  if(active_->remaining > 0)
    {
      active_->remaining--;
      return;
    }

  active_->frame_index++;
  sprite = _nk_cinema_sprite_def(state_, active_->sprite_index);
  if((sprite == NULL) || (active_->frame_index >= sprite->frame_count))
    {
      if(!active_->loop)
        {
          active_->active = 0U;
          return;
        }

      active_->frame_index = 0U;
    }

  frame = nk_cinema_active_frame(
    state_,
    (u8)(active_ - state_->sprites)
    );
  if(frame == NULL)
    {
      active_->active = 0U;
    }
  else
    {
      active_->remaining = frame->duration_100hz;
    }
}


void
nk_cinema_tick(NkCinemaState *state_)
{
  const NkCinemaBank *bank;
  const NkCinemaEvent *event;
  int index;

  if((state_ == NULL) || (!state_->valid) || (state_->completed))
    {
      return;
    }

  state_->pending_sound = -1;
  state_->pending_text = -1;
  bank = nk_cinema_bank(state_->bank_index);
  while(state_->remaining == 0)
    {
      if(state_->event_index >= bank->event_count)
        {
          state_->completed = 1U;
          state_->current_background = -1;
          return;
        }

      event = &nk_cinema_events[bank->first_event + state_->event_index];
      state_->event_index++;
      _nk_cinema_process_event(state_, event);
    }

  state_->remaining--;

  for(index = 0;
      index < NK_CINEMA_ACTIVE_SPRITE_COUNT;
      ++index)
    {
      if(state_->sprites[index].active)
        {
          _nk_cinema_tick_sprite(state_, &state_->sprites[index]);
        }
    }

  if(state_->fade_active)
    {
      if(!state_->fade_state)
        {
          state_->fade_level++;
          if(state_->fade_level == NK_CINEMA_FADE_LEVEL_MAX)
            {
              state_->fade_state = 1U;
              state_->fade_active = 0U;
            }
        }
      else
        {
          state_->fade_level--;
          if(state_->fade_level == 0U)
            {
              state_->fade_state = 0U;
              state_->fade_active = 0U;
            }
        }
    }
}


bool
nk_cinema_data_valid(void)
{
  const NkCinemaBank *bank;
  const NkCinemaEvent *event;
  const NkCinemaSprite *sprite;
  const NkCinemaFrame *frame;
  const NkCinemaImageRef *image;
  u32    bank_index;
  u32    event_index;
  u32    frame_index;
  u32    image_index;
  u32    sprite_index;

  for(bank_index = 0U;
      bank_index < NK_CINEMA_BANK_COUNT;
      ++bank_index)
    {
      bank = &nk_cinema_banks[bank_index];
      if(((u32)bank->first_event + bank->event_count
          > nk_cinema_event_count) ||
         ((u32)bank->first_sprite + NK_CINEMA_SPRITES_PER_BANK
             > NK_CINEMA_BANK_COUNT * NK_CINEMA_SPRITES_PER_BANK) ||
         (bank->first_text + NK_CINEMA_TEXTS_PER_BANK
             > NK_CINEMA_BANK_COUNT * NK_CINEMA_TEXTS_PER_BANK))
        {
          return false;
        }

      for(event_index = 0U;
          event_index < bank->event_count;
          ++event_index)
        {
          event = &nk_cinema_events[bank->first_event + event_index];
          switch(event->type)
            {
            case NK_CINEMA_EVENT_DELAY:
              if(event->x < 0)
                {
                  return false;
                }

              break;
            case NK_CINEMA_EVENT_FADE:
            case NK_CINEMA_EVENT_RELEASE_SPRITE:
              break;
            case NK_CINEMA_EVENT_BACKGROUND:
              if(event->data >= bank->image_count)
                {
                  return false;
                }

              break;
            case NK_CINEMA_EVENT_START_SPRITE:
              sprite_index = (u32)(event->data & NK_CINEMA_SPRITE_INDEX_MASK);
              if((sprite_index >= NK_CINEMA_SPRITES_PER_BANK) ||
                 (nk_cinema_sprites[
                       bank->first_sprite + sprite_index
                     ].frame_count == 0U))
                {
                  return false;
                }

              break;
            case NK_CINEMA_EVENT_PLAY_SOUND:
              if(event->data >= NK_CINEMA_SOUND_COUNT)
                {
                  return false;
                }

              break;
            case NK_CINEMA_EVENT_SHOW_TEXT:
              if(event->data >= NK_CINEMA_TEXTS_PER_BANK)
                {
                  return false;
                }

              break;
            default:
              return false;
            }
        }

      for(sprite_index = 0U;
          sprite_index < NK_CINEMA_SPRITES_PER_BANK;
          ++sprite_index)
        {
          sprite = &nk_cinema_sprites[
            bank->first_sprite + sprite_index
                   ];
          if((u32)sprite->first_frame + sprite->frame_count
             > nk_cinema_frame_count)
            {
              return false;
            }

          for(frame_index = 0U;
              frame_index < sprite->frame_count;
              ++frame_index)
            {
              frame = &nk_cinema_frames[
                sprite->first_frame + frame_index
                      ];
              if((u32)frame->first_image + frame->image_count
                 > nk_cinema_image_ref_count)
                {
                  return false;
                }

              for(image_index = 0U;
                  image_index < frame->image_count;
                  ++image_index)
                {
                  image = &nk_cinema_image_refs[
                    frame->first_image + image_index
                          ];
                  if(image->image >= bank->image_count)
                    {
                      return false;
                    }
                }
            }
        }
    }

  return true;
}
