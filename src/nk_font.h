#pragma once

#include "nk_types.h"

#define NK_FONT_BLUE  (0U)
#define NK_FONT_RED   (1U)
#define NK_FONT_SMALL (2U)
#define NK_FONT_COUNT (3U)
#define NK_FONT_CHARACTER_COUNT (128U)

extern const s16
  nk_font_glyph_index[NK_FONT_COUNT][NK_FONT_CHARACTER_COUNT];
extern const u8
  nk_font_glyph_width[NK_FONT_COUNT][NK_FONT_CHARACTER_COUNT];
extern const u8
  nk_font_glyph_height[NK_FONT_COUNT][NK_FONT_CHARACTER_COUNT];
extern const u16    nk_font_glyph_count[NK_FONT_COUNT];

s32
nk_font_image_index(u8            font_,
                    unsigned char character_);
u8
nk_font_letter_width(u8            font_,
                     unsigned char character_);
s32
nk_font_string_width(u8          font_,
                     const char *text_,
                     int         spaced_);
bool
nk_font_data_valid(void);
