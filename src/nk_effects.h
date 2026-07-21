#pragma once

#include "nk_anim.h"
#include "nk_math.h"

#define NK_EFFECT_BANK_INDEX (9)
#define NK_EFFECT_POOL_HALF (40)
#define NK_EFFECT_POOL_COUNT (80)

#define NK_EFFECT_DRAW_NONE (0)
#define NK_EFFECT_DRAW_NORMAL (1)
#define NK_EFFECT_DRAW_STICKY (2)

#define NK_EFFECT_DIRECTION_MASK (0x01U)
#define NK_EFFECT_FRAME_INDEX_SHIFT (1U)

#define NK_EFFECT_GROUND_OFFSET (20)
#define NK_EFFECT_SETTLED_MAX_Y (999)
#define NK_EFFECT_STICKY_HOLD_TICKS (9999)

#define NK_EFFECT_STICKY_HOLD_TYPE (19)
#define NK_EFFECT_LOOP_TYPE_ONE (21)
#define NK_EFFECT_LOOP_TYPE_TWO (27)

typedef struct NkEffect
{
  NkAnimCursor animation;
  const NkAnimFrame *frame;
  s32    type;
  s32    x;
  s32    y;
  u32    draw_variant_index;
  s32    fraction_x;
  s32    fraction_y;
  s32    max_y;
  s32    delta_x;
  s32    delta_y;
  u32    last_source_tick;
  u8    bank_move_frame_first;
  u8    active;
} NkEffect;

typedef struct NkEffectPool
{
  NkEffect effects[NK_EFFECT_POOL_COUNT];
  u32    free_low[2];
  u32    free_high[2];
} NkEffectPool;

void
nk_effect_pool_reset(NkEffectPool *pool_);
void
nk_effect_pool_rebuild_free_slots(NkEffectPool *pool_);
int
nk_effect_generate(NkEffectPool *pool_,
                   nk_rng       *rng_,
                   s32           type_,
                   s32           x_,
                   s32           y_,
                   s32           dx_,
                   s32           dy_,
                   u8            direction_,
                   u8            priority_);
void
nk_effect_tick(NkEffect *effect_,
               int       sticky_blood_);
void
nk_effect_advance(NkEffect *effect_,
                  int       sticky_blood_,
                  u32       source_ticks_);
void
nk_effect_pool_tick(NkEffectPool *pool_,
                    int           sticky_blood_);
void
nk_effect_pool_advance_to_tick(NkEffectPool *pool_,
                               int           sticky_blood_,
                               u32           source_tick_);
int
nk_effect_prepare_draw(NkEffectPool *pool_,
                       u32           effect_index_,
                       int           sticky_blood_,
                       nk_rng       *rng_);
/* A null draw_modes_ advances painter state without materializing modes. */
bool
nk_effect_prepare_draw_range(NkEffectPool *pool_,
                             u32           first_,
                             u32           count_,
                             int           sticky_blood_,
                             nk_rng       *rng_,
                             u8           *draw_modes_,
                             u32          *sticky_indices_,
                             u32          *sticky_count_);
const
NkAnimFrame *
nk_effect_frame(const NkEffect *effect_);
