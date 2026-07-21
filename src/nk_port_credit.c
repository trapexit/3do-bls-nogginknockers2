#include "nk_port_credit.h"

#include "stddef.h"

#define NK_PORT_CREDIT_HOLD_TICKS (926U)
#define NK_PORT_CREDIT_FADE_TICKS (71U)

void
nk_port_credit_begin(NkPortCreditState *state_)
{
  if(state_ == NULL)
    {
      return;
    }

  state_->remaining_hold_ticks = NK_PORT_CREDIT_HOLD_TICKS;
  state_->remaining_fade_ticks = NK_PORT_CREDIT_FADE_TICKS;
  state_->fade_level = NK_PORT_CREDIT_FADE_MAX;
  state_->completed = 0U;
}


void
nk_port_credit_tick(NkPortCreditState *state_)
{
  if((state_ == NULL) || (state_->completed))
    {
      return;
    }

  if(state_->remaining_hold_ticks > 0U)
    {
      state_->remaining_hold_ticks--;
      return;
    }

  if(state_->remaining_fade_ticks > 0U)
    {
      state_->remaining_fade_ticks--;
      state_->fade_level = (u8)(
        (state_->remaining_fade_ticks * NK_PORT_CREDIT_FADE_MAX) /
        NK_PORT_CREDIT_FADE_TICKS
        );
    }

  if(state_->remaining_fade_ticks == 0U)
    {
      state_->completed = 1U;
    }
}


void
nk_port_credit_skip(NkPortCreditState *state_)
{
  if(state_ == NULL)
    {
      return;
    }

  state_->remaining_hold_ticks = 0U;
  state_->remaining_fade_ticks = 0U;
  state_->fade_level = 0U;
  state_->completed = 1U;
}
