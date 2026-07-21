#include "nk_font.h"

#include "stddef.h"

#define NK_FONT_FIRST_PRINTABLE_CHARACTER (30U)
#define NK_FONT_FALLBACK_GLYPH_WIDTH       (10U)
#define NK_FONT_FALLBACK_ADVANCE           (6)

s32
nk_font_image_index(u8            font_,
                    unsigned char character_)
{
  if((font_ >= NK_FONT_COUNT) || (character_ >= NK_FONT_CHARACTER_COUNT))
    {
      return -1;
    }

  return nk_font_glyph_index[font_][character_];
}


u8
nk_font_letter_width(u8            font_,
                     unsigned char character_)
{
  if((font_ >= NK_FONT_COUNT) || (character_ < NK_FONT_FIRST_PRINTABLE_CHARACTER) ||
     (character_ >= NK_FONT_CHARACTER_COUNT))
    {
      return NK_FONT_FALLBACK_GLYPH_WIDTH;
    }

  return nk_font_glyph_width[font_][character_];
}


s32
nk_font_string_width(u8          font_,
                     const char *text_,
                     int         spaced_)
{
  s32    width;
  s32    advance;
  unsigned char character;

  if((font_ >= NK_FONT_COUNT) || (text_ == NULL))
    {
      return 0;
    }

  width = 0;
  while(*text_ != '\0')
    {
      character = (unsigned char)*text_++;
      if((character >= NK_FONT_CHARACTER_COUNT) ||
         (nk_font_glyph_index[font_][character] < 0))
        {
          advance = NK_FONT_FALLBACK_ADVANCE;
        }
      else
        {
          advance = nk_font_glyph_width[font_][character];
        }

      if(!spaced_)
        {
          advance--;
        }

      width += advance;
    }

  return width;
}


bool
nk_font_data_valid(void)
{
  u32    font;
  u32    character;

  for(font = 0U; font < NK_FONT_COUNT; ++font)
    {
      if(nk_font_glyph_count[font] == 0U)
        {
          return false;
        }

      for(character = 0U;
          character < NK_FONT_CHARACTER_COUNT;
          ++character)
        {
          s32    image;

          image = nk_font_glyph_index[font][character];
          if((image < -1) ||
             ((image >= 0) &&
              ((u32)image >= (u32)nk_font_glyph_count[font])))
            {
              return false;
            }

          if((image >= 0) &&
             ((nk_font_glyph_width[font][character] == 0U) ||
                 (nk_font_glyph_height[font][character] == 0U)))
            {
              return false;
            }
        }
    }

  return true;
}
