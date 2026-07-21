#pragma once

#include "types.h"

/*
 * Portfolio supplies the standard u8/s8/u16/s16/u32/s32 integer names.
 * Host tests provide the same exact-width contract through their types.h shim.
 * Keep 16-bit values for decoded DOS/VOL fields and explicit source-width
 * wrap/bit operations only; mutable runtime values use 32-bit integers.
 */

/*
 * C89 compile-time assertion. A unique assertion_name_ preserves a useful
 * compiler diagnostic when expression_ is false.
 */
#define nk_static_assert(expression_, assertion_name_) \
  typedef char assertion_name_[((expression_) ? 1 : -1)]

nk_static_assert(sizeof(s8) == 1, nk_check_s8);
nk_static_assert(sizeof(u8) == 1, nk_check_u8);
nk_static_assert(sizeof(s16) == 2, nk_check_s16);
nk_static_assert(sizeof(u16) == 2, nk_check_u16);
nk_static_assert(sizeof(s32) == 4, nk_check_s32);
nk_static_assert(sizeof(u32) == 4, nk_check_u32);
nk_static_assert(sizeof(bool) == 1, nk_check_bool);

#ifndef NULL
  #define NULL ((void *)0)
#endif

#define NK_U32_MAX (0xffffffffU)

#define NK_FIXED_ONE     (0x00010000)
#define NK_SUBPIXEL_SHIFT (8U)
#define NK_SUBPIXEL_ONE   (256)
#define NK_SUBPIXEL_MASK  (0xffU)
