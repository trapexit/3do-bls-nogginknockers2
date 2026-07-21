#include "nk_sound_logic.h"

#include "stddef.h"

#define NK_SOUND_MINIMUM_OPTION (0)
#define NK_SOUND_MAXIMUM_OPTION (7)
#define NK_SOUND_GAIN_STEP (640)
#define NK_MUSIC_GAIN_MAX (0x7fffL)

void
nk_sound_request_clear(NkSoundRequest *request_)
{
  if(request_ == NULL)
    {
      return;
    }

  request_->operation = NK_SOUND_REQUEST_NONE;
  request_->bank_index = 0U;
  request_->sample_index = 0U;
  request_->volume = 0U;
  request_->loop = 0U;
  request_->logical_index = -1;
  request_->reserved_zero = 0U;
  request_->reserved_one = 0U;
}


bool
nk_sound_request_from_game_event(const NkGameEvent *event_,
                                 NkSoundRequest    *request_)
{
  u8    code;
  u8    sound_type;
  int bank_index;
  int sample_index;

  if(request_ == NULL)
    {
      return false;
    }

  nk_sound_request_clear(request_);
  if((event_ == NULL) || (event_->type != NK_GAME_EVENT_SOUND))
    {
      return false;
    }

  if(event_->actor < NK_GAME_PLAYER_COUNT)
    {
      bank_index = (int)event_->actor;
    }
  else if((event_->actor == NK_GAME_ACTOR_BALL) || (event_->actor == NK_GAME_ACTOR_CHOPPER))
    {
      bank_index = NK_GAME_PLAYER_COUNT;
    }
  else
    {
      return false;
    }

  /*
   * v0.78 PlaySE() selects the high nibble for hit sounds and the low
   * nibble for frame sounds.  Bit 3 selects the player-specific bank and
   * bits 0..2 select one of seven samples.  The current converted runtime
   * has no standard sse[][] bank, matching the pre-extraction adapter.
   */
  sound_type = (u8)(event_->flags & NK_GAME_EVENT_SOUND_TYPE_MASK);
  code = event_->value;
  if(sound_type != 0U)
    {
      code >>= NK_ANIM_SOUND_CODE_BITS;
    }

  code &= NK_ANIM_SOUND_CODE_MASK;
  if(((code & NK_ANIM_SOUND_PLAYER_FLAG) == 0U) || ((code & NK_ANIM_SOUND_SAMPLE_MASK) == 0U))
    {
      return false;
    }

  sample_index = (int)sound_type * NK_SOUND_SAMPLES_PER_TYPE
                 + (int)(code & NK_ANIM_SOUND_SAMPLE_MASK) - 1;
  if((sample_index < 0) ||
     (sample_index >= NK_SOUND_COMBAT_SAMPLE_COUNT))
    {
      return false;
    }

  request_->operation = NK_SOUND_REQUEST_PLAY;
  request_->bank_index = (u8)bank_index;
  request_->sample_index = (u8)sample_index;
  request_->volume = NK_SOUND_EFFECT_VOLUME;
  request_->loop = 0U;
  request_->logical_index = NK_SOUND_EFFECT_LOGICAL_INDEX;
  return true;
}


s32
nk_sound_gain_from_option(s32    level_)
{
  if(level_ < NK_SOUND_MINIMUM_OPTION)
    {
      level_ = NK_SOUND_MINIMUM_OPTION;
    }

  if(level_ > NK_SOUND_MAXIMUM_OPTION)
    {
      level_ = NK_SOUND_MAXIMUM_OPTION;
    }

  if(level_ == 0)
    {
      return 0;
    }

  /*
   * v0.78 sets the mixer master level to 32 * level + 31.  Combined with
   * SMIX.C's signed 8-bit lookup scale, the Portfolio equivalent is
   * exactly 640 * (2 * level + 1) for levels 1 through 7.  The port treats
   * option level zero as a true mute.
   */
  return (s32)(NK_SOUND_GAIN_STEP * (2 * level_ + 1));
}


s32
nk_music_gain_from_option(s32    level_)
{
  s32    dos_volume;

  if(level_ < NK_SOUND_MINIMUM_OPTION)
    {
      level_ = NK_SOUND_MINIMUM_OPTION;
    }

  if(level_ > NK_SOUND_MAXIMUM_OPTION)
    {
      level_ = NK_SOUND_MAXIMUM_OPTION;
    }

  if(level_ == 0)
    {
      return 0;
    }

  dos_volume = (s32)(32L * level_ + 31L);
  return (s32)((dos_volume * NK_MUSIC_GAIN_MAX + 127L) / 255L);
}
