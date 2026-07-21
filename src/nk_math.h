#pragma once

#include "nk_types.h"

#define NK_LOGICAL_WIDTH  (320)
#define NK_LOGICAL_HEIGHT (200)
#define NK_LOGICAL_HZ     (100)

#define NK_CHARACTER_KLUBBOR  (0U)
#define NK_CHARACTER_FETUS    (1U)
#define NK_CHARACTER_HENRY    (2U)
#define NK_CHARACTER_GURDIP   (3U)
#define NK_CHARACTER_ED       (4U)
#define NK_CHARACTER_SINAMMON (5U)
#define NK_CHARACTER_BUDDY    (6U)
#define NK_CHARACTER_GONZOLES (7U)
#define NK_CHARACTER_COUNT    (8)
#define NK_CHARACTER_INDEX_MASK (7U)

#define NK_DIR_LEFT  (0x01U)
#define NK_DIR_RIGHT (0x02U)
#define NK_DIR_UP    (0x04U)
#define NK_DIR_DOWN  (0x08U)
#define NK_DIR_MASK  (0x0fU)

typedef struct nk_point32
{
  s32    x;
  s32    y;
} nk_point32;

typedef struct nk_vector
{
  nk_point32 position;
  nk_point32 velocity;
  nk_point32 acceleration;
  nk_point32 maximum;
  nk_point32 deceleration;
  nk_point32 bound_min;
  nk_point32 bound_max;
  s32    bounce;
} nk_vector;

typedef struct nk_motion_profile
{
  nk_point32 acceleration;
  nk_point32 deceleration;
  nk_point32 maximum;
  nk_point32 bound_min;
  nk_point32 bound_max;
} nk_motion_profile;

typedef struct nk_rng
{
  u32    state;
} nk_rng;

typedef struct nk_tick_clock
{
  u32    increment;
  u32    threshold;
  u32    accumulator;
} nk_tick_clock;

extern const nk_motion_profile nk_character_motion[NK_CHARACTER_COUNT];
extern const s32    nk_character_energy[NK_CHARACTER_COUNT][3];
extern const nk_motion_profile nk_ball_motion;

s32
nk_s32_from_bits(u32    value_);
s16
nk_s16_from_bits(u16    value_);
s32
nk_wrap_add(s32    left_,
            s32    right_);
s32
nk_wrap_sub(s32    left_,
            s32    right_);
s32
nk_wrap_neg(s32    value_);
s32
nk_floor_shift_right(s32    value_,
                     u8     bits_);

s32
nk_fixed_from_int(s32    value_);
s32
nk_fixed_floor_to_int(s32    value_);

void
nk_rng_seed(nk_rng *rng_,
            u32     seed_);
u32
nk_rng_next(nk_rng *rng_);
s32
nk_rng_bounded(nk_rng *rng_,
               s32     limit_);

bool
nk_tick_clock_init(nk_tick_clock *clock_,
                   u32            source_rate_numerator_,
                   u32            source_rate_denominator_);
u32
nk_tick_clock_step(nk_tick_clock *clock_);
u32
nk_tick_clock_advance(nk_tick_clock *clock_,
                      u32            source_ticks_);
u32
nk_tick_clock_phase_16(const nk_tick_clock *clock_);

void
nk_vector_reset(nk_vector *vector_,
                nk_point32 acceleration_,
                nk_point32 deceleration_,
                nk_point32 maximum_);
void
nk_vector_flip_x(nk_vector *vector_);
void
nk_vector_tick(nk_vector *vector_,
               u8         direction_,
               int        clip_x_);
int
nk_vector_bounce(nk_vector *vector_);
void
nk_vector_slow_x_acceleration(nk_vector *vector_,
                              s32        amount_);
void
nk_vector_speed_x_acceleration(nk_vector *vector_,
                               s32        amount_);
void
nk_vector_slow_x_velocity(nk_vector *vector_,
                          s32        amount_);
void
nk_vector_speed_x_velocity(nk_vector *vector_,
                           s32        amount_);
void
nk_vector_slow_y_velocity(nk_vector *vector_,
                          s32        amount_);
void
nk_vector_speed_y_velocity(nk_vector *vector_,
                           s32        amount_);
