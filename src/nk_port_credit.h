#pragma once

#include "nk_types.h"

#define NK_PORT_CREDIT_FADE_MAX (32U)

typedef struct NkPortCreditState
{
  u16    remaining_hold_ticks;
  u8    remaining_fade_ticks;
  u8    fade_level;
  u8    completed;
} NkPortCreditState;

void
nk_port_credit_begin(NkPortCreditState *state_);
void
nk_port_credit_tick(NkPortCreditState *state_);
void
nk_port_credit_skip(NkPortCreditState *state_);
