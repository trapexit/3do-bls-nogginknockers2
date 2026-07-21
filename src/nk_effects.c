#include "nk_effects.h"

#include "stddef.h"

#define NK_EFFECT_FREE_HIGH_MASK (0xffU)

#define NK_EFFECT_POOL_WORD_BITS            (32U)
#define NK_EFFECT_GROUND_RANGE_ONE_FIRST    (7)
#define NK_EFFECT_GROUND_RANGE_ONE_LAST     (10)
#define NK_EFFECT_GROUND_RANGE_TWO_FIRST    (12)
#define NK_EFFECT_GROUND_RANGE_TWO_LAST     (14)
#define NK_EFFECT_LANDING_SHIFT_TYPE_FIRST (7)
#define NK_EFFECT_LANDING_SHIFT_TYPE_LAST  (9)
#define NK_EFFECT_RANDOM_BLOOD_TYPE         (16)
#define NK_EFFECT_RANDOM_BLOOD_FIRST        (42)
#define NK_EFFECT_RANDOM_BLOOD_COUNT        (6)
#define NK_EFFECT_RANDOM_BLOOD_LAST         \
  (NK_EFFECT_RANDOM_BLOOD_FIRST + NK_EFFECT_RANDOM_BLOOD_COUNT - 1)
#define NK_EFFECT_BLOOD_TYPE                (11)
#define NK_EFFECT_STICKY_BLOOD_TYPE         (15)
#define NK_EFFECT_STICKY_START_FRAME        (4U)
#define NK_EFFECT_STICKY_FRAME_OFFSET       (3U)
#define NK_EFFECT_STICKY_CHANCE             (10)


static
bool
_nk_effect_type_has_ground(s32    type_)
{
  if(((type_ >= NK_EFFECT_GROUND_RANGE_ONE_FIRST) && (type_ <= NK_EFFECT_GROUND_RANGE_ONE_LAST)) ||
     ((type_ >= NK_EFFECT_GROUND_RANGE_TWO_FIRST) && (type_ <= NK_EFFECT_GROUND_RANGE_TWO_LAST)) ||
     ((type_ >= NK_EFFECT_RANDOM_BLOOD_FIRST) && (type_ <= NK_EFFECT_RANDOM_BLOOD_LAST)))
    {
      return true;
    }

  return false;
}


static
void
_nk_effect_pool_mark_free(NkEffectPool *pool_,
                          u32           index_)
{
  u32    bit;
  u32    half;
  u32    local;

  if(index_ < NK_EFFECT_POOL_HALF)
    {
      half = 0U;
      local = index_;
    }
  else
    {
      half = 1U;
      local = index_ - NK_EFFECT_POOL_HALF;
    }

  if(local < NK_EFFECT_POOL_WORD_BITS)
    {
      bit = (u32)1U << local;
      pool_->free_low[half] |= bit;
    }
  else
    {
      bit = (u32)1U << (local - NK_EFFECT_POOL_WORD_BITS);
      pool_->free_high[half] |= bit;
    }
}


static
void
_nk_effect_pool_mark_occupied(NkEffectPool *pool_,
                              u32           index_)
{
  u32    bit;
  u32    half;
  u32    local;

  if(index_ < NK_EFFECT_POOL_HALF)
    {
      half = 0U;
      local = index_;
    }
  else
    {
      half = 1U;
      local = index_ - NK_EFFECT_POOL_HALF;
    }

  if(local < NK_EFFECT_POOL_WORD_BITS)
    {
      bit = (u32)1U << local;
      pool_->free_low[half] &= ~bit;
    }
  else
    {
      bit = (u32)1U << (local - NK_EFFECT_POOL_WORD_BITS);
      pool_->free_high[half] &= ~bit;
    }
}


static
int
_nk_effect_pool_first_free(const NkEffectPool *pool_,
                           u32                 half_)
{
  u32    bit;
  u32    local;
  u32    word;

  word = pool_->free_low[half_];
  if(word != 0U)
    {
      bit = 1U;
      local = 0U;
      while((word & bit) == 0U)
        {
          bit <<= 1;
          local++;
        }

      return (int)(half_ * NK_EFFECT_POOL_HALF + local);
    }

  word = pool_->free_high[half_] & NK_EFFECT_FREE_HIGH_MASK;
  if(word != 0U)
    {
      bit = 1U;
      local = NK_EFFECT_POOL_WORD_BITS;
      while((word & bit) == 0U)
        {
          bit <<= 1;
          local++;
        }

      return (int)(half_ * NK_EFFECT_POOL_HALF + local);
    }

  return -1;
}


static
void
_nk_effect_cache_frame_motion(NkEffect *effect_)
{
  const NkAnimFrame *frame;
  u32    frame_index;

  /*
   * Retain the combined frame-and-direction table index in the old direction
   * word. Its low bit is the direction, and the remaining bits select the
   * recovered 200-frame MISC entry. This keeps the 68-byte record stride while
   * avoiding separate frame and direction loads during saturated traversal.
   */
  frame_index =
    (u32)effect_->bank_move_frame_first +
    effect_->animation.frame_index;
  effect_->draw_variant_index =
    (frame_index << NK_EFFECT_FRAME_INDEX_SHIFT) |
    (effect_->draw_variant_index & NK_EFFECT_DIRECTION_MASK);
  /*
   * Decode the two little-endian signed motion words directly from their
   * packed bytes. A signed high byte times 256 plus an unsigned low byte is
   * the exact 32-bit sign extension and avoids the generic 16-bit helpers
   * on every effect frame transition.
   */
  frame = effect_->frame;
  effect_->delta_x =
    (s32)frame->dy * NK_SUBPIXEL_ONE + (s32)(u8)frame->dx;
  effect_->delta_y =
    (s32)frame->ty * NK_SUBPIXEL_ONE + (s32)(u8)frame->tx;
  if((effect_->draw_variant_index & NK_EFFECT_DIRECTION_MASK) != 0U)
    {
      /*
       * Mirroring uses signed-16 wrap in the DOS oracle. Preserve the
       * exceptional -32768 value while keeping runtime motion 32-bit.
       */
      if(effect_->delta_x != -32768)
        {
          effect_->delta_x = -effect_->delta_x;
        }
    }
}


static
bool
_nk_effect_start_animation(NkEffect *effect_,
                           s32       type_)
{
  const NkAnimBank *bank;
  const NkAnimFrame *frame;

  if((type_ < 0) || (type_ >= NK_ANIM_MOVES_PER_BANK))
    {
      return false;
    }

  if(!nk_anim_cursor_start(
       &effect_->animation,
       NK_EFFECT_BANK_INDEX,
       (u32)type_))
    {
      return false;
    }

  frame = nk_anim_cursor_frame(&effect_->animation);
  if(frame == NULL)
    {
      nk_anim_cursor_reset(&effect_->animation);
      effect_->frame = NULL;
      return false;
    }

  bank = &nk_anim_banks[NK_EFFECT_BANK_INDEX];
  effect_->frame = frame;
  /*
   * Cursor start always selects local frame zero. Pay the packed-bank
   * pointer conversion once per new animation; subsequent frame changes
   * update the draw index with the cached move base.
   */
  effect_->bank_move_frame_first = (u8)(
    frame - &nk_anim_frames[bank->frame_first]
    );
  _nk_effect_cache_frame_motion(effect_);
  effect_->type = type_;
  effect_->active = 1U;
  return true;
}


void
nk_effect_pool_reset(NkEffectPool *pool_)
{
  u32    index;

  for(index = 0U; index < NK_EFFECT_POOL_COUNT; ++index)
    {
      NkEffect *effect;

      effect = &pool_->effects[index];
      nk_anim_cursor_reset(&effect->animation);
      effect->frame = NULL;
      effect->bank_move_frame_first = 0U;
      effect->type = 0;
      effect->x = 0;
      effect->y = 0;
      effect->draw_variant_index = 0U;
      effect->fraction_x = 0;
      effect->fraction_y = 0;
      effect->max_y = 0;
      effect->delta_x = 0;
      effect->delta_y = 0;
      effect->last_source_tick = 0U;
      effect->active = 0U;
    }

  pool_->free_low[0] = NK_U32_MAX;
  pool_->free_low[1] = NK_U32_MAX;
  pool_->free_high[0] = NK_EFFECT_FREE_HIGH_MASK;
  pool_->free_high[1] = NK_EFFECT_FREE_HIGH_MASK;
}


void
nk_effect_pool_rebuild_free_slots(NkEffectPool *pool_)
{
  u32    index;

  pool_->free_low[0] = NK_U32_MAX;
  pool_->free_low[1] = NK_U32_MAX;
  pool_->free_high[0] = NK_EFFECT_FREE_HIGH_MASK;
  pool_->free_high[1] = NK_EFFECT_FREE_HIGH_MASK;
  for(index = 0U; index < NK_EFFECT_POOL_COUNT; ++index)
    {
      if(pool_->effects[index].active != 0U)
        {
          _nk_effect_pool_mark_occupied(pool_, index);
        }
    }
}


int
nk_effect_generate(NkEffectPool *pool_,
                   nk_rng       *rng_,
                   s32           type_,
                   s32           x_,
                   s32           y_,
                   s32           dx_,
                   s32           dy_,
                   u8            direction_,
                   u8            priority_)
{
  NkEffect *effect;
  const NkAnimMove *move;
  int free_index;
  u32    index;
  u32    half;

  if(type_ == NK_EFFECT_RANDOM_BLOOD_TYPE)
    {
      type_ = NK_EFFECT_RANDOM_BLOOD_FIRST + nk_rng_bounded(rng_, NK_EFFECT_RANDOM_BLOOD_COUNT);
    }

  if((type_ < 0) || (type_ >= NK_ANIM_MOVES_PER_BANK))
    {
      return -1;
    }

  move = nk_anim_move(NK_EFFECT_BANK_INDEX, (u32)type_);
  if((move == NULL) || (move->frame_count == 0U))
    {
      return -1;
    }

  half = priority_ != 0U ? 1U : 0U;
  free_index = _nk_effect_pool_first_free(pool_, half);
  if(free_index < 0)
    {
      return -1;
    }

  index = (u32)free_index;
  effect = &pool_->effects[index];
  effect->draw_variant_index = direction_ != 0U ? 1U : 0U;
  if(!_nk_effect_start_animation(effect, type_))
    {
      return -1;
    }

  _nk_effect_pool_mark_occupied(pool_, index);
  if((effect->draw_variant_index & NK_EFFECT_DIRECTION_MASK) == 0U)
    {
      effect->x = nk_wrap_add(x_, dx_);
    }
  else
    {
      effect->x = nk_wrap_sub(x_, dx_);
    }

  effect->y = nk_wrap_add(y_, dy_);
  if((type_ >= NK_EFFECT_RANDOM_BLOOD_FIRST) &&
     (type_ < NK_EFFECT_RANDOM_BLOOD_FIRST + NK_EFFECT_RANDOM_BLOOD_COUNT))
    {
      effect->animation.remaining = nk_wrap_add(
        effect->animation.remaining,
        nk_rng_bounded(rng_, 10)
        );
    }

  effect->fraction_x = 0;
  effect->fraction_y = 0;
  effect->max_y = _nk_effect_type_has_ground(type_) ? 190 : 999;
  effect->max_y = nk_wrap_add(
    effect->max_y,
    nk_rng_bounded(rng_, 10)
    );
  effect->last_source_tick = 0U;
  return (int)index;
}


const
NkAnimFrame *
nk_effect_frame(const NkEffect *effect_)
{
  if((effect_ == NULL) || (effect_->active == 0U))
    {
      return NULL;
    }

  return effect_->frame;
}


static
void
_nk_effect_apply_axis(s32    *position_,
                      s32    *fraction_,
                      s32     delta_,
                      u32     source_ticks_)
{
  s32    elapsed_delta;
  s32    movement;
  u32    magnitude;
  u32    quotient;

  /*
   * A stored fraction is 0..255, a frame delta is signed 16-bit, and a
   * batch is at most four ticks.  Their sum therefore cannot overflow.
   */
  elapsed_delta = (s32)delta_ * (s32)source_ticks_;
  *fraction_ += elapsed_delta;
  if(*fraction_ >= 0)
    {
      movement = (s32)((u32) * fraction_ >> NK_SUBPIXEL_SHIFT);
    }
  else
    {
      magnitude = 0U - (u32) * fraction_;
      quotient = magnitude >> NK_SUBPIXEL_SHIFT;
      if((magnitude & NK_SUBPIXEL_MASK) != 0U)
        {
          quotient++;
        }

      movement = -(s32)quotient;
    }

  /*
   * Runtime effect positions are gameplay coordinates plus finite signed
   * frame motion, so applying this small movement cannot overflow s32.
   */
  *position_ += movement;
  *fraction_ = (s32)((u32) * fraction_ & NK_SUBPIXEL_MASK);
}


static
u32
_nk_effect_span_to_ground(const NkEffect *effect_,
                          s32             delta_y_,
                          u32             maximum_span_)
{
  s32    fraction;
  s32    movement;
  s32    y;
  u32    magnitude;
  u32    quotient;
  u32    tick;

  fraction = effect_->fraction_y;
  y = effect_->y;
  for(tick = 1U; tick <= maximum_span_; ++tick)
    {
      fraction += delta_y_;
      if(fraction >= 0)
        {
          movement = (s32)((u32)fraction >> NK_SUBPIXEL_SHIFT);
        }
      else
        {
          magnitude = 0U - (u32)fraction;
          quotient = magnitude >> NK_SUBPIXEL_SHIFT;
          if((magnitude & NK_SUBPIXEL_MASK) != 0U)
            {
              quotient++;
            }

          movement = -(s32)quotient;
        }

      y += movement;
      fraction = (s32)((u32)fraction & NK_SUBPIXEL_MASK);
      if(y > effect_->max_y)
        {
          return tick;
        }
    }

  return maximum_span_;
}


static
void
_nk_effect_advance_unchecked(NkEffect *effect_,
                             int       sticky_blood_,
                             u32       source_ticks_)
{
  s32    comparison_type;
  s32    elapsed_delta;
  s32    movement;
  s32    original_fraction_y;
  s32    original_y;
  u32    frame_ticks;
  u32    magnitude;
  u32    quotient;
  u32    span;
  int step;

  while((source_ticks_ > 0U) && (effect_->active != 0U))
    {
      if(effect_->animation.remaining < 0)
        {
          effect_->active = 0U;
          return;
        }

      /*
       * Movement uses the current frame before the duration counter advances.
       * Stop a batch at a frame boundary, and split before the first ground
       * crossing, so this remains equivalent to consecutive 100 Hz ticks.
       */
      frame_ticks = (u32)effect_->animation.remaining + 1U;
      span = source_ticks_ < frame_ticks ? source_ticks_ : frame_ticks;
      if(span > 4U)
        {
          span = 4U;
        }

      /*
       * Most effects cannot reach the ground during this three- or
       * four-tick visit. Project Y once for that common case. Only restore
       * and scan individual ticks when the batch endpoint proves that a
       * first-crossing split is required.
       */
      original_y = effect_->y;
      original_fraction_y = effect_->fraction_y;
      /*
       * Norcroft does not inline the generic pointer-based axis helper.
       * Expand the common Y projection here so every active effect avoids
       * one call and its indirect position/fraction accesses. The helper
       * remains below for the rare ground-crossing retry.
       */
      elapsed_delta = effect_->delta_y * (s32)span;
      effect_->fraction_y += elapsed_delta;
      if(effect_->fraction_y >= 0)
        {
          movement = (s32)((u32)effect_->fraction_y >> NK_SUBPIXEL_SHIFT);
        }
      else
        {
          magnitude = 0U - (u32)effect_->fraction_y;
          quotient = magnitude >> NK_SUBPIXEL_SHIFT;
          if((magnitude & NK_SUBPIXEL_MASK) != 0U)
            {
              quotient++;
            }

          movement = -(s32)quotient;
        }

      effect_->y += movement;
      effect_->fraction_y =
        (s32)((u32)effect_->fraction_y & NK_SUBPIXEL_MASK);
      if((span > 1U) &&
         ((original_y > effect_->max_y) ||
          (effect_->y > effect_->max_y)))
        {
          effect_->y = original_y;
          effect_->fraction_y = original_fraction_y;
          span = _nk_effect_span_to_ground(
            effect_,
            effect_->delta_y,
            span
            );
          _nk_effect_apply_axis(
            &effect_->y,
            &effect_->fraction_y,
            effect_->delta_y,
            span
            );
        }

      /*
       * Expand the common X projection for the same reason. These
       * statements are arithmetically identical to nk_effect_apply_axis().
       */
      elapsed_delta = effect_->delta_x * (s32)span;
      effect_->fraction_x += elapsed_delta;
      if(effect_->fraction_x >= 0)
        {
          movement = (s32)((u32)effect_->fraction_x >> NK_SUBPIXEL_SHIFT);
        }
      else
        {
          magnitude = 0U - (u32)effect_->fraction_x;
          quotient = magnitude >> NK_SUBPIXEL_SHIFT;
          if((magnitude & NK_SUBPIXEL_MASK) != 0U)
            {
              quotient++;
            }

          movement = -(s32)quotient;
        }

      effect_->x += movement;
      effect_->fraction_x =
        (s32)((u32)effect_->fraction_x & NK_SUBPIXEL_MASK);

      /*
       * Saturated blood normally consumes the complete three- or four-tick
       * visit without reaching either a frame or ground boundary.  In that
       * case no animation or type transition can occur, so finish the exact
       * already-projected motion without entering the boundary machinery.
       */
      if((effect_->y <= effect_->max_y) &&
         (span == source_ticks_) &&
         (span <= (u32)effect_->animation.remaining))
        {
          effect_->animation.remaining -= (s32)span;
          return;
        }

      comparison_type = effect_->type;
      if(effect_->y > effect_->max_y)
        {
          effect_->type = NK_EFFECT_BLOOD_TYPE;
          if((comparison_type >= NK_EFFECT_LANDING_SHIFT_TYPE_FIRST) &&
             (comparison_type <= NK_EFFECT_LANDING_SHIFT_TYPE_LAST))
            {
              if((effect_->draw_variant_index & NK_EFFECT_DIRECTION_MASK) == 0U)
                {
                  effect_->x = nk_wrap_sub(effect_->x, NK_EFFECT_GROUND_OFFSET);
                }
              else
                {
                  effect_->x = nk_wrap_add(effect_->x, NK_EFFECT_GROUND_OFFSET);
                }
            }

          effect_->max_y = NK_EFFECT_SETTLED_MAX_Y;
        }

      if(span <= (u32)effect_->animation.remaining)
        {
          effect_->animation.remaining -= (s32)span;
          step = NK_ANIM_STEP_NONE;
        }
      else
        {
          effect_->animation.remaining = 0;
          step = nk_anim_cursor_tick(&effect_->animation);
        }

      if(step == NK_ANIM_STEP_COMPLETE)
        {
          if((effect_->type != NK_EFFECT_STICKY_HOLD_TYPE) || (sticky_blood_))
            {
              effect_->active = 0U;
              effect_->frame = NULL;
              if((effect_->type == NK_EFFECT_LOOP_TYPE_TWO) ||
                 (effect_->type == NK_EFFECT_LOOP_TYPE_ONE))
                {
                  comparison_type = 0;
                }
            }
          else if(!nk_anim_cursor_hold_last(&effect_->animation, NK_EFFECT_STICKY_HOLD_TICKS))
            {
              effect_->active = 0U;
              effect_->frame = NULL;
            }
          else
            {
              effect_->frame = nk_anim_cursor_frame(&effect_->animation);
              _nk_effect_cache_frame_motion(effect_);
            }
        }
      else if(step == NK_ANIM_STEP_INVALID)
        {
          effect_->active = 0U;
          effect_->frame = NULL;
        }
      else if(step == NK_ANIM_STEP_FRAME)
        {
          effect_->frame = nk_anim_cursor_frame(&effect_->animation);
          if(effect_->frame == NULL)
            {
              effect_->active = 0U;
            }
          else
            {
              _nk_effect_cache_frame_motion(effect_);
            }
        }

      if(comparison_type != effect_->type)
        {
          if(!_nk_effect_start_animation(effect_, effect_->type))
            {
              effect_->active = 0U;
              effect_->frame = NULL;
            }
        }

      source_ticks_ -= span;
    }
}


void
nk_effect_advance(NkEffect *effect_,
                  int       sticky_blood_,
                  u32       source_ticks_)
{
  if((effect_ == NULL) || (effect_->active == 0U))
    {
      return;
    }

  /*
   * Preserve the public entry point's fail-closed behavior.  Pool callers
   * use the unchecked path only for slots created by
   * nk_effect_start_animation() or copied whole during migration, so their
   * active slots already prove a cached frame.
   */
  if(effect_->frame == NULL)
    {
      effect_->active = 0U;
      return;
    }

  _nk_effect_advance_unchecked(effect_, sticky_blood_, source_ticks_);
}


void
nk_effect_tick(NkEffect *effect_,
               int       sticky_blood_)
{
  nk_effect_advance(effect_, sticky_blood_, 1U);
}


void
nk_effect_pool_tick(NkEffectPool *pool_,
                    int           sticky_blood_)
{
  u32    index;

  for(index = 0U; index < NK_EFFECT_POOL_COUNT; ++index)
    {
      if(pool_->effects[index].active != 0U)
        {
          _nk_effect_advance_unchecked(
            &pool_->effects[index],
            sticky_blood_,
            1U
            );
          if(pool_->effects[index].active == 0U)
            {
              _nk_effect_pool_mark_free(pool_, index);
            }
        }
    }
}


void
nk_effect_pool_advance_to_tick(NkEffectPool *pool_,
                               int           sticky_blood_,
                               u32           source_tick_)
{
  NkEffect *effect;
  u32    index;
  u32    source_ticks;

  for(index = 0U; index < NK_EFFECT_POOL_COUNT; ++index)
    {
      effect = &pool_->effects[index];
      if(effect->active != 0U)
        {
          source_ticks = source_tick_ - effect->last_source_tick;
          effect->last_source_tick = source_tick_;
          _nk_effect_advance_unchecked(
            effect,
            sticky_blood_,
            source_ticks
            );
          if(effect->active == 0U)
            {
              _nk_effect_pool_mark_free(pool_, index);
            }
        }
    }
}


static
int
_nk_effect_prepare_draw_unchecked(NkEffectPool *pool_,
                                  NkEffect     *effect_,
                                  u32           effect_index_,
                                  int           sticky_blood_,
                                  nk_rng       *rng_)
{
  const NkAnimMove *move;

  if(effect_->active == 0U)
    {
      return NK_EFFECT_DRAW_NONE;
    }

  if((effect_index_ >= NK_EFFECT_POOL_HALF) && (effect_->type == NK_EFFECT_BLOOD_TYPE))
    {
      int target_index;

      /*
       * The source scans through effects[MAXMARKS] inclusive.  Slot 40
       * has already been visited by the front painter, so a later slot may
       * migrate into that now-empty boundary slot for the next frame.
       */
      target_index = _nk_effect_pool_first_free(pool_, 0U);
      if((target_index < 0) &&
         (pool_->effects[NK_EFFECT_POOL_HALF].active == 0U))
        {
          target_index = NK_EFFECT_POOL_HALF;
        }

      if(target_index >= 0)
        {
          pool_->effects[target_index] = *effect_;
          _nk_effect_pool_mark_occupied(
            pool_,
            (u32)target_index
            );
        }

      effect_->active = 0U;
      _nk_effect_pool_mark_free(pool_, effect_index_);
      return NK_EFFECT_DRAW_NONE;
    }

  if(!sticky_blood_)
    {
      return NK_EFFECT_DRAW_NORMAL;
    }

  if(effect_->type == NK_EFFECT_STICKY_BLOOD_TYPE)
    {
      return NK_EFFECT_DRAW_STICKY;
    }

  if((effect_->type != NK_EFFECT_BLOOD_TYPE) ||
     (effect_->animation.frame_index < NK_EFFECT_STICKY_START_FRAME))
    {
      return NK_EFFECT_DRAW_NORMAL;
    }

  move = nk_anim_move(NK_EFFECT_BANK_INDEX, NK_EFFECT_BLOOD_TYPE);
  if(move == NULL)
    {
      return NK_EFFECT_DRAW_NONE;
    }

  if(effect_->animation.frame_index ==
     (u32)move->frame_count - NK_EFFECT_STICKY_FRAME_OFFSET)
    {
      return NK_EFFECT_DRAW_STICKY;
    }

  if(nk_rng_bounded(rng_, NK_EFFECT_STICKY_CHANCE) != 0)
    {
      return NK_EFFECT_DRAW_NORMAL;
    }

  return NK_EFFECT_DRAW_STICKY;
}


int
nk_effect_prepare_draw(NkEffectPool *pool_,
                       u32           effect_index_,
                       int           sticky_blood_,
                       nk_rng       *rng_)
{
  if(effect_index_ >= NK_EFFECT_POOL_COUNT)
    {
      return NK_EFFECT_DRAW_NONE;
    }

  return _nk_effect_prepare_draw_unchecked(
    pool_,
    &pool_->effects[effect_index_],
    effect_index_,
    sticky_blood_,
    rng_
    );
}


bool
nk_effect_prepare_draw_range(NkEffectPool *pool_,
                             u32           first_,
                             u32           count_,
                             int           sticky_blood_,
                             nk_rng       *rng_,
                             u8           *draw_modes_,
                             u32          *sticky_indices_,
                             u32          *sticky_count_)
{
  NkEffect *effect;
  u32    index;
  int mode;

  if((pool_ == NULL) || (rng_ == NULL) ||
     (sticky_indices_ == NULL) || (sticky_count_ == NULL) ||
     (count_ > NK_EFFECT_POOL_HALF) ||
     (first_ > NK_EFFECT_POOL_COUNT - count_))
    {
      return false;
    }

  *sticky_count_ = 0U;
  if(draw_modes_ == NULL)
    {
      for(index = first_; index < first_ + count_; ++index)
        {
          effect = &pool_->effects[index];
          if(effect->active == 0U)
            {
              continue;
            }

          if(effect->type == NK_EFFECT_BLOOD_TYPE)
            {
              if((index < NK_EFFECT_POOL_HALF) && (!sticky_blood_))
                {
                  continue;
                }
            }
          else if((effect->type != NK_EFFECT_STICKY_BLOOD_TYPE) || (!sticky_blood_))
            {
              continue;
            }

          mode = _nk_effect_prepare_draw_unchecked(
            pool_,
            effect,
            index,
            sticky_blood_,
            rng_
            );
          if((mode == NK_EFFECT_DRAW_STICKY) &&
             (effect->active != 0U))
            {
              if(*sticky_count_ >= count_)
                {
                  return false;
                }

              sticky_indices_[*sticky_count_] = index;
              (*sticky_count_)++;
            }
        }

      return true;
    }

  for(index = first_; index < first_ + count_; ++index)
    {
      effect = &pool_->effects[index];
      mode = _nk_effect_prepare_draw_unchecked(
        pool_,
        effect,
        index,
        sticky_blood_,
        rng_
        );
      draw_modes_[index] = (u8)mode;
      if((mode == NK_EFFECT_DRAW_STICKY) &&
         (effect->active != 0U))
        {
          if(*sticky_count_ >= count_)
            {
              return false;
            }

          sticky_indices_[*sticky_count_] = index;
          (*sticky_count_)++;
        }
    }

  return true;
}
