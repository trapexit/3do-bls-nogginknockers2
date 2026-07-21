#include "nk_compositor.h"

#include "nk_font.h"

#include "string.h"

#define NK_SELECTOR_CHARACTER_COUNT (8)
#define NK_PORT_CREDIT_IMAGE_X (177)
#define NK_PORT_CREDIT_IMAGE_Y (19)
#define NK_PORT_CREDIT_TEXT_X (30)
#define NK_PORT_CREDIT_TEXT_Y (87)
#define NK_PORT_CREDIT_LINE_SPACING (24)
const NkCompositorEffectCompositeLayout nk_compositor_effect_composite_layouts[
  NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_COUNT * 2U
] =
{
  { -15, 60, 33U, 22U },
  { -18, 60, 33U, 22U },
  { -17, 57, 40U, 25U },
  { -23, 57, 40U, 25U },
  { -21, 55, 46U, 27U },
  { -25, 55, 46U, 27U },
  { -25, 57, 52U, 25U },
  { -27, 57, 52U, 25U },
  { -26, 61, 54U, 21U },
  { -28, 61, 54U, 21U },
  { -28, 65, 58U, 18U },
  { -30, 65, 58U, 18U }
};

static const u8    g_SELECTOR_NORMAL_SERIES[
  NK_SELECTOR_CHARACTER_COUNT
] =
{
  5U,
  1U,
  7U,
  3U,
  6U,
  4U,
  0U,
  2U
};

static const u8    g_SELECTOR_SCREAM_SERIES[
  NK_SELECTOR_CHARACTER_COUNT
] =
{
  9U,
  11U,
  13U,
  12U,
  14U,
  15U,
  8U,
  10U
};

static const u8    g_SELECTOR_SERIES_CHARACTER[
  NK_SELECTOR_CHARACTER_COUNT
] =
{
  NK_CHARACTER_BUDDY,
  NK_CHARACTER_FETUS,
  NK_CHARACTER_GONZOLES,
  NK_CHARACTER_GURDIP,
  NK_CHARACTER_SINAMMON,
  NK_CHARACTER_KLUBBOR,
  NK_CHARACTER_ED,
  NK_CHARACTER_HENRY
};


static
bool
_nk_compositor_emit(const NkCompositorSink    *sink_,
                    const NkCompositorCommand *command_)
{
  if((sink_ == NULL) ||
     (sink_->emit == (NkCompositorEmit)0) ||
     (command_ == NULL))
    {
      return false;
    }

  return sink_->emit(sink_->context, command_) != 0;
}


static
bool
_nk_compositor_emit_effect_draw(const NkCompositorSink *sink_,
                                u32                     image_index_,
                                s32                     x_,
                                s32                     y_,
                                u8                      orientation_)
{
  NkCompositorImageRef image;

  if((sink_ != NULL) &&
     (sink_->draw_effect != (NkCompositorEffectDraw)0))
    {
      return sink_->draw_effect(
        sink_->context,
        image_index_,
        x_,
        y_,
        orientation_
        ) != 0;
    }

  image.kind = (u8)NK_COMPOSITOR_IMAGE_ANIMATION;
  image.bank_index = NK_EFFECT_BANK_INDEX;
  image.index = (u16)image_index_;
  return nk_compositor_emit_draw(sink_, image, x_, y_, orientation_);
}


bool
nk_compositor_emit_begin(const NkCompositorSink *sink_,
                         NkCompositorPaletteRef  palette_,
                         u8                      fade_level_)
{
  NkCompositorCommand command;

  command.type = (u8)NK_COMPOSITOR_BEGIN;
  command.value.begin.palette = palette_;
  if(fade_level_ > NK_COMPOSITOR_FADE_MAX)
    {
      fade_level_ = NK_COMPOSITOR_FADE_MAX;
    }

  command.value.begin.fade_level = fade_level_;
  return _nk_compositor_emit(sink_, &command);
}


bool
nk_compositor_emit_clear(const NkCompositorSink *sink_,
                         u8                      color_)
{
  NkCompositorCommand command;

  command.type = (u8)NK_COMPOSITOR_CLEAR;
  command.value.clear.color = color_;
  return _nk_compositor_emit(sink_, &command);
}


bool
nk_compositor_emit_clip(const NkCompositorSink *sink_,
                        s32                     x_,
                        s32                     y_,
                        s32                     width_,
                        s32                     height_)
{
  NkCompositorCommand command;

  command.type = (u8)NK_COMPOSITOR_CLIP;
  command.value.clip.x = x_;
  command.value.clip.y = y_;
  command.value.clip.width = width_;
  command.value.clip.height = height_;
  return _nk_compositor_emit(sink_, &command);
}


bool
nk_compositor_emit_draw(const NkCompositorSink *sink_,
                        NkCompositorImageRef    image_,
                        s32                     x_,
                        s32                     y_,
                        u8                      orientation_)
{
  NkCompositorCommand command;

  command.type = (u8)NK_COMPOSITOR_DRAW;
  command.value.draw.image = image_;
  command.value.draw.x = x_;
  command.value.draw.y = y_;
  command.value.draw.orientation = orientation_;
  return _nk_compositor_emit(sink_, &command);
}


bool
nk_compositor_emit_fill(const NkCompositorSink *sink_,
                        s32                     x_,
                        s32                     y_,
                        s32                     width_,
                        s32                     height_,
                        u8                      color_)
{
  NkCompositorCommand command;

  command.type = (u8)NK_COMPOSITOR_FILL;
  command.value.fill.x = x_;
  command.value.fill.y = y_;
  command.value.fill.width = width_;
  command.value.fill.height = height_;
  command.value.fill.color = color_;
  return _nk_compositor_emit(sink_, &command);
}


bool
nk_compositor_emit_end(const NkCompositorSink *sink_)
{
  NkCompositorCommand command;

  command.type = (u8)NK_COMPOSITOR_END;
  return _nk_compositor_emit(sink_, &command);
}


bool
nk_compositor_emit_sticky_surface(const NkCompositorSink *sink_)
{
  NkCompositorCommand command;

  command.type = (u8)NK_COMPOSITOR_STICKY_SURFACE;
  return _nk_compositor_emit(sink_, &command);
}


static
NkCompositorImageRef
_nk_compositor_scene_image(u8     kind_,
                           u8     bank_index_,
                           u32    image_)
{
  NkCompositorImageRef reference;

  reference.kind = kind_;
  reference.bank_index = bank_index_;
  reference.index = (u16)image_;
  return reference;
}


static
NkCompositorImageRef
_nk_compositor_scene_layer(u8     bank_index_,
                           u32    layer_)
{
  NkCompositorImageRef reference;

  reference.kind = (u8)NK_COMPOSITOR_IMAGE_SCENE_LAYER;
  reference.bank_index = bank_index_;
  reference.index = (u16)layer_;
  return reference;
}


static
NkCompositorImageRef
_nk_compositor_selector_mark(u32    player_)
{
  NkCompositorImageRef reference;

  reference.kind = (u8)NK_COMPOSITOR_IMAGE_SELECTOR_MARK;
  reference.bank_index = NK_SCENE_BANK_SELECT;
  reference.index = (u16)player_;
  return reference;
}


static
bool
_nk_compositor_draw_wrapped_layer(const NkCompositorSink *sink_,
                                  u8                      bank_index_,
                                  u32                     layer_,
                                  s32                     scroll_,
                                  s32                     y_,
                                  s32                     positive_y_delta_)
{
  NkCompositorImageRef image;

  image = _nk_compositor_scene_layer(bank_index_, layer_);
  while(scroll_ >= NK_COMPOSITOR_WIDTH)
    {
      scroll_ -= NK_COMPOSITOR_WIDTH;
    }
  while(scroll_ < 0)
    {
      scroll_ += NK_COMPOSITOR_WIDTH;
    }

  if(!nk_compositor_emit_draw(sink_, image, -scroll_, y_, 0U))
    {
      return false;
    }

  if((scroll_ != 0) &&
     (!nk_compositor_emit_draw(
        sink_,
        image,
        NK_COMPOSITOR_WIDTH - scroll_,
        y_ + positive_y_delta_,
        0U)))
    {
      return false;
    }

  return true;
}


static
bool
_nk_compositor_draw_selector_mark(const NkSelectorView   *view_,
                                  const NkCompositorSink *sink_,
                                  int                     player_)
{
  s32    cursor;

  cursor = view_->selection->cursor[player_];
  if((cursor < 0) || (cursor >= NK_SELECTOR_CHARACTER_COUNT))
    {
      return false;
    }

  return nk_compositor_emit_draw(
    sink_,
    _nk_compositor_selector_mark((u32)player_),
    3 + (cursor & 3) * 80,
    1 + (cursor >> 2) * 100,
    0U
    );
}


static
bool
_nk_compositor_draw_selector_marks(const NkSelectorView   *view_,
                                   const NkCompositorSink *sink_)
{
  const NkSelectState *selection;

  selection = view_->selection;
  if(selection->locked[0] != 0)
    {
      if(!_nk_compositor_draw_selector_mark(view_, sink_, 0))
        {
          return false;
        }

      if((selection->locked[1] != 0) ||
         ((selection->tick & 8U) != 0U))
        {
          if(!_nk_compositor_draw_selector_mark(view_, sink_, 1))
            {
              return false;
            }
        }
    }
  else if(selection->locked[1] != 0)
    {
      if(!_nk_compositor_draw_selector_mark(view_, sink_, 1))
        {
          return false;
        }

      if((selection->tick & 8U) != 0U)
        {
          if(!_nk_compositor_draw_selector_mark(view_, sink_, 0))
            {
              return false;
            }
        }
    }
  else if((selection->tick & 8U) != 0U)
    {
      if(!_nk_compositor_draw_selector_mark(view_, sink_, 0))
        {
          return false;
        }
    }
  else
    {
      if(!_nk_compositor_draw_selector_mark(view_, sink_, 1))
        {
          return false;
        }
    }

  return true;
}


static
bool
_nk_compositor_draw_selector_portraits(const NkSelectorView   *view_,
                                       const NkCompositorSink *sink_)
{
  const NkSceneFrame *frame;
  s32    screamer;
  u8    character;
  u8    image_kind;
  int index;

  screamer = view_->selection->screamer;
  for(index = 0; index < NK_SELECTOR_CHARACTER_COUNT; ++index)
    {
      if((screamer >= 0) &&
         (screamer < NK_SELECTOR_CHARACTER_COUNT) &&
         (g_SELECTOR_NORMAL_SERIES[screamer] == (u8)index))
        {
          continue;
        }

      frame = nk_scene_series_frame(view_->scene, (u8)index);
      if((frame == NULL) ||
         (frame->image == NK_ANIM_NO_IMAGE))
        {
          continue;
        }

      character = g_SELECTOR_SERIES_CHARACTER[index];
      if((view_->selection->defeated_mask &
          (1L << character)) != 0)
        {
          image_kind = (u8)NK_COMPOSITOR_IMAGE_SCENE_GRAY;
        }
      else
        {
          image_kind = (u8)NK_COMPOSITOR_IMAGE_SCENE;
        }

      if(!nk_compositor_emit_draw(
           sink_,
           _nk_compositor_scene_image(
             image_kind,
             NK_SCENE_BANK_SELECT,
             frame->image
             ),
           frame->x,
           frame->y,
           0U))
        {
          return false;
        }
    }

  return true;
}


static
bool
_nk_compositor_draw_selector_screamer(const NkSelectorView   *view_,
                                      const NkCompositorSink *sink_)
{
  const NkSceneFrame *frame;
  s32    screamer;

  screamer = view_->selection->screamer;
  if((screamer < 0) ||
     (screamer >= NK_SELECTOR_CHARACTER_COUNT))
    {
      return true;
    }

  frame = nk_scene_series_frame(
    view_->scene,
    g_SELECTOR_SCREAM_SERIES[screamer]
    );
  if((frame == NULL) ||
     (frame->image == NK_ANIM_NO_IMAGE))
    {
      return true;
    }

  return nk_compositor_emit_draw(
    sink_,
    _nk_compositor_scene_image(
      (u8)NK_COMPOSITOR_IMAGE_SCENE,
      NK_SCENE_BANK_SELECT,
      frame->image
      ),
    frame->x,
    frame->y,
    0U
    );
}


bool
nk_compositor_compose_selector(const NkSelectorView   *view_,
                               const NkCompositorSink *sink_)
{
  NkCompositorPaletteRef palette;
  NkCompositorImageRef layer;
  s32    giger;

  if((view_ == NULL) ||
     (view_->selection == NULL) ||
     (view_->scene == NULL) ||
     (!view_->scene->valid) ||
     (view_->scene->bank_index != NK_SCENE_BANK_SELECT))
    {
      return false;
    }

  palette.kind = (u8)NK_COMPOSITOR_PALETTE_SCENE;
  palette.bank_index = NK_SCENE_BANK_SELECT;
  palette.index = 0U;
  if((!nk_compositor_emit_begin(
        sink_,
        palette,
        view_->selection->fade_level)) ||
     (!nk_compositor_emit_clear(sink_, 0U)))
    {
      return false;
    }

  if(!_nk_compositor_draw_wrapped_layer(
       sink_,
       NK_SCENE_BANK_SELECT,
       NK_COMPOSITOR_LAYER_3,
       view_->scene_scroll,
       0,
       0))
    {
      return false;
    }

  giger = (s32)(view_->selection->tick & 63U);
  if((view_->selection->tick & 64U) != 0U)
    {
      giger ^= 63;
    }

  giger >>= 2;
  if(!_nk_compositor_draw_wrapped_layer(
       sink_,
       NK_SCENE_BANK_SELECT,
       NK_COMPOSITOR_LAYER_2,
       giger,
       0,
       -1))
    {
      return false;
    }

  layer = _nk_compositor_scene_layer(
    NK_SCENE_BANK_SELECT,
    NK_COMPOSITOR_LAYER_1A
    );
  if(!nk_compositor_emit_draw(sink_, layer, 0, 0, 0U))
    {
      return false;
    }

  layer = _nk_compositor_scene_layer(
    NK_SCENE_BANK_SELECT,
    NK_COMPOSITOR_LAYER_1B
    );
  if(!nk_compositor_emit_draw(sink_, layer, 0, 100, 0U))
    {
      return false;
    }

  if((!_nk_compositor_draw_selector_portraits(view_, sink_)) ||
     (!_nk_compositor_draw_selector_marks(view_, sink_)) ||
     (!_nk_compositor_draw_selector_screamer(view_, sink_)))
    {
      return false;
    }

  return nk_compositor_emit_end(sink_);
}


static
bool
_nk_compositor_draw_scene_series_y_xor(const NkSceneState     *scene_,
                                       const NkCompositorSink *sink_,
                                       u8                      series_index_,
                                       s32                     offset_x_,
                                       s32                     offset_y_,
                                       u8                      y_xor_)
{
  const NkSceneFrame *frame;

  frame = nk_scene_series_frame(scene_, series_index_);
  if((frame == NULL) ||
     (frame->image == NK_ANIM_NO_IMAGE))
    {
      return true;
    }

  return nk_compositor_emit_draw(
    sink_,
    _nk_compositor_scene_image(
      (u8)NK_COMPOSITOR_IMAGE_SCENE,
      scene_->bank_index,
      frame->image
      ),
    frame->x + offset_x_,
    ((s32)frame->y ^ (s32)y_xor_) + offset_y_,
    0U
    );
}


static
bool
_nk_compositor_draw_scene_series(const NkSceneState     *scene_,
                                 const NkCompositorSink *sink_,
                                 u8                      series_index_,
                                 s32                     offset_x_,
                                 s32                     offset_y_)
{
  return _nk_compositor_draw_scene_series_y_xor(
    scene_,
    sink_,
    series_index_,
    offset_x_,
    offset_y_,
    0U
    );
}


bool
nk_compositor_compose_title(const NkTitleView      *view_,
                            const NkCompositorSink *sink_)
{
  const nk_title_state *title;
  NkCompositorPaletteRef palette;
  NkCompositorImageRef layer;
  s32    layer_two_scroll;
  s32    layer_three_scroll;
  u8    title_y_xor;

  if((view_ == NULL) ||
     (view_->title == NULL) ||
     (view_->scene == NULL) ||
     (!view_->scene->valid) ||
     (view_->scene->bank_index != NK_SCENE_BANK_TITLE) ||
     (view_->title->choice > NK_TITLE_OPTIONS))
    {
      return false;
    }

  title = view_->title;
  layer_two_scroll = (s32)((title->tick / 4U) % 320U);
  layer_three_scroll = (s32)((title->tick / 8U) % 320U);
  /* title.cpp flips the low Y bit of both logo frames every eight ticks. */
  title_y_xor = (u8)((title->tick >> 3) & 1U);

  palette.kind = (u8)NK_COMPOSITOR_PALETTE_SCENE;
  palette.bank_index = NK_SCENE_BANK_TITLE;
  palette.index = 0U;
  if((!nk_compositor_emit_begin(
        sink_,
        palette,
        title->fade_level)) ||
     (!nk_compositor_emit_clear(sink_, 0U)) ||
     (!nk_compositor_emit_clip(
        sink_,
        0,
        104,
        NK_COMPOSITOR_WIDTH,
        96)))
    {
      return false;
    }

  /*
   * title.cpp submits the lower 100-line overlap first, then copies source
   * rows 0..194 to VGA rows 5..199.  Commands use absolute coordinates in
   * that final 320x200 VGA image.  The widened clips preserve the even
   * SetClipOrigin requirement; the upper pass replaces row 104 and the
   * five-row fill restores the untouched VGA rows 0..4.
   */
  if((!_nk_compositor_draw_wrapped_layer(
        sink_,
        NK_SCENE_BANK_TITLE,
        NK_COMPOSITOR_LAYER_3,
        layer_three_scroll,
        13,
        0)) ||
     (!_nk_compositor_draw_wrapped_layer(
        sink_,
        NK_SCENE_BANK_TITLE,
        NK_COMPOSITOR_LAYER_2,
        layer_two_scroll,
        10,
        -1)))
    {
      return false;
    }

  layer = _nk_compositor_scene_layer(
    NK_SCENE_BANK_TITLE,
    NK_COMPOSITOR_LAYER_1B
    );
  if((!nk_compositor_emit_draw(sink_, layer, 0, 105, 0U)) ||
     (!nk_compositor_emit_clip(
        sink_,
        0,
        4,
        NK_COMPOSITOR_WIDTH,
        101)) ||
     (!_nk_compositor_draw_wrapped_layer(
        sink_,
        NK_SCENE_BANK_TITLE,
        NK_COMPOSITOR_LAYER_3,
        layer_three_scroll,
        3,
        0)) ||
     (!_nk_compositor_draw_wrapped_layer(
        sink_,
        NK_SCENE_BANK_TITLE,
        NK_COMPOSITOR_LAYER_2,
        layer_two_scroll,
        0,
        -1)))
    {
      return false;
    }

  layer = _nk_compositor_scene_layer(
    NK_SCENE_BANK_TITLE,
    NK_COMPOSITOR_LAYER_1A
    );
  if((!nk_compositor_emit_draw(sink_, layer, 0, 5, 0U)) ||
     (!nk_compositor_emit_clip(
        sink_,
        0,
        0,
        NK_COMPOSITOR_WIDTH,
        NK_COMPOSITOR_HEIGHT)) ||
     (!nk_compositor_emit_fill(
        sink_,
        0,
        0,
        NK_COMPOSITOR_WIDTH,
        5,
        0U)) ||
     (!_nk_compositor_draw_scene_series(
        view_->scene,
        sink_,
        title->button_push ? 4U : 1U,
        0,
        5)))
    {
      return false;
    }

  if(title->show_choice)
    {
      if((!_nk_compositor_draw_scene_series(
            view_->scene,
            sink_,
            2U,
            0,
            5)) ||
         (!_nk_compositor_draw_scene_series(
            view_->scene,
            sink_,
            3U,
            0,
            5 + (s32)title->choice * 25)))
        {
          return false;
        }
    }

  if((!_nk_compositor_draw_scene_series_y_xor(
        view_->scene,
        sink_,
        5U,
        0,
        5,
        title_y_xor)) ||
     (!_nk_compositor_draw_scene_series_y_xor(
        view_->scene,
        sink_,
        6U,
        0,
        5,
        title_y_xor)))
    {
      return false;
    }

  if((view_->voice_playing) &&
     (!_nk_compositor_draw_scene_series(
        view_->scene,
        sink_,
        0U,
        0,
        5)))
    {
      return false;
    }

  return nk_compositor_emit_end(sink_);
}


static
unsigned char
_nk_compositor_font_character(unsigned char character_,
                              int           uppercase_)
{
  if((uppercase_) &&
     (character_ >= (unsigned char)'a') &&
     (character_ <= (unsigned char)'z'))
    {
      return (unsigned char)(character_ - ('a' - 'A'));
    }

  return character_;
}


static
u8
_nk_compositor_default_font_kind(u8    source_font_)
{
  if(source_font_ == NK_FONT_BLUE)
    {
      return (u8)NK_COMPOSITOR_IMAGE_FONT_BLUE;
    }

  if(source_font_ == NK_FONT_RED)
    {
      return (u8)NK_COMPOSITOR_IMAGE_FONT_RED;
    }

  return (u8)NK_COMPOSITOR_IMAGE_FONT_SMALL_WHITE;
}


static
NkCompositorImageRef
_nk_compositor_font_image(u8            image_kind_,
                          u8            source_font_,
                          unsigned char character_)
{
  NkCompositorImageRef reference;

  reference.kind = image_kind_;
  reference.bank_index = source_font_;
  reference.index = (u16)character_;
  return reference;
}


static
s32
_nk_compositor_font_width(u8          source_font_,
                          const char *text_,
                          int         spaced_,
                          int         uppercase_)
{
  s32    width;
  s32    advance;
  unsigned char character;

  width = 0;
  while((text_ != NULL) && (*text_ != '\0'))
    {
      character = _nk_compositor_font_character(
        (unsigned char)*text_,
        uppercase_
        );
      if(nk_font_image_index(source_font_, character) < 0)
        {
          advance = 6;
        }
      else
        {
          advance = nk_font_letter_width(
            source_font_,
            character
            );
          if(!spaced_)
            {
              advance--;
            }
        }

      width += advance;
      text_++;
    }

  return width;
}


static
bool
_nk_compositor_draw_font_text_kind(const NkCompositorSink *sink_,
                                   u8                      image_kind_,
                                   u8                      source_font_,
                                   s32                     x_,
                                   s32                     y_,
                                   const char             *text_,
                                   int                     spaced_,
                                   int                     uppercase_)
{
  s32    image_index;
  s32    advance;
  unsigned char character;

  if((source_font_ >= NK_FONT_COUNT) ||
     (text_ == NULL))
    {
      return false;
    }
  while(*text_ != '\0')
    {
      character = _nk_compositor_font_character(
        (unsigned char)*text_,
        uppercase_
        );
      image_index = nk_font_image_index(source_font_, character);
      if(image_index >= 0)
        {
          if(!nk_compositor_emit_draw(
               sink_,
               _nk_compositor_font_image(
                 image_kind_,
                 source_font_,
                 character
                 ),
               x_,
               y_,
               0U))
            {
              return false;
            }

          advance = nk_font_letter_width(
            source_font_,
            character
            );
        }
      else
        {
          advance = 6;
        }

      if(!spaced_)
        {
          advance--;
        }

      x_ += advance;
      text_++;
    }

  return true;
}


static
bool
_nk_compositor_draw_font_text(const NkCompositorSink *sink_,
                              u8                      source_font_,
                              s32                     x_,
                              s32                     y_,
                              const char             *text_,
                              int                     spaced_,
                              int                     uppercase_)
{
  return _nk_compositor_draw_font_text_kind(
    sink_,
    _nk_compositor_default_font_kind(source_font_),
    source_font_,
    x_,
    y_,
    text_,
    spaced_,
    uppercase_
    );
}


static
bool
_nk_compositor_draw_big_word(const NkCompositorSink *sink_,
                             const char             *text_,
                             s32                     x_,
                             s32                     baseline_)
{
  s32    width;

  width = _nk_compositor_font_width(
    NK_FONT_RED,
    text_,
    false,
    true
    );
  return _nk_compositor_draw_font_text(
    sink_,
    NK_FONT_RED,
    x_ - width,
    baseline_,
    text_,
    false,
    true) &&
         _nk_compositor_draw_font_text(
    sink_,
    NK_FONT_RED,
    x_ + 1,
    baseline_ - 12,
    ".",
    false,
    true) &&
         _nk_compositor_draw_font_text(
    sink_,
    NK_FONT_RED,
    x_ + 1,
    baseline_ - 5,
    ".",
    false,
    true);
}


static
NkCompositorImageRef
_nk_compositor_animation_image(u8     bank_index_,
                               u32    image_index_)
{
  NkCompositorImageRef reference;

  reference.kind = (u8)NK_COMPOSITOR_IMAGE_ANIMATION;
  reference.bank_index = bank_index_;
  reference.index = (u16)image_index_;
  return reference;
}


static
bool
_nk_compositor_draw_animation_frame(const NkCompositorSink *sink_,
                                    const NkAnimFrame      *frame_,
                                    u8                      bank_index_,
                                    s32                     x_,
                                    s32                     y_)
{
  const NkAnimImageRef *image;
  NkAnimProjectedImage projected;
  u32    image_index;

  if(frame_ == NULL)
    {
      return true;
    }

  for(image_index = 0U;
      image_index < frame_->image_count;
      ++image_index)
    {
      image = nk_anim_frame_image(frame_, image_index);
      if(image == NULL)
        {
          return false;
        }

      nk_anim_project_image(
        image,
        x_,
        y_,
        -80,
        0U,
        &projected
        );
      if(!nk_compositor_emit_draw(
           sink_,
           _nk_compositor_animation_image(
             bank_index_,
             projected.image
             ),
           projected.x,
           projected.y,
           projected.orientation))
        {
          return false;
        }
    }

  return true;
}


static
void
_nk_compositor_points_text(s32    round_setting_,
                           char   text_[10])
{
  static const char suffix[] = " POINTS";
  s32    points;
  int index;
  int suffix_index;

  points = round_setting_;
  index = 0;
  if(points >= 10)
    {
      text_[index] = (char)('0' + points / 10);
      index++;
    }

  text_[index] = (char)('0' + points % 10);
  index++;
  suffix_index = 0;
  while(suffix[suffix_index] != '\0')
    {
      text_[index] = suffix[suffix_index];
      index++;
      suffix_index++;
    }

  text_[index] = '\0';
}


bool
nk_compositor_compose_options(const NkOptionsView    *view_,
                              const NkCompositorSink *sink_)
{
  static const char *labels[NK_OPTION_COUNT] =
    {
      "DIFFICULTY",
      "SOUND VOL",
      "MUSIC VOL",
      "TALKING",
      "ROUND WIN",
      "FIX BUGS"
    };
  const nk_options *options;
  const NkAnimFrame *skull_frame;
  NkCompositorPaletteRef palette;
  char value[10];
  s32    levels[NK_OPTION_COUNT];
  s32    option_tick;
  s32    x;
  s32    y;
  int index;
  int skull_index;

  if((view_ == NULL) ||
     (view_->options == NULL) ||
     (view_->scene == NULL) ||
     (view_->skull == NULL) ||
     (!view_->scene->valid) ||
     (view_->scene->bank_index != NK_SCENE_BANK_SELECT) ||
     (view_->options->cursor > NK_OPTION_EXIT))
    {
      return false;
    }

  options = &view_->options->pending;
  if((options->difficulty < NK_DIFFICULTY_MIN) ||
     (options->difficulty > NK_DIFFICULTY_MAX) ||
     (options->sound_volume < NK_VOLUME_MIN) ||
     (options->sound_volume > NK_VOLUME_MAX) ||
     (options->music_volume < NK_VOLUME_MIN) ||
     (options->music_volume > NK_VOLUME_MAX) ||
     (options->talking < NK_TOGGLE_OFF) ||
     (options->talking > NK_TOGGLE_ON) ||
     (options->round_setting < NK_ROUND_WIN_MIN) ||
     (options->round_setting > NK_ROUND_WIN_MAX) ||
     (options->fix_orig_bugs < NK_TOGGLE_OFF) ||
     (options->fix_orig_bugs > NK_TOGGLE_ON))
    {
      return false;
    }

  palette.kind = (u8)NK_COMPOSITOR_PALETTE_SCENE;
  palette.bank_index = NK_SCENE_BANK_SELECT;
  palette.index = 0U;
  if((!nk_compositor_emit_begin(
        sink_,
        palette,
        view_->options->fade_level)) ||
     (!nk_compositor_emit_clear(sink_, 0U)) ||
     (!_nk_compositor_draw_wrapped_layer(
        sink_,
        NK_SCENE_BANK_SELECT,
        NK_COMPOSITOR_LAYER_3,
        (view_->scene_scroll % 640) / 2,
        20,
        0)) ||
     (!_nk_compositor_draw_wrapped_layer(
        sink_,
        NK_SCENE_BANK_SELECT,
        NK_COMPOSITOR_LAYER_2,
        view_->scene_scroll % 320,
        20,
        -1)))
    {
      return false;
    }

  levels[0] = options->difficulty;
  levels[1] = options->sound_volume;
  levels[2] = options->music_volume;
  levels[3] = options->talking;
  levels[4] = options->round_setting;
  levels[5] = options->fix_orig_bugs;
  option_tick = view_->scene_scroll - 160;
  skull_frame = nk_anim_cursor_frame(view_->skull);
  for(index = 0; index < NK_OPTION_COUNT; ++index)
    {
      y = (12 + (index * 21));
      x = 185;
      if(((int)view_->options->cursor != index) ||
         ((option_tick & 32) != 0))
        {
          if(!_nk_compositor_draw_big_word(
               sink_,
               labels[index],
               170,
               y + 20))
            {
              return false;
            }
        }

      if(index < NK_OPTION_LEVEL_COUNT)
        {
          for(skull_index = 0;
              skull_index < levels[index];
              ++skull_index)
            {
              if(!_nk_compositor_draw_animation_frame(
                   sink_,
                   skull_frame,
                   9U,
                   x + skull_index * 14,
                   y + 30))
                {
                  return false;
                }
            }
        }
      else if(index == NK_OPTION_TALKING)
        {
          if(!_nk_compositor_draw_font_text(sink_,
                                            NK_FONT_BLUE,
                                            x,
                                            (y + 23),
                                            levels[index] ? "ON" : "OFF",
                                            true,
                                            false))
            {
              return false;
            }
        }
      else if(index == NK_OPTION_FIX_ORIG_BUGS)
        {
          if(!_nk_compositor_draw_font_text(sink_,
                                            NK_FONT_BLUE,
                                            x,
                                            (y + 23),
                                            levels[index] ? "TRUE" : "FALSE",
                                            true,
                                            false))
            {
              return false;
            }
        }
      else
        {
          _nk_compositor_points_text(levels[index], value);
          if(!_nk_compositor_draw_font_text(sink_,
                                            NK_FONT_BLUE,
                                            x,
                                            (y + 23),
                                            value,
                                            true,
                                            false))
            {
              return false;
            }
        }
    }

  if((view_->options->cursor != NK_OPTION_EXIT) ||
     ((option_tick & 32) != 0))
    {
      if(!_nk_compositor_draw_font_text(
           sink_,
           NK_FONT_RED,
           120,
           155,
           "EXIT",
           false,
           true))
        {
          return false;
        }
    }

  if((!nk_compositor_emit_fill(
        sink_,
        0,
        -20,
        NK_COMPOSITOR_WIDTH,
        50,
        0U)) ||
     (!nk_compositor_emit_fill(
        sink_,
        0,
        177,
        NK_COMPOSITOR_WIDTH,
        43,
        0U)))
    {
      return false;
    }

  return nk_compositor_emit_end(sink_);
}


static
NkCompositorImageRef
_nk_compositor_pain_image(u8     bank_index_,
                          u32    image_index_)
{
  NkCompositorImageRef reference;

  reference.kind = (u8)NK_COMPOSITOR_IMAGE_ANIMATION_PAIN;
  reference.bank_index = bank_index_;
  reference.index = (u16)image_index_;
  return reference;
}


static
bool
_nk_compositor_image_visible(const NkAnimProjectedImage *projected_,
                             const NkAnimImageSize      *size_)
{
  if((projected_ == NULL) ||
     (size_ == NULL))
    {
      return false;
    }

  return projected_->x < NK_COMPOSITOR_WIDTH &&
         projected_->x > -(s32)size_->width &&
         projected_->y < NK_COMPOSITOR_HEIGHT &&
         projected_->y > -(s32)size_->height;
}


static
bool
_nk_compositor_draw_match_frame(const NkCompositorSink *sink_,
                                const NkAnimFrame      *frame_,
                                u8                      bank_index_,
                                s32                     x_,
                                s32                     y_,
                                u8                      facing_,
                                const NkGamePlayer     *pain_owner_,
                                int                     cull_offscreen_)
{
  const NkAnimImageRef *image;
  const NkAnimImageRef *pain_image;
  const NkAnimImageSize *base_size;
  const NkAnimImageSize *pain_size;
  const NkAnimPainLayout *layout;
  NkAnimProjectedImage projected;
  s32    pain_x;
  s32    pain_y;
  u32    image_index;
  u32    pain_index;

  if(frame_ == NULL)
    {
      return true;
    }

  for(image_index = 0U;
      image_index < frame_->image_count;
      ++image_index)
    {
      image = nk_anim_frame_image(frame_, image_index);
      if(image == NULL)
        {
          return false;
        }

      nk_anim_project_image(
        image,
        x_,
        y_,
        -80,
        facing_,
        &projected
        );
      if(cull_offscreen_)
        {
          base_size = nk_anim_image_size(
            bank_index_,
            projected.image
            );
          if(base_size == NULL)
            {
              return false;
            }

          if(!_nk_compositor_image_visible(
               &projected,
               base_size))
            {
              continue;
            }
        }

      if(!nk_compositor_emit_draw(
           sink_,
           _nk_compositor_animation_image(
             bank_index_,
             projected.image
             ),
           projected.x,
           projected.y,
           projected.orientation))
        {
          return false;
        }

      if((pain_owner_ == NULL) ||
         (pain_owner_->pain <= 0))
        {
          continue;
        }

      layout = nk_anim_pain_layout(bank_index_, image->image);
      if(layout == NULL)
        {
          continue;
        }

      base_size = nk_anim_image_size(bank_index_, projected.image);
      if(base_size == NULL)
        {
          return false;
        }

      for(pain_index = 0U;
          pain_index < layout->image_count;
          ++pain_index)
        {
          pain_image = nk_anim_pain_layout_image(
            layout,
            pain_index
            );
          if(pain_image == NULL)
            {
              return false;
            }

          if((s32)((pain_image->orientation >> 2) & 3U) !=
             pain_owner_->pain)
            {
              continue;
            }

          pain_size = nk_anim_pain_image_size(
            bank_index_,
            pain_image->image
            );
          if(pain_size == NULL)
            {
              return false;
            }

          if((projected.orientation & 2U) != 0U)
            {
              pain_x = projected.x + pain_image->x_flipped;
            }
          else
            {
              pain_x = projected.x + pain_image->x_normal;
            }

          pain_y = pain_image->y;
          if((projected.orientation & 1U) != 0U)
            {
              pain_y = -pain_y + base_size->height -
                       pain_size->height;
            }

          pain_y += projected.y;
          if(!nk_compositor_emit_draw(
               sink_,
               _nk_compositor_pain_image(
                 bank_index_,
                 pain_image->image
                 ),
               pain_x,
               pain_y,
               (u8)(
                 projected.orientation ^
                 pain_image->orientation
                 )))
            {
              return false;
            }
        }
    }

  return true;
}


static
bool
_nk_compositor_draw_match_player(const NkCompositorSink *sink_,
                                 const NkGamePlayer     *player_,
                                 s32                     x_,
                                 s32                     y_)
{
  return _nk_compositor_draw_match_frame(
    sink_,
    nk_game_player_frame(player_),
    player_->animation_bank,
    x_,
    y_,
    player_->facing,
    player_,
    false
    );
}


static
bool
_nk_compositor_draw_match_projectiles(const NkMatchView      *view_,
                                      const NkCompositorSink *sink_,
                                      const NkGamePlayer     *owner_)
{
  const NkProjectile *projectile;
  s32    x;
  s32    y;
  u32    index;

  for(index = 0U; index < NK_GAME_PROJECTILE_COUNT; ++index)
    {
      projectile = &owner_->projectiles[index];
      if(projectile->active == 0U)
        {
          continue;
        }

      nk_match_present_projectile_position(
        view_->presentation,
        owner_,
        index,
        &x,
        &y
        );
      if(!_nk_compositor_draw_match_frame(
           sink_,
           nk_anim_cursor_frame(&projectile->animation),
           owner_->animation_bank,
           x,
           y,
           projectile->facing,
           owner_,
           false))
        {
          return false;
        }
    }

  return true;
}


static
bool
_nk_compositor_draw_match_effects(const NkMatchView      *view_,
                                  const NkCompositorSink *sink_,
                                  u32                     first_,
                                  u32                     count_)
{
  const NkAnimBank *bank;
  const NkAnimFrame *frame;
  const NkAnimImageRef *image;
  const NkAnimImageSize *size;
  const NkCompositorEffectCompositeLayout *composite;
  const NkEffect *effect;
  s32    x;
  s32    y;
  u32    composite_index;
  u32    frame_index;
  u32    image_index;
  u32    variant_index;
  const u8    *mode_cursor;
  u8    mode;
  u8    orientation;

  bank = &nk_anim_banks[NK_EFFECT_BANK_INDEX];
  effect = &view_->game->effects.effects[first_];
  mode_cursor = &view_->presentation->effect_draw_modes[first_];
  if(sink_->draw_effect_range != (NkCompositorEffectRangeDraw)0)
    {
      return sink_->draw_effect_range(
        sink_->context,
        effect,
        mode_cursor,
        count_
        ) != 0;
    }

  for(; count_ > 0U; --count_, ++effect, ++mode_cursor)
    {
      mode = *mode_cursor;
      if((mode == NK_EFFECT_DRAW_NONE) ||
         (mode == NK_EFFECT_DRAW_STICKY))
        {
          continue;
        }

      if(mode != NK_EFFECT_DRAW_NORMAL)
        {
          return false;
        }

      /*
       * Match preparation assigns NORMAL only to an active effect and
       * completes all state-changing draw work before composition. Use the
       * frame cached by effect simulation: an active effect always owns a
       * frame, so repeating either the active or frame-null check for every
       * submitted blood effect would only duplicate preparation work.
       */
      frame = effect->frame;
      variant_index = effect->draw_variant_index;
      frame_index = variant_index >> 1;
      if((sink_->draw_effect_composite !=
          (NkCompositorEffectCompositeDraw)0) &&
         (frame_index >= NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_FIRST) &&
         (frame_index < NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_FIRST +
          NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_COUNT))
        {
          composite_index = variant_index -
                            (NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_FIRST * 2U);
          composite = &nk_compositor_effect_composite_layouts[
            composite_index
                      ];
          x = effect->x + composite->x;
          y = effect->y - 80 + composite->y;
          if((x >= NK_COMPOSITOR_WIDTH) ||
             (x <= -(s32)composite->width) ||
             (y >= NK_COMPOSITOR_HEIGHT) ||
             (y <= -(s32)composite->height))
            {
              continue;
            }

          if(!sink_->draw_effect_composite(
               sink_->context,
               composite_index,
               x,
               y))
            {
              return false;
            }

          continue;
        }

      if((frame->image_count == 1U) &&
         (sink_->draw_effect_frame !=
          (NkCompositorEffectFrameDraw)0))
        {
          if(!sink_->draw_effect_frame(
               sink_->context,
               variant_index,
               effect->x,
               effect->y))
            {
              return false;
            }

          continue;
        }

      for(image_index = 0U;
          image_index < frame->image_count;
          ++image_index)
        {
          /*
           * Animation data is validated once at startup. The effect bank
           * can therefore use its already-validated table offsets directly
           * instead of repeating generic bank, frame, and image-range
           * lookups for every blood image.
           */
          image = &nk_anim_image_refs[
            frame->image_first + image_index
                  ];
          orientation = (u8)(
            image->orientation ^
            ((variant_index & 1U) << 1)
            );
          /*
           * Effect origins are bounded gameplay coordinates and these
           * offsets are signed bytes, so the specialized path cannot
           * overflow s32.
           */
          if((variant_index & 1U) != 0U)
            {
              x = effect->x + image->x_flipped;
            }
          else
            {
              x = effect->x + image->x_normal;
            }

          y = effect->y - 80 + image->y;
          size = &nk_anim_image_sizes[
            bank->image_size_first + image->image
                 ];
          if((x >= NK_COMPOSITOR_WIDTH) ||
             (x <= -(s32)size->width) ||
             (y >= NK_COMPOSITOR_HEIGHT) ||
             (y <= -(s32)size->height))
            {
              continue;
            }

          if(!_nk_compositor_emit_effect_draw(
               sink_,
               (u32)image->image,
               x,
               y,
               orientation))
            {
              return false;
            }
        }
    }

  return true;
}


static
bool
_nk_compositor_draw_match_series(const NkMatchView      *view_,
                                 const NkCompositorSink *sink_,
                                 u8                      series_index_)
{
  return _nk_compositor_draw_scene_series(
    view_->scene,
    sink_,
    series_index_,
    -160,
    0
    );
}


static
bool
_nk_compositor_draw_match_shock(const NkMatchView      *view_,
                                const NkCompositorSink *sink_,
                                u8                      series_index_)
{
  const NkSceneSeries *series;
  const NkSceneFrame *frame;

  if(view_->shock_phase >= NK_MATCH_SHOCK_VISIBLE_PRESENTATIONS)
    {
      return true;
    }

  series = nk_scene_series_def(
    view_->scene->bank_index,
    series_index_
    );
  if((series == NULL) || (series->frame_count == 0U))
    {
      return true;
    }

  /*
   * NOGGINBG shock series 6 and 7 alternate one repeated image with a
   * source 0xff blank.  Select the visible source frame explicitly so the
   * 50 ms source phase maps to exactly three successful NTSC presentations.
   */
  frame = &nk_scene_frames[series->first_frame];
  if(frame->image == NK_ANIM_NO_IMAGE)
    {
      return true;
    }

  return nk_compositor_emit_draw(
    sink_,
    _nk_compositor_scene_image(
      (u8)NK_COMPOSITOR_IMAGE_SCENE,
      view_->scene->bank_index,
      frame->image
      ),
    frame->x - 160,
    frame->y,
    0U
    );
}


static
bool
_nk_compositor_draw_match_background_series(const NkMatchView      *view_,
                                            const NkCompositorSink *sink_)
{
  const NkGame *game;
  s32    person;

  game = view_->game;
  person = view_->dialogue->person;
  if(game->electrocute != 1)
    {
      if((game->players[0].input_stat &
          (NK_DIR_UP | NK_DIR_DOWN)) == 0U)
        {
          if(!_nk_compositor_draw_match_series(view_, sink_, 0U))
            {
              return false;
            }
        }
      else if((game->players[0].input_stat & NK_DIR_DOWN) != 0U)
        {
          if(!_nk_compositor_draw_match_series(view_, sink_, 2U))
            {
              return false;
            }
        }
      else if(!_nk_compositor_draw_match_series(view_, sink_, 4U))
        {
          return false;
        }

      if(person == NK_DIALOGUE_PERSON_ICER)
        {
          if(game->game_over)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 12U))
                {
                  return false;
                }
            }
          else if(game->ball.x < 70)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 14U))
                {
                  return false;
                }
            }
          else if(game->ball.x > 140)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 16U))
                {
                  return false;
                }
            }
          else if(!_nk_compositor_draw_match_series(view_, sink_, 12U))
            {
              return false;
            }
        }
      else if(!game->game_over)
        {
          if(game->ball.x < 70)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 8U))
                {
                  return false;
                }
            }
          else if((game->ball.x > 140) &&
                  (!_nk_compositor_draw_match_series(
                     view_,
                     sink_,
                     10U)))
            {
              return false;
            }
        }
    }
  else
    {
      if((!_nk_compositor_draw_match_shock(view_, sink_, 6U)) ||
         (!_nk_compositor_draw_match_series(view_, sink_, 24U)))
        {
          return false;
        }
    }

  if(game->electrocute != 2)
    {
      if((game->players[1].input_stat &
          (NK_DIR_UP | NK_DIR_DOWN)) == 0U)
        {
          if(!_nk_compositor_draw_match_series(view_, sink_, 1U))
            {
              return false;
            }
        }
      else if((game->players[1].input_stat & NK_DIR_DOWN) != 0U)
        {
          if(!_nk_compositor_draw_match_series(view_, sink_, 3U))
            {
              return false;
            }
        }
      else if(!_nk_compositor_draw_match_series(view_, sink_, 5U))
        {
          return false;
        }

      if(person == NK_DIALOGUE_PERSON_ETHAN)
        {
          if(game->game_over)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 13U))
                {
                  return false;
                }
            }
          else if(game->ball.x > 250)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 17U))
                {
                  return false;
                }
            }
          else if(game->ball.x < 180)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 15U))
                {
                  return false;
                }
            }
          else if(!_nk_compositor_draw_match_series(view_, sink_, 13U))
            {
              return false;
            }
        }
      else if(!game->game_over)
        {
          if(game->ball.x > 250)
            {
              if(!_nk_compositor_draw_match_series(view_, sink_, 11U))
                {
                  return false;
                }
            }
          else if((game->ball.x < 180) &&
                  (!_nk_compositor_draw_match_series(
                     view_,
                     sink_,
                     9U)))
            {
              return false;
            }
        }
    }
  else
    {
      if((!_nk_compositor_draw_match_shock(view_, sink_, 7U)) ||
         (!_nk_compositor_draw_match_series(view_, sink_, 25U)))
        {
          return false;
        }
    }

  if(!game->electrocute)
    {
      if(person == NK_DIALOGUE_PERSON_STUMP)
        {
          if(game->ball.x > 190)
            {
              return _nk_compositor_draw_match_series(
                view_,
                sink_,
                23U
                );
            }

          if(game->ball.x < 120)
            {
              return _nk_compositor_draw_match_series(
                view_,
                sink_,
                22U
                );
            }

          return _nk_compositor_draw_match_series(view_, sink_, 21U);
        }

      if(game->ball.x > 190)
        {
          return _nk_compositor_draw_match_series(view_, sink_, 20U);
        }

      if(game->ball.x < 120)
        {
          return _nk_compositor_draw_match_series(view_, sink_, 19U);
        }

      return _nk_compositor_draw_match_series(view_, sink_, 18U);
    }

  return true;
}


static
bool
_nk_compositor_draw_match_background(const NkMatchView      *view_,
                                     const NkCompositorSink *sink_)
{
  NkCompositorImageRef layer;
  s32    giger;
  s32    scroll;

  scroll = view_->scene_scroll;
  while(scroll > NK_COMPOSITOR_WIDTH)
    {
      scroll -= NK_COMPOSITOR_WIDTH;
    }
  while(scroll < 0)
    {
      scroll += NK_COMPOSITOR_WIDTH;
    }

  layer = _nk_compositor_scene_layer(
    NK_SCENE_BANK_GAME,
    NK_COMPOSITOR_LAYER_3
    );
  if((!nk_compositor_emit_draw(sink_, layer, -scroll, 0, 0U)) ||
     (!nk_compositor_emit_draw(
        sink_,
        layer,
        NK_COMPOSITOR_WIDTH - scroll,
        0,
        0U)))
    {
      return false;
    }

  giger = (s32)(view_->game->tick & 63U);
  if((view_->game->tick & 64U) != 0U)
    {
      giger ^= 63;
    }

  giger /= 8;
  layer = _nk_compositor_scene_layer(
    NK_SCENE_BANK_GAME,
    NK_COMPOSITOR_LAYER_2
    );
  if(!nk_compositor_emit_draw(sink_, layer, 0, -giger, 0U))
    {
      return false;
    }

  layer = _nk_compositor_scene_layer(
    NK_SCENE_BANK_GAME,
    NK_COMPOSITOR_LAYER_1_COMPOSITE
    );
  if(!nk_compositor_emit_draw(sink_, layer, -160, 0, 0U))
    {
      return false;
    }

  if(!nk_compositor_emit_sticky_surface(sink_))
    {
      return false;
    }

  return _nk_compositor_draw_match_background_series(view_, sink_);
}


static
bool
_nk_compositor_match_space(unsigned char character_)
{
  return character_ == ' ' ||
         character_ == '\t' ||
         character_ == '\r' ||
         character_ == '\n' ||
         character_ == '\f' ||
         character_ == '\v';
}


/*
 * Preserve NOGGIN.CPP's parsing literally, including its unusual centering
 * width.  When a line ends in the middle of a word, the source subtracts the
 * first unconsumed character before walking backward over the copied suffix.
 */
static
bool
_nk_compositor_match_parse_line(const char **text_,
                                s32          maximum_width_,
                                char         line_[50],
                                s32         *line_width_)
{
  const char *source;
  char *destination;
  s32    width;

  source = *text_;
  destination = line_;
  width = 0;
  while(_nk_compositor_match_space((unsigned char)*source))
    {
      source++;
    }
  while((width < maximum_width_) && (*source != '\0'))
    {
      if(!((_nk_compositor_match_space((unsigned char)*source)) &&
           (_nk_compositor_match_space(
             (unsigned char)*(source - 1)))))
        {
          if(destination >= line_ + 49)
            {
              return false;
            }

          *destination = *source;
          width += nk_font_letter_width(
            NK_FONT_SMALL,
            (unsigned char)*source
            );
          destination++;
        }

      source++;
    }

  if(*source != '\0')
    {
      while((width > 0) &&
            (!_nk_compositor_match_space(
               (unsigned char)*source)))
        {
          width -= nk_font_letter_width(
            NK_FONT_SMALL,
            (unsigned char)*source
            );
          *destination = '\0';
          if((destination == line_) || (source == *text_))
            {
              return false;
            }

          destination--;
          source--;
        }
    }

  *destination = '\0';
  *text_ = source;
  *line_width_ = width;
  return true;
}


static
bool
_nk_compositor_draw_match_dialogue(const NkMatchView      *view_,
                                   const NkCompositorSink *sink_)
{
  const char *source;
  char line[50];
  s32    center_x;
  s32    line_width;
  s32    maximum_width;
  s32    y;
  u8    image_kind;
  int line_index;

  source = nk_dialogue_current_text(view_->dialogue);
  if((source == NULL) || (!view_->talking))
    {
      return true;
    }

  if(view_->dialogue->person == NK_DIALOGUE_PERSON_ICER)
    {
      center_x = 100;
      y = 140;
      maximum_width = 180;
      image_kind = (u8)NK_COMPOSITOR_IMAGE_FONT_SMALL_ICER;
    }
  else if(view_->dialogue->person == NK_DIALOGUE_PERSON_ETHAN)
    {
      center_x = 220;
      y = 140;
      maximum_width = 180;
      image_kind = (u8)NK_COMPOSITOR_IMAGE_FONT_SMALL_WHITE;
    }
  else
    {
      center_x = 160;
      y = 35;
      maximum_width = 220;
      image_kind = (u8)NK_COMPOSITOR_IMAGE_FONT_SMALL_STUMP;
    }

  line_index = 0;
  while((*source != '\0') && (line_index < 9))
    {
      if(!_nk_compositor_match_parse_line(
           &source,
           maximum_width,
           line,
           &line_width
           ))
        {
          return false;
        }

      if(!_nk_compositor_draw_font_text_kind(
           sink_,
           image_kind,
           NK_FONT_SMALL,
           center_x - line_width / 2,
           y + line_index * 9,
           line,
           true,
           false))
        {
          return false;
        }

      line_index++;
    }

  return true;
}


static
bool
_nk_compositor_draw_match_outcome(const NkMatchView      *view_,
                                  const NkCompositorSink *sink_)
{
  static const char *names[NK_CHARACTER_COUNT] =
  {
    "KLUBBOR", "FETUS", "HENRY", "GURDIP",
    "ED", "SINAMMON", "BUDDY", "GONZOLES"
  };
  static const char wins[] = " WINS!";
  static const char prompt[] = "PRESS BUTTON TO CONTINUE";
  char text[24];
  const char *name;
  s32    character;
  s32    width;
  int index;
  int name_index;

  if((view_->game->game_state != NK_GAME_STATE_MATCH_COMPLETE) ||
     (view_->game->winner < 0) ||
     (view_->game->winner >= NK_GAME_PLAYER_COUNT))
    {
      return true;
    }

  character = view_->game->players[
    view_->game->winner
              ].character_type;
  if((character < 0) || (character >= NK_CHARACTER_COUNT))
    {
      return false;
    }

  name = names[character];
  index = 0;
  name_index = 0;
  while(name[name_index] != '\0')
    {
      text[index++] = name[name_index++];
    }

  name_index = 0;
  while(wins[name_index] != '\0')
    {
      text[index++] = wins[name_index++];
    }

  text[index] = '\0';
  width = _nk_compositor_font_width(
    NK_FONT_RED,
    text,
    false,
    true
    );
  if(!_nk_compositor_draw_font_text(
       sink_,
       NK_FONT_RED,
       160 - width / 2,
       34,
       text,
       false,
       true))
    {
      return false;
    }

  if((view_->game->tick & 32U) == 0U)
    {
      return true;
    }

  width = _nk_compositor_font_width(
    NK_FONT_SMALL,
    prompt,
    true,
    false
    );
  return _nk_compositor_draw_font_text_kind(
    sink_,
    (u8)NK_COMPOSITOR_IMAGE_FONT_SMALL_WHITE,
    NK_FONT_SMALL,
    160 - width / 2,
    56,
    prompt,
    true,
    false
    );
}


static
void
_nk_compositor_score_text(s32    score_,
                          char   text_[3])
{
  s32    tens;

  tens = 0;
  while(score_ >= 10)
    {
      score_ -= 10;
      tens++;
    }

  text_[0] = (char)('0' + tens);
  text_[1] = (char)('0' + score_);
  text_[2] = '\0';
}


static
bool
_nk_compositor_emit_nonempty_fill(const NkCompositorSink *sink_,
                                  s32                     x_,
                                  s32                     y_,
                                  s32                     width_,
                                  s32                     height_,
                                  u8                      color_)
{
  if((width_ <= 0) || (height_ <= 0))
    {
      return true;
    }

  return nk_compositor_emit_fill(
    sink_,
    x_,
    y_,
    width_,
    height_,
    color_
    );
}


static
bool
_nk_compositor_draw_match_status(const NkMatchView      *view_,
                                 const NkCompositorSink *sink_)
{
  char text[3];
  s32    length_zero;
  s32    length_one;
  s32    width;

  if((view_->game->score[0] < 0) ||
     (view_->game->score[0] > 99) ||
     (view_->game->score[1] < 0) ||
     (view_->game->score[1] > 99))
    {
      return false;
    }

  _nk_compositor_score_text(view_->game->score[0], text);
  if(!_nk_compositor_draw_font_text(
       sink_,
       NK_FONT_RED,
       37,
       12,
       text,
       true,
       false))
    {
      return false;
    }

  _nk_compositor_score_text(view_->game->score[1], text);
  width = _nk_compositor_font_width(
    NK_FONT_RED,
    text,
    false,
    false
    );
  if(!_nk_compositor_draw_font_text(
       sink_,
       NK_FONT_RED,
       320 - 37 - width,
       12,
       text,
       true,
       false))
    {
      return false;
    }

  length_zero = view_->game->bar_size[0] / 2;
  length_one = view_->game->bar_size[1] / 2;
  if(length_zero < 0)
    {
      length_zero = 0;
    }

  if(length_zero > 64)
    {
      length_zero = 64;
    }

  if(length_one < 0)
    {
      length_one = 0;
    }

  if(length_one > 64)
    {
      length_one = 64;
    }

  return _nk_compositor_emit_nonempty_fill(
    sink_,
    25,
    7,
    64 - length_zero,
    3,
    0U) &&
         _nk_compositor_emit_nonempty_fill(
    sink_,
    89 - length_zero,
    7,
    length_zero,
    3,
    2U) &&
         _nk_compositor_emit_nonempty_fill(
    sink_,
    231,
    7,
    length_one,
    3,
    2U) &&
         _nk_compositor_emit_nonempty_fill(
    sink_,
    231 + length_one,
    7,
    64 - length_one,
    3,
    0U);
}


static
bool
_nk_compositor_draw_match_actors(const NkMatchView      *view_,
                                 const NkCompositorSink *sink_)
{
  const NkGame *game;
  s32    player_x[NK_GAME_PLAYER_COUNT];
  s32    player_y[NK_GAME_PLAYER_COUNT];
  s32    ball_x;
  s32    ball_y;
  s32    chopper_x;
  s32    chopper_y;
  u32    index;

  game = view_->game;
  if(game->ready == 0)
    {
      return true;
    }

  for(index = 0U; index < NK_GAME_PLAYER_COUNT; ++index)
    {
      nk_match_present_player_position(
        view_->presentation,
        &game->players[index],
        &player_x[index],
        &player_y[index]
        );
    }

  nk_match_present_ball_position(
    view_->presentation,
    &game->ball,
    &ball_x,
    &ball_y
    );
  nk_match_present_chopper_position(
    view_->presentation,
    &game->chopper,
    &chopper_x,
    &chopper_y
    );
  if((game->chopper.draw_me != 0U) &&
     (!_nk_compositor_draw_match_player(
        sink_,
        &game->chopper,
        chopper_x,
        chopper_y)))
    {
      return false;
    }

  if((game->ball.freeze == 0) &&
     (game->ball.draw_me != 0U) &&
     (!_nk_compositor_draw_match_player(
        sink_,
        &game->ball,
        ball_x,
        ball_y)))
    {
      return false;
    }

  if(game->draw_priority != 0)
    {
      if((!_nk_compositor_draw_match_player(
            sink_,
            &game->players[0],
            player_x[0],
            player_y[0])) ||
         (!_nk_compositor_draw_match_player(
            sink_,
            &game->players[1],
            player_x[1],
            player_y[1])))
        {
          return false;
        }
    }
  else
    {
      if((!_nk_compositor_draw_match_player(
            sink_,
            &game->players[1],
            player_x[1],
            player_y[1])) ||
         (!_nk_compositor_draw_match_player(
            sink_,
            &game->players[0],
            player_x[0],
            player_y[0])))
        {
          return false;
        }
    }

  if((game->ball.freeze != 0) &&
     (game->ball.draw_me != 0U) &&
     (!_nk_compositor_draw_match_player(
        sink_,
        &game->ball,
        ball_x,
        ball_y)))
    {
      return false;
    }

  return _nk_compositor_draw_match_projectiles(
           view_,
           sink_,
           &game->players[0]) &&
         _nk_compositor_draw_match_projectiles(
           view_,
           sink_,
           &game->players[1]);
}


bool
nk_compositor_compose_match(const NkMatchView      *view_,
                            const NkCompositorSink *sink_)
{
  NkCompositorPaletteRef palette;

  if((view_ == NULL) ||
     (view_->game == NULL) ||
     (view_->presentation == NULL) ||
     (view_->scene == NULL) ||
     (view_->dialogue == NULL) ||
     (!view_->scene->valid) ||
     (view_->scene->bank_index != NK_SCENE_BANK_GAME) ||
     (view_->fade_level > NK_COMPOSITOR_FADE_MAX) ||
     (view_->presentation->prepared == 0U) ||
     (view_->presentation->prepared_tick != view_->game->tick) ||
     (view_->presentation->sticky_pending.count >
      NK_STICKY_PENDING_LIMIT))
    {
      return false;
    }

  palette.kind = (u8)NK_COMPOSITOR_PALETTE_SCENE;
  palette.bank_index = NK_SCENE_BANK_GAME;
  palette.index = 0U;
  if((!nk_compositor_emit_begin(
        sink_,
        palette,
        view_->fade_level)) ||
     (!nk_compositor_emit_clear(sink_, 0U)))
    {
      return false;
    }

  if((!view_->background_hidden) &&
     (!_nk_compositor_draw_match_background(view_, sink_)))
    {
      return false;
    }

  if((!_nk_compositor_draw_match_effects(
        view_,
        sink_,
        0U,
        NK_EFFECT_POOL_HALF)) ||
     (!_nk_compositor_draw_match_actors(view_, sink_)))
    {
      return false;
    }

  if((!view_->background_hidden) && (!view_->quit) &&
     (!_nk_compositor_draw_match_dialogue(view_, sink_)))
    {
      return false;
    }

  if((view_->paused) &&
     (!_nk_compositor_draw_font_text(
        sink_,
        NK_FONT_RED,
        85,
        90,
        "GAME PAUSED",
        false,
        true)))
    {
      return false;
    }

  if((!_nk_compositor_draw_match_outcome(view_, sink_)) ||
     (!_nk_compositor_draw_match_effects(
        view_,
        sink_,
        NK_EFFECT_POOL_HALF,
        NK_EFFECT_POOL_HALF)) ||
     (!_nk_compositor_draw_match_status(view_, sink_)))
    {
      return false;
    }

  return nk_compositor_emit_end(sink_);
}


static
NkCompositorImageRef
_nk_compositor_cinema_image(u8     bank_index_,
                            u32    image_index_)
{
  NkCompositorImageRef reference;

  reference.kind = (u8)NK_COMPOSITOR_IMAGE_CINEMA;
  reference.bank_index = bank_index_;
  reference.index = (u16)image_index_;
  return reference;
}


static
NkCompositorImageRef
_nk_compositor_cinema_character(unsigned char character_)
{
  NkCompositorImageRef reference;

  reference.kind = (u8)NK_COMPOSITOR_IMAGE_FONT_SMALL_WHITE;
  reference.bank_index = NK_FONT_SMALL;
  reference.index = character_;
  return reference;
}


static
s32
_nk_compositor_cinema_line_width(const char *first_,
                                 const char *last_)
{
  s32    width;
  s32    advance;
  unsigned char character;

  width = 0;
  while(first_ < last_)
    {
      character = (unsigned char)*first_;
      if(nk_font_image_index(NK_FONT_SMALL, character) < 0)
        {
          advance = 6;
        }
      else
        {
          advance = nk_font_letter_width(NK_FONT_SMALL, character);
        }

      width += advance;
      first_++;
    }

  return width;
}


static
bool
_nk_compositor_draw_cinema_text(const NkCompositorSink *sink_,
                                const char             *text_)
{
  const char *line;
  const char *end;
  s32    x;
  s32    y;
  s32    advance;
  unsigned char character;

  line = text_;
  y = 161;
  for(;;)
    {
      end = line;
      while((*end != '\0') && (*end != '/'))
        {
          end++;
        }

      x = 160 - _nk_compositor_cinema_line_width(line, end) / 2;
      while(line < end)
        {
          character = (unsigned char)*line;
          if(nk_font_image_index(NK_FONT_SMALL, character) >= 0)
            {
              if(!nk_compositor_emit_draw(
                   sink_,
                   _nk_compositor_cinema_character(character),
                   x,
                   y,
                   0U))
                {
                  return false;
                }

              advance = nk_font_letter_width(
                NK_FONT_SMALL,
                character
                );
            }
          else
            {
              advance = 6;
            }

          x += advance;
          line++;
        }

      if(*end == '\0')
        {
          break;
        }

      line = end + 1;
      y += 12;
    }

  return true;
}


bool
nk_compositor_compose_cinema(const NkCinemaView     *view_,
                             const NkCompositorSink *sink_)
{
  const NkCinemaState *cinema;
  const NkCinemaBank *bank;
  const NkCinemaActiveSprite *active;
  const NkCinemaFrame *frame;
  const NkCinemaImageRef *image;
  const char *text;
  NkCompositorPaletteRef palette;
  int active_index;
  int image_index;

  if((view_ == NULL) ||
     (view_->cinema == NULL) ||
     (!view_->cinema->valid))
    {
      return false;
    }

  cinema = view_->cinema;
  bank = nk_cinema_bank(cinema->bank_index);
  if(bank == NULL)
    {
      return false;
    }

  if((cinema->current_background < 0) &&
     (cinema->fade_level != 0U))
    {
      return false;
    }

  if((cinema->current_background >= 0) &&
     (cinema->current_background >= (s32)bank->image_count))
    {
      return false;
    }

  palette.kind = (u8)NK_COMPOSITOR_PALETTE_CINEMA;
  palette.bank_index = cinema->bank_index;
  if(cinema->current_background < 0)
    {
      palette.index = NK_CINEMA_NO_INDEX;
    }
  else
    {
      palette.index = (u16)cinema->current_background;
    }

  if((!nk_compositor_emit_begin(
        sink_,
        palette,
        cinema->fade_level)) ||
     (!nk_compositor_emit_clear(sink_, 0U)))
    {
      return false;
    }

  if(cinema->current_background < 0)
    {
      return nk_compositor_emit_end(sink_);
    }

  if(!nk_compositor_emit_draw(
       sink_,
       _nk_compositor_cinema_image(
         cinema->bank_index,
         (u32)cinema->current_background
         ),
       0,
       40,
       0U))
    {
      return false;
    }

  for(active_index = 0;
      active_index < NK_CINEMA_ACTIVE_SPRITE_COUNT;
      ++active_index)
    {
      active = &cinema->sprites[active_index];
      frame = nk_cinema_active_frame(
        cinema,
        (u8)active_index
        );
      if(frame == NULL)
        {
          continue;
        }

      for(image_index = 0;
          image_index < frame->image_count;
          ++image_index)
        {
          image = nk_cinema_frame_image(
            frame,
            (u8)image_index
            );
          if((image == NULL) ||
             (image->image >= bank->image_count) ||
             (!nk_compositor_emit_draw(
                sink_,
                _nk_compositor_cinema_image(
                  cinema->bank_index,
                  image->image
                  ),
                active->x + image->x,
                active->y + image->y + 40,
                image->orientation)))
            {
              return false;
            }
        }
    }

  /*
   * The DOS player draws sprites into a 320x120 scratch image and copies
   * only that band to rows 40 through 159.  Post-masking reproduces that
   * vertical clip without cropping packed CEL source data.
   */
  if((!nk_compositor_emit_fill(
        sink_, 0, 0, NK_COMPOSITOR_WIDTH, 40, 0U)) ||
     (!nk_compositor_emit_fill(
        sink_, 0, 160, NK_COMPOSITOR_WIDTH, 40, 0U)))
    {
      return false;
    }

  text = nk_cinema_current_text(cinema);
  if((text != NULL) &&
     (!_nk_compositor_draw_cinema_text(sink_, text)))
    {
      return false;
    }

  return nk_compositor_emit_end(sink_);
}


bool
nk_compositor_compose_port_credit(const NkPortCreditView *view_,
                                  const NkCompositorSink *sink_)
{
  NkCompositorImageRef image;
  NkCompositorPaletteRef palette;

  if((view_ == NULL) || (view_->credit == NULL))
    {
      return false;
    }

  palette.kind = (u8)NK_COMPOSITOR_PALETTE_PORT_CREDIT;
  palette.bank_index = 0U;
  palette.index = 0U;
  if((!nk_compositor_emit_begin(
        sink_,
        palette,
        view_->credit->fade_level)) ||
     (!nk_compositor_emit_clear(sink_, 0U)))
    {
      return false;
    }

  if(view_->credit->fade_level == 0U)
    {
      return nk_compositor_emit_end(sink_);
    }

  image.kind = (u8)NK_COMPOSITOR_IMAGE_PORT_CREDIT;
  image.bank_index = 0U;
  image.index = 0U;
  if((!nk_compositor_emit_draw(
        sink_,
        image,
        NK_PORT_CREDIT_IMAGE_X,
        NK_PORT_CREDIT_IMAGE_Y,
        0U)) ||
     (!_nk_compositor_draw_font_text(
        sink_,
        NK_FONT_SMALL,
        NK_PORT_CREDIT_TEXT_X,
        NK_PORT_CREDIT_TEXT_Y,
        "PORTED BY TRAPEXIT",
        true,
        true)) ||
     (!_nk_compositor_draw_font_text(
        sink_,
        NK_FONT_SMALL,
        NK_PORT_CREDIT_TEXT_X,
        NK_PORT_CREDIT_TEXT_Y + NK_PORT_CREDIT_LINE_SPACING,
        "THANKS FOR PLAYING!",
        true,
        true)))
    {
      return false;
    }

  return nk_compositor_emit_end(sink_);
}
