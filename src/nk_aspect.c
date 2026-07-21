#include "nk_aspect.h"

/*
 * Exact unsigned division by five using shifts and adds.  Norcroft 4.91
 * otherwise emits a call to __rt_sdiv for every projected CEL.
 */
static
u32
_nk_aspect_divide_u32_by_5(u32    value_)
{
  u32    quotient;
  u32    remainder;

  quotient = (value_ >> 1) + (value_ >> 2);
  quotient += quotient >> 4;
  quotient += quotient >> 8;
  quotient += quotient >> 16;
  quotient >>= 2;
  remainder = value_ - quotient * 5U;
  return quotient + ((remainder + 3U) >> 3);
}


static
s32
_nk_aspect_scale_signed(s32    value_,
                        int    round_outward_)
{
  u32    magnitude;
  u32    quotient;
  u32    scaled;

  if(value_ < 0)
    {
      magnitude = 0U - (u32)value_;
    }
  else
    {
      magnitude = (u32)value_;
    }

  quotient = _nk_aspect_divide_u32_by_5(magnitude);
  scaled = magnitude + quotient;
  if((round_outward_) &&
     (magnitude - quotient * 5U != 0U))
    {
      scaled++;
    }

  if(value_ < 0)
    {
      return (s32)(0U - scaled);
    }

  return (s32)scaled;
}


void
nk_aspect_init(NkAspectState *state_)
{
  state_->mode = NK_ASPECT_CORRECT;
  state_->toggle_was_down = 0U;
}


void
nk_aspect_update(NkAspectState *state_,
                 int            toggle_down_)
{
  if((toggle_down_) && (!state_->toggle_was_down))
    {
      if(state_->mode == NK_ASPECT_CORRECT)
        {
          state_->mode = NK_ASPECT_RAW;
        }
      else
        {
          state_->mode = NK_ASPECT_CORRECT;
        }
    }

  state_->toggle_was_down = (u8)(toggle_down_ != 0);
}


s32
nk_aspect_transform_fixed_y(NkAspectMode mode_,
                            s32          value_)
{
  if(mode_ == NK_ASPECT_CORRECT)
    {
      return _nk_aspect_scale_signed(value_, false);
    }

  return value_ + NK_ASPECT_RAW_Y_OFFSET * NK_FIXED_ONE;
}


s32
nk_aspect_scale_fixed_derivative_y(s32    value_)
{
  return _nk_aspect_scale_signed(value_, true);
}


s32
nk_aspect_transform_boundary_y(NkAspectMode mode_,
                               s32          value_)
{
  if(mode_ == NK_ASPECT_CORRECT)
    {
      return _nk_aspect_scale_signed(value_, false);
    }

  return value_ + NK_ASPECT_RAW_Y_OFFSET;
}
