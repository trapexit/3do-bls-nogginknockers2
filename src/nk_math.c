#include "nk_math.h"

/* NOGGIN.CPP: accel, deccel, maxvelocity, boundminimum, boundmaximum. */
const nk_motion_profile nk_character_motion[NK_CHARACTER_COUNT] =
{
  {
    { 0x0b00, 0x0b00 },
    { 0x0600, 0x0600 },
    { 0x10000, 0x10000 },
    { -(27 * NK_FIXED_ONE), 40 * NK_FIXED_ONE },
    { -(12 * NK_FIXED_ONE), 180 * NK_FIXED_ONE }
  },
  {
    { 0x1200, 0x1200 },
    { 0x0700, 0x0700 },
    { 0x20000, 0x20000 },
    { -(12 * NK_FIXED_ONE), 20 * NK_FIXED_ONE },
    { -(5 * NK_FIXED_ONE), 190 * NK_FIXED_ONE }
  },
  {
    { 0x5d00, 0x5650 },
    { 0x5200, 0x5200 },
    { 0x16000, 0x16000 },
    { -(35 * NK_FIXED_ONE), 20 * NK_FIXED_ONE },
    { -(10 * NK_FIXED_ONE), 190 * NK_FIXED_ONE }
  },
  {
    { 0x1b00, 0x1b00 },
    { 0x0c00, 0x0c00 },
    { 0x1a000, 0x1a000 },
    { -(18 * NK_FIXED_ONE), 20 * NK_FIXED_ONE },
    { -(2 * NK_FIXED_ONE), 190 * NK_FIXED_ONE }
  },
  {
    { 0x1a00, 0x1a00 },
    { 0x1300, 0x1300 },
    { 0x12000, 0x12000 },
    { -(24 * NK_FIXED_ONE), 20 * NK_FIXED_ONE },
    { -(2 * NK_FIXED_ONE), 190 * NK_FIXED_ONE }
  },
  {
    { 0x1b00, 0x1b00 },
    { 0x0c00, 0x0c00 },
    { 0x10000, 0x1b000 },
    { -(18 * NK_FIXED_ONE), 20 * NK_FIXED_ONE },
    { -(2 * NK_FIXED_ONE), 190 * NK_FIXED_ONE }
  },
  {
    { 0x1100, 0x1100 },
    { 0x0200, 0x0200 },
    { 0x14000, 0x14000 },
    { -(29 * NK_FIXED_ONE), 20 * NK_FIXED_ONE },
    { -(9 * NK_FIXED_ONE), 190 * NK_FIXED_ONE }
  },
  {
    { 0x1000, 0x1000 },
    { 0x0500, 0x0500 },
    { 0x1b000, 0x1b000 },
    { -(8 * NK_FIXED_ONE), 20 * NK_FIXED_ONE },
    { 155 * NK_FIXED_ONE, 190 * NK_FIXED_ONE }
  }
};

/*
 * Gain per tap, cost of special one, cost of special two.
 */
const s32    nk_character_energy[NK_CHARACTER_COUNT][3] =
{
  { 30, 20, 120 },
  { 30, 20, 40 },
  { 30, 25, 25 },
  { 30, 25, 25 },
  { 30, 25, 25 },
  { 30, 25, 25 },
  { 30, 35, 40 },
  { 30, 10, 25 }
};

/*
 * NOGGIN.CPP: ballinit.
 */
const nk_motion_profile nk_ball_motion =
{
  { 0, 0 },
  { 1, 1 },
  { 0x0a0000, 0x020000 },
  { -(30 * NK_FIXED_ONE), 8 * NK_FIXED_ONE },
  { 350 * NK_FIXED_ONE, 192 * NK_FIXED_ONE }
};

s32
nk_s32_from_bits(u32    value_)
{
  if(value_ <= 0x7fffffffU)
    {
      return (s32)value_;
    }

  return (s32)(-1 - (s32)(NK_U32_MAX - value_));
}


s16
nk_s16_from_bits(u16    value_)
{
  if(value_ <= 0x7fffU)
    {
      return (s16)value_;
    }

  return (s16)(-1 - (s16)(0xffffU - value_));
}


s32
nk_wrap_add(s32    left_,
            s32    right_)
{
  return nk_s32_from_bits((u32)left_ + (u32)right_);
}


s32
nk_wrap_sub(s32    left_,
            s32    right_)
{
  return nk_s32_from_bits((u32)left_ - (u32)right_);
}


s32
nk_wrap_neg(s32    value_)
{
  return nk_s32_from_bits(0U - (u32)value_);
}


s32
nk_floor_shift_right(s32    value_,
                     u8     bits_)
{
  u32    divisor;
  u32    magnitude;
  u32    quotient;

  if(bits_ == 0U)
    {
      return value_;
    }

  if(bits_ >= 31U)
    {
      return value_ < 0 ? -1 : 0;
    }

  divisor = 1U << bits_;
  if(value_ >= 0)
    {
      return value_ / (s32)divisor;
    }

  magnitude = 0U - (u32)value_;
  quotient = magnitude / divisor;
  if(magnitude % divisor != 0U)
    {
      quotient++;
    }

  return -(s32)quotient;
}


s32
nk_fixed_from_int(s32    value_)
{
  return nk_s32_from_bits((u32)value_ * 65536U);
}


s32
nk_fixed_floor_to_int(s32    value_)
{
  u32    magnitude;
  u32    quotient;

  if(value_ >= 0)
    {
      return value_ / NK_FIXED_ONE;
    }

  magnitude = 0U - (u32)value_;
  quotient = magnitude / (u32)NK_FIXED_ONE;
  if((magnitude % (u32)NK_FIXED_ONE) != 0U)
    {
      quotient++;
    }

  return -(s32)quotient;
}


void
nk_rng_seed(nk_rng *rng_,
            u32     seed_)
{
  rng_->state = seed_;
}


u32
nk_rng_next(nk_rng *rng_)
{
  rng_->state = rng_->state * 1103515245U + 12345U;
  return (rng_->state >> 16) & 0x7fffU;
}


s32
nk_rng_bounded(nk_rng *rng_,
               s32     limit_)
{
  u32    sample;

  if(limit_ <= 0)
    {
      return 0;
    }

  sample = (u32)nk_rng_next(rng_);
  return (s32)(((u32)limit_ * sample) / 32768U);
}


bool
nk_tick_clock_init(nk_tick_clock *clock_,
                   u32            source_rate_numerator_,
                   u32            source_rate_denominator_)
{
  u32    increment;

  if((source_rate_numerator_ == 0U) || (source_rate_denominator_ == 0U))
    {
      return false;
    }

  if(source_rate_denominator_ > NK_U32_MAX / (u32)NK_LOGICAL_HZ)
    {
      return false;
    }

  increment = source_rate_denominator_ * (u32)NK_LOGICAL_HZ;
  if(increment > NK_U32_MAX - (source_rate_numerator_ - 1U))
    {
      return false;
    }

  clock_->increment = increment;
  clock_->threshold = source_rate_numerator_;
  clock_->accumulator = 0U;
  return true;
}


u32
nk_tick_clock_step(nk_tick_clock *clock_)
{
  return nk_tick_clock_advance(clock_, 1U);
}


u32
nk_tick_clock_advance(nk_tick_clock *clock_,
                      u32            source_ticks_)
{
  u32    chunk;
  u32    maximum_chunk;
  u32    ticks;

  ticks = 0U;
  while(source_ticks_ > 0U)
    {
      maximum_chunk =
        (NK_U32_MAX - clock_->accumulator) / clock_->increment;
      chunk = source_ticks_;
      if(chunk > maximum_chunk)
        {
          chunk = maximum_chunk;
        }

      clock_->accumulator += chunk * clock_->increment;
      ticks += clock_->accumulator / clock_->threshold;
      clock_->accumulator %= clock_->threshold;
      source_ticks_ -= chunk;
    }

  return ticks;
}


u32
nk_tick_clock_phase_16(const nk_tick_clock *clock_)
{
  u32    phase;
  u32    remainder;
  u32    step;

  if((clock_ == NULL) || (clock_->threshold == 0U))
    {
      return 0U;
    }

  if(clock_->accumulator >= clock_->threshold)
    {
      return 0xffffU;
    }

  remainder = clock_->accumulator;
  phase = 0U;
  for(step = 0U; step < 16U; ++step)
    {
      phase <<= 1;
      if(remainder >= clock_->threshold - remainder)
        {
          remainder -= clock_->threshold - remainder;
          phase |= 1U;
        }
      else
        {
          remainder += remainder;
        }
    }

  return phase;
}


void
nk_vector_reset(nk_vector *vector_,
                nk_point32 acceleration_,
                nk_point32 deceleration_,
                nk_point32 maximum_)
{
  vector_->position.x = nk_wrap_add(vector_->bound_min.x,
                                    vector_->bound_max.x) / 2;
  vector_->position.y = nk_wrap_add(vector_->bound_min.y,
                                    vector_->bound_max.y) / 2;
  vector_->velocity.x = 0;
  vector_->velocity.y = 0;
  vector_->acceleration = acceleration_;
  vector_->deceleration = deceleration_;
  vector_->maximum = maximum_;
  vector_->bounce = 0;
}


void
nk_vector_flip_x(nk_vector *vector_)
{
  vector_->position.x = nk_wrap_sub(nk_fixed_from_int(NK_LOGICAL_WIDTH),
                                    vector_->position.x);
  vector_->velocity.x = nk_wrap_neg(vector_->velocity.x);
  vector_->acceleration.x = nk_wrap_neg(vector_->acceleration.x);
}


void
nk_vector_tick(nk_vector *vector_,
               u8         direction_,
               int        clip_x_)
{
  s32    negative_maximum;

  if(vector_->velocity.x > 0)
    {
      vector_->velocity.x = nk_wrap_sub(vector_->velocity.x,
                                        vector_->deceleration.x);
      if(vector_->velocity.x < 0)
        {
          vector_->velocity.x = 0;
        }
    }

  if(vector_->velocity.x < 0)
    {
      vector_->velocity.x = nk_wrap_add(vector_->velocity.x,
                                        vector_->deceleration.x);
      if(vector_->velocity.x > 0)
        {
          vector_->velocity.x = 0;
        }
    }

  if(vector_->velocity.y > 0)
    {
      vector_->velocity.y = nk_wrap_sub(vector_->velocity.y,
                                        vector_->deceleration.y);
      if(vector_->velocity.y < 0)
        {
          vector_->velocity.y = 0;
        }
    }

  if(vector_->velocity.y < 0)
    {
      vector_->velocity.y = nk_wrap_add(vector_->velocity.y,
                                        vector_->deceleration.y);
      if(vector_->velocity.y > 0)
        {
          vector_->velocity.y = 0;
        }
    }

  if((direction_ & NK_DIR_RIGHT) != 0U)
    {
      vector_->velocity.x = nk_wrap_add(vector_->velocity.x,
                                        vector_->acceleration.x);
    }

  if((direction_ & NK_DIR_LEFT) != 0U)
    {
      vector_->velocity.x = nk_wrap_sub(vector_->velocity.x,
                                        vector_->acceleration.x);
    }

  if((direction_ & NK_DIR_DOWN) != 0U)
    {
      vector_->velocity.y = nk_wrap_add(vector_->velocity.y,
                                        vector_->acceleration.y);
    }

  if((direction_ & NK_DIR_UP) != 0U)
    {
      vector_->velocity.y = nk_wrap_sub(vector_->velocity.y,
                                        vector_->acceleration.y);
    }

  if(vector_->velocity.x != 0)
    {
      if(vector_->velocity.x > 0)
        {
          if(vector_->position.x <= vector_->bound_max.x)
            {
              vector_->position.x = nk_wrap_add(vector_->position.x,
                                                vector_->velocity.x);
            }

          if(vector_->velocity.x > vector_->maximum.x)
            {
              vector_->velocity.x = vector_->maximum.x;
            }
        }
      else
        {
          if(vector_->position.x >= vector_->bound_min.x)
            {
              vector_->position.x = nk_wrap_add(vector_->position.x,
                                                vector_->velocity.x);
            }

          negative_maximum = nk_wrap_neg(vector_->maximum.x);
          if(vector_->velocity.x < negative_maximum)
            {
              vector_->velocity.x = negative_maximum;
            }
        }
    }

  if(!clip_x_)
    {
      if(vector_->position.x < vector_->bound_min.x)
        {
          vector_->position.x = nk_wrap_add(vector_->position.x, NK_FIXED_ONE);
          if(vector_->position.x >= vector_->bound_min.x)
            {
              vector_->position.x = vector_->bound_min.x;
              vector_->velocity.x = 0;
            }
        }

      if(vector_->position.x > vector_->bound_max.x)
        {
          vector_->position.x = nk_wrap_sub(vector_->position.x, NK_FIXED_ONE);
          if(vector_->position.x <= vector_->bound_max.x)
            {
              vector_->position.x = vector_->bound_max.x;
              vector_->velocity.x = 0;
            }
        }
    }

  if(vector_->velocity.y != 0)
    {
      vector_->position.y = nk_wrap_add(vector_->position.y,
                                        vector_->velocity.y);
      if(vector_->velocity.y > 0)
        {
          if(vector_->velocity.y > vector_->maximum.y)
            {
              vector_->velocity.y = nk_wrap_sub(vector_->velocity.y, 0x500);
              if(vector_->velocity.y < vector_->maximum.y)
                {
                  vector_->velocity.y = vector_->maximum.y;
                }
            }
        }
      else
        {
          negative_maximum = nk_wrap_neg(vector_->maximum.y);
          if(vector_->velocity.y < negative_maximum)
            {
              vector_->velocity.y = nk_wrap_add(vector_->velocity.y, 0x500);
              /* Preserve the original comparison, including its asymmetry. */
              if(vector_->velocity.y > vector_->maximum.y)
                {
                  vector_->velocity.y = negative_maximum;
                }
            }
        }

      if(vector_->position.y < vector_->bound_min.y)
        {
          vector_->position.y = vector_->bound_min.y;
          vector_->velocity.y = 0;
        }

      if(vector_->position.y > vector_->bound_max.y)
        {
          vector_->position.y = vector_->bound_max.y;
          vector_->velocity.y = 0;
        }
    }
}


int
nk_vector_bounce(nk_vector *vector_)
{
  int bounced;
  s32    negative_maximum;

  bounced = 0;
  if(vector_->acceleration.x != 0)
    {
      vector_->velocity.x = nk_wrap_add(vector_->velocity.x,
                                        vector_->acceleration.x);
      if(vector_->acceleration.x < 0)
        {
          vector_->acceleration.x = nk_wrap_add(vector_->acceleration.x,
                                                0x500);
          if(vector_->acceleration.x > 0)
            {
              vector_->acceleration.x = 0;
            }
        }

      if(vector_->acceleration.x > 0)
        {
          vector_->acceleration.x = nk_wrap_sub(vector_->acceleration.x,
                                                0x500);
          if(vector_->acceleration.x < 0)
            {
              vector_->acceleration.x = 0;
            }
        }
    }

  if(vector_->velocity.x != 0)
    {
      vector_->position.x = nk_wrap_add(vector_->position.x,
                                        vector_->velocity.x);
      if(vector_->velocity.x > 0)
        {
          if(vector_->velocity.x > vector_->maximum.x)
            {
              vector_->velocity.x = vector_->maximum.x;
            }

          if(vector_->velocity.x > 0x50000)
            {
              vector_->velocity.x = nk_wrap_sub(vector_->velocity.x, 0x800);
            }
        }
      else
        {
          negative_maximum = nk_wrap_neg(vector_->maximum.x);
          if(vector_->velocity.x < negative_maximum)
            {
              vector_->velocity.x = negative_maximum;
            }

          if(vector_->velocity.x < -0x50000)
            {
              vector_->velocity.x = nk_wrap_add(vector_->velocity.x, 0x800);
            }
        }

      /* The original x-boundary bounce code is commented out. */
    }

  if(vector_->velocity.y != 0)
    {
      vector_->position.y = nk_wrap_add(vector_->position.y,
                                        vector_->velocity.y);
      if(vector_->velocity.y > 0)
        {
          if(vector_->velocity.y > vector_->maximum.y)
            {
              vector_->velocity.y = vector_->maximum.y;
            }
        }
      else
        {
          negative_maximum = nk_wrap_neg(vector_->maximum.y);
          if(vector_->velocity.y < negative_maximum)
            {
              vector_->velocity.y = negative_maximum;
            }
        }

      if(vector_->position.y < vector_->bound_min.y)
        {
          vector_->position.y = vector_->bound_min.y;
          vector_->velocity.y = nk_wrap_neg(vector_->velocity.y);
          bounced |= 4;
        }

      if(vector_->position.y > vector_->bound_max.y)
        {
          vector_->position.y = vector_->bound_max.y;
          vector_->velocity.y = nk_wrap_neg(vector_->velocity.y);
          bounced |= 8;
        }
    }

  return bounced;
}


void
nk_vector_slow_x_acceleration(nk_vector *vector_,
                              s32        amount_)
{
  if(vector_->velocity.x <= 0)
    {
      vector_->acceleration.x = nk_wrap_add(vector_->acceleration.x, amount_);
    }
  else
    {
      vector_->acceleration.x = nk_wrap_sub(vector_->acceleration.x, amount_);
    }
}


void
nk_vector_speed_x_acceleration(nk_vector *vector_,
                               s32        amount_)
{
  if(vector_->velocity.x >= 0)
    {
      vector_->acceleration.x = nk_wrap_add(vector_->acceleration.x, amount_);
    }
  else
    {
      vector_->acceleration.x = nk_wrap_sub(vector_->acceleration.x, amount_);
    }
}


void
nk_vector_slow_x_velocity(nk_vector *vector_,
                          s32        amount_)
{
  if(vector_->velocity.x <= 0)
    {
      vector_->velocity.x = nk_wrap_add(vector_->velocity.x, amount_);
    }
  else
    {
      vector_->velocity.x = nk_wrap_sub(vector_->velocity.x, amount_);
    }
}


void
nk_vector_speed_x_velocity(nk_vector *vector_,
                           s32        amount_)
{
  if(vector_->velocity.x >= 0)
    {
      vector_->velocity.x = nk_wrap_add(vector_->velocity.x, amount_);
    }
  else
    {
      vector_->velocity.x = nk_wrap_sub(vector_->velocity.x, amount_);
    }
}


void
nk_vector_slow_y_velocity(nk_vector *vector_,
                          s32        amount_)
{
  if(vector_->velocity.y <= 0)
    {
      vector_->velocity.y = nk_wrap_add(vector_->velocity.y, amount_);
    }
  else
    {
      vector_->velocity.y = nk_wrap_sub(vector_->velocity.y, amount_);
    }
}


void
nk_vector_speed_y_velocity(nk_vector *vector_,
                           s32        amount_)
{
  if(vector_->velocity.y >= 0)
    {
      vector_->velocity.y = nk_wrap_add(vector_->velocity.y, amount_);
    }
  else
    {
      vector_->velocity.y = nk_wrap_sub(vector_->velocity.y, amount_);
    }
}
