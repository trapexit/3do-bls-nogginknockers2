#pragma once

#include "nk_types.h"

#define NK_ASPECT_SCALE_NUMERATOR   (6)
#define NK_ASPECT_SCALE_DENOMINATOR (5)
#define NK_ASPECT_RAW_Y_OFFSET      (20)
#define NK_ASPECT_CORRECT_UNIT_VDY  (78644)

typedef enum NkAspectMode
{
  NK_ASPECT_CORRECT = 0,
  NK_ASPECT_RAW = 1
} NkAspectMode;

typedef struct NkAspectState
{
  NkAspectMode mode;
  u8    toggle_was_down;
} NkAspectState;

void
nk_aspect_init(NkAspectState *state_);
void
nk_aspect_update(NkAspectState *state_,
                 int            toggle_down_);
s32
nk_aspect_transform_fixed_y(NkAspectMode mode_,
                            s32          value_);
s32
nk_aspect_scale_fixed_derivative_y(s32    value_);
s32
nk_aspect_transform_boundary_y(NkAspectMode mode_,
                               s32          value_);
