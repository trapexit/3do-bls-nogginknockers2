#pragma once

#include "nk_types.h"

/*
 * The DOS palette oracle has 33 intensities, level / 32.  The 3DO Pixel
 * Processor can produce 0 plus exact sixteenth steps for this single-CEL
 * fade.  Odd DOS levels are exact half-way ties; choosing the brighter
 * sixteenth keeps every nonzero DOS level visible and bounds the normalized
 * intensity error to 1/32.
 */
#define NK_FADE_DOS_LEVEL_MAX (32U)
#define NK_FADE_HARDWARE_LEVEL_MAX (16U)
#define NK_FADE_MAX_ERROR_NUMERATOR (1U)
#define NK_FADE_MAX_ERROR_DENOMINATOR (32U)

/*
 * Portfolio 2.5 CrossFadeCels uses the AV secondary divider to form
 * 9/16..15/16 from PDC/2 plus 1/16..7/16 PDC.  CCB_USEAV must be present
 * whenever that path is selected.  The canonical 16/16 PIXC endpoint does
 * not consume AV, but retaining the requirement for every level above 8/16
 * makes the caller's CCB contract explicit and harmless.
 */
#define NK_FADE_CCB_USEAV (0x00000400U)
#define NK_FADE_PIXC_OPAQUE (0x1f001f00U)

typedef struct NkFadeCelConfig
{
  u32    pixc;
  u32    required_ccb_flags;
  u8    hardware_level;
  u8    draw;
  u8    reserved_zero;
  u8    reserved_one;
} NkFadeCelConfig;

/*
 * Clamp and map a DOS 0..32 fade level to the best supported 3DO
 * sixteenth.  Level zero is represented by draw == false: no PIXC value
 * can multiply PDC by zero, so the runtime leaves the SPORT-cleared black
 * framebuffer untouched.  The PIXC field is a sentinel at that endpoint.
 */
void
nk_fade_configure(u8               dos_level_,
                  NkFadeCelConfig *config_);
