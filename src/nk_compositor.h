#pragma once

#include "nk_anim.h"
#include "nk_cinema.h"
#include "nk_dialogue.h"
#include "nk_match_present.h"
#include "nk_port_credit.h"
#include "nk_scene.h"
#include "nk_select.h"
#include "nk_ui.h"

#define NK_COMPOSITOR_WIDTH (320)
#define NK_COMPOSITOR_HEIGHT (200)
#define NK_COMPOSITOR_FADE_MAX (32U)

#define NK_MATCH_SHOCK_VISIBLE_PRESENTATIONS (3U)
#define NK_MATCH_SHOCK_PERIOD_PRESENTATIONS (6U)

#define NK_COMPOSITOR_LAYER_1A (0U)
#define NK_COMPOSITOR_LAYER_1B (1U)
#define NK_COMPOSITOR_LAYER_2 (2U)
#define NK_COMPOSITOR_LAYER_3 (3U)
#define NK_COMPOSITOR_LAYER_1_COMPOSITE (4U)

#define NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_FIRST (61U)
#define NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_COUNT (6U)

typedef struct NkCompositorEffectCompositeLayout
{
  s8    x;
  s8    y;
  u8    width;
  u8    height;
} NkCompositorEffectCompositeLayout;

extern const NkCompositorEffectCompositeLayout nk_compositor_effect_composite_layouts[
  NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_COUNT * 2U
];

typedef enum NkCompositorImageKind
{
  NK_COMPOSITOR_IMAGE_SCENE = 0,
  NK_COMPOSITOR_IMAGE_SCENE_GRAY = 1,
  NK_COMPOSITOR_IMAGE_SCENE_LAYER = 2,
  NK_COMPOSITOR_IMAGE_SELECTOR_MARK = 3,
  NK_COMPOSITOR_IMAGE_CINEMA = 4,
  NK_COMPOSITOR_IMAGE_FONT_SMALL_WHITE = 5,
  NK_COMPOSITOR_IMAGE_ANIMATION = 6,
  NK_COMPOSITOR_IMAGE_FONT_BLUE = 7,
  NK_COMPOSITOR_IMAGE_FONT_RED = 8,
  NK_COMPOSITOR_IMAGE_ANIMATION_PAIN = 9,
  NK_COMPOSITOR_IMAGE_FONT_SMALL_ICER = 10,
  NK_COMPOSITOR_IMAGE_FONT_SMALL_STUMP = 11,
  NK_COMPOSITOR_IMAGE_PORT_CREDIT = 12
} NkCompositorImageKind;

typedef struct NkCompositorImageRef
{
  u8    kind;
  u8    bank_index;
  u16    index;
} NkCompositorImageRef;

typedef enum NkCompositorPaletteKind
{
  NK_COMPOSITOR_PALETTE_SCENE = 0,
  NK_COMPOSITOR_PALETTE_CINEMA = 1,
  NK_COMPOSITOR_PALETTE_PORT_CREDIT = 2
} NkCompositorPaletteKind;

typedef struct NkCompositorPaletteRef
{
  u8    kind;
  u8    bank_index;
  u16    index;
} NkCompositorPaletteRef;

typedef enum NkCompositorCommandType
{
  NK_COMPOSITOR_BEGIN = 0,
  NK_COMPOSITOR_CLEAR = 1,
  NK_COMPOSITOR_CLIP = 2,
  NK_COMPOSITOR_DRAW = 3,
  NK_COMPOSITOR_FILL = 4,
  NK_COMPOSITOR_END = 5,
  NK_COMPOSITOR_STICKY_SURFACE = 6
} NkCompositorCommandType;

typedef struct NkCompositorBeginCommand
{
  NkCompositorPaletteRef palette;
  u8    fade_level;
} NkCompositorBeginCommand;

typedef struct NkCompositorClearCommand
{
  u8    color;
} NkCompositorClearCommand;

typedef struct NkCompositorClipCommand
{
  s32    x;
  s32    y;
  s32    width;
  s32    height;
} NkCompositorClipCommand;

typedef struct NkCompositorDrawCommand
{
  NkCompositorImageRef image;
  s32    x;
  s32    y;
  u8    orientation;
} NkCompositorDrawCommand;

typedef struct NkCompositorFillCommand
{
  s32    x;
  s32    y;
  s32    width;
  s32    height;
  u8    color;
} NkCompositorFillCommand;

typedef struct NkCompositorCommand
{
  u8    type;
  union
  {
    NkCompositorBeginCommand begin;
    NkCompositorClearCommand clear;
    NkCompositorClipCommand clip;
    NkCompositorDrawCommand draw;
    NkCompositorFillCommand fill;
  } value;
} NkCompositorCommand;

typedef bool (*NkCompositorEmit)(void                      *context_,
                                const NkCompositorCommand *command_);

typedef bool (*NkCompositorEffectDraw)(void  *context_,
                                      u32    image_index_,
                                      s32    x_,
                                      s32    y_,
                                      u8     orientation_);

typedef bool (*NkCompositorEffectFrameDraw)(void  *context_,
                                           u32    variant_index_,
                                           s32    x_,
                                           s32    y_);

typedef bool (*NkCompositorEffectCompositeDraw)(void  *context_,
                                               u32    composite_index_,
                                               s32    x_,
                                               s32    y_);

typedef bool (*NkCompositorEffectRangeDraw)(void           *context_,
                                           const NkEffect *effects_,
                                           const u8       *draw_modes_,
                                           u32             count_);

typedef struct NkCompositorSink
{
  void *context;
  NkCompositorEmit emit;
  NkCompositorEffectDraw draw_effect;
  NkCompositorEffectFrameDraw draw_effect_frame;
  NkCompositorEffectCompositeDraw draw_effect_composite;
  NkCompositorEffectRangeDraw draw_effect_range;
} NkCompositorSink;

typedef struct NkSelectorView
{
  const NkSelectState *selection;
  const NkSceneState *scene;
  s32    scene_scroll;
} NkSelectorView;

typedef struct NkCinemaView
{
  const NkCinemaState *cinema;
} NkCinemaView;

typedef struct NkPortCreditView
{
  const NkPortCreditState *credit;
} NkPortCreditView;

typedef struct NkTitleView
{
  const nk_title_state *title;
  const NkSceneState *scene;
  int voice_playing;
} NkTitleView;

typedef struct NkOptionsView
{
  const nk_options_state *options;
  const NkSceneState *scene;
  const NkAnimCursor *skull;
  s32    scene_scroll;
} NkOptionsView;

typedef struct NkMatchView
{
  const NkGame *game;
  const NkMatchPresentation *presentation;
  const NkSceneState *scene;
  const NkDialogueState *dialogue;
  s32    scene_scroll;
  u8    fade_level;
  u8    talking;
  u8    paused;
  u8    quit;
  u8    background_hidden;
  u8    shock_phase;
} NkMatchView;

bool
nk_compositor_emit_begin(const NkCompositorSink *sink_,
                         NkCompositorPaletteRef  palette_,
                         u8                      fade_level_);
bool
nk_compositor_emit_clear(const NkCompositorSink *sink_,
                         u8                      color_);
bool
nk_compositor_emit_clip(const NkCompositorSink *sink_,
                        s32                     x_,
                        s32                     y_,
                        s32                     width_,
                        s32                     height_);
bool
nk_compositor_emit_draw(const NkCompositorSink *sink_,
                        NkCompositorImageRef    image_,
                        s32                     x_,
                        s32                     y_,
                        u8                      orientation_);
bool
nk_compositor_emit_fill(const NkCompositorSink *sink_,
                        s32                     x_,
                        s32                     y_,
                        s32                     width_,
                        s32                     height_,
                        u8                      color_);
bool
nk_compositor_emit_end(const NkCompositorSink *sink_);
bool
nk_compositor_emit_sticky_surface(const NkCompositorSink *sink_);

/*
 * Emit one complete 320x200 selector frame.  The view and referenced state
 * remain caller-owned, and command emission performs no allocation.
 */
bool
nk_compositor_compose_selector(const NkSelectorView   *view_,
                               const NkCompositorSink *sink_);

/*
 * Emit one complete source-coordinate 320x200 cinema frame.  Cinema content
 * is intentionally not vertically centered by this portable layer.
 */
bool
nk_compositor_compose_cinema(const NkCinemaView     *view_,
                             const NkCompositorSink *sink_);

bool
nk_compositor_compose_port_credit(const NkPortCreditView *view_,
                                  const NkCompositorSink *sink_);

/*
 * Emit one complete source-coordinate 320x200 title frame.  voice_playing is
 * an explicit read-only input so composition never polls or mutates audio.
 */
bool
nk_compositor_compose_title(const NkTitleView      *view_,
                            const NkCompositorSink *sink_);

/*
 * Emit one complete source-coordinate 320x200 options frame.  The cursor,
 * UI state, scene state, and animation cursor remain caller-owned.
 */
bool
nk_compositor_compose_options(const NkOptionsView    *view_,
                              const NkCompositorSink *sink_);

/*
 * Emit one prepared source-coordinate match frame without mutating the game,
 * presentation snapshot, scene, dialogue, or RNG state.
 */
bool
nk_compositor_compose_match(const NkMatchView      *view_,
                            const NkCompositorSink *sink_);
