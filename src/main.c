/*
 * Noggin Knockers 2 port for 3DO.
 */

#include "nk_aspect.h"
#include "nk_assets.h"
#include "nk_audio.h"
#include "nk_cinema.h"
#include "nk_compositor.h"
#include "nk_dialogue.h"
#include "nk_fade.h"
#include "nk_flow.h"
#include "nk_font.h"
#include "nk_game.h"
#include "nk_music.h"
#include "nk_port_credit.h"
#include "nk_scene.h"
#include "nk_select.h"
#include "nk_sticky_target.h"
#include "nk_storage.h"
#include "version.h"

#include "audio.h"
#include "celutils.h"
#include "controlpad.h"
#include "debug.h"
#include "displayutils.h"
#include "event.h"
#include "graphics.h"
#include "hardware.h"
#include "hardware_pixc.h"
#include "io.h"
#include "mem.h"
#include "operror.h"
#include "types.h"

#include "stdio.h"
#include "string.h"

#define NK_SCREEN_WIDTH (320)
#define NK_SCREEN_HEIGHT (240)
#define NK_CEL_LIMIT (256)
#define NK_RENDER_CCB_LIMIT (256)
#define NK_EFFECT_DRAW_FRAME_COUNT (200U)
#define NK_RENDER_Y_LOOKUP_MIN (-256)
#define NK_RENDER_Y_LOOKUP_MAX (511)
#define NK_RENDER_Y_LOOKUP_COUNT                        \
  (NK_RENDER_Y_LOOKUP_MAX - NK_RENDER_Y_LOOKUP_MIN + 1)
#define NK_EFFECT_LOOKUP_HEIGHT_LIMIT (257U)
#define NK_SOLID_FILL_COLOR_COUNT (2U)
#define NK_SELECTOR_MARK_ASSET_COUNT (2U)
#define NK_SELECTOR_OPTION_ASSET_COUNT (3U)
#define NK_PROJECTION_MODE_COUNT (2U)
#define NK_PROJECTION_ORIENTATION_COUNT (4U)
#define NK_PROJECTION_VARIANT_COUNT \
  (NK_PROJECTION_MODE_COUNT * NK_PROJECTION_ORIENTATION_COUNT)
#define NK_PROJECTION_DIRECTION_SHIFT (1U)
#define NK_PROJECTION_MODE_SHIFT (2U)
#define NK_PROJECTION_ORIENTATION_MASK (0x03U)
#define NK_EFFECT_DRAW_DIRECTION_COUNT (2U)
#define NK_EFFECT_DRAW_VARIANT_COUNT \
  (NK_EFFECT_DRAW_FRAME_COUNT * NK_EFFECT_DRAW_DIRECTION_COUNT)

nk_static_assert(NK_FADE_CCB_USEAV == CCB_USEAV,
                 nk_fade_useav_matches_portfolio);
nk_static_assert(NK_FADE_PIXC_OPAQUE == PIXC_OPAQUE,
                 nk_fade_opaque_matches_portfolio);
nk_static_assert(NK_UI_FADE_LEVEL_MAX == NK_FADE_DOS_LEVEL_MAX,
                 nk_ui_fade_matches_renderer);

#define NK_RUNTIME_FONT_BLUE  (0U)
#define NK_RUNTIME_FONT_RED   (1U)
#define NK_RUNTIME_FONT_WHITE (2U)
#define NK_RUNTIME_FONT_GOLD  (3U)
#define NK_RUNTIME_FONT_ICER  (4U)
#define NK_RUNTIME_FONT_STUMP (5U)
#define NK_RUNTIME_FONT_COUNT (6U)

#define NK_PAD_ATTRACT_ACTIVITY                                 \
  (ControlUp | ControlDown | ControlLeft | ControlRight |       \
   ControlA | ControlB | ControlC | ControlStart)
#define NK_PAD_CONTINUOUS                                       \
  (NK_PAD_ATTRACT_ACTIVITY | ControlX | ControlLeftShift)

typedef struct NkCelTemplate
{
  uint32   flags;
  CelData *source;
  void    *plut;
  uint32   pre0;
  uint32   pre1;
  int32    width;
  int32    height;
} NkCelTemplate;

nk_static_assert(sizeof(NkCelTemplate) == 28U,
                 NkCelTemplateSizeCheck);

typedef struct NkProjectionVariant
{
  int32 hdx;
  int32 vdy;
  int32 flip_x_mask;
  int32 flip_y_mask;
} NkProjectionVariant;

typedef struct NkEffectDrawVariant
{
  const NkCelTemplate       *render_template;
  const NkProjectionVariant *projection;
  s32                        x_offset;
  s32                        y_offset;
  s32                        width;
  s32                        height;
  s32                        flip_x_offset;
  s32                        flip_y_offset;
} NkEffectDrawVariant;

nk_static_assert(sizeof(NkEffectDrawVariant) == 32U,
                 NkEffectDrawVariantSizeCheck);

static const NkProjectionVariant
g_PROJECTION_VARIANTS[NK_PROJECTION_VARIANT_COUNT] =
  {
    { 1 << NK_CEL_HDX_SHIFT, NK_ASPECT_CORRECT_UNIT_VDY, 0, 0 },
    { 1 << NK_CEL_HDX_SHIFT, -NK_ASPECT_CORRECT_UNIT_VDY, 0, -1 },
    { -(1 << NK_CEL_HDX_SHIFT), NK_ASPECT_CORRECT_UNIT_VDY, -1, 0 },
    { -(1 << NK_CEL_HDX_SHIFT), -NK_ASPECT_CORRECT_UNIT_VDY, -1, -1 },
    { 1 << NK_CEL_HDX_SHIFT, 1 << NK_CEL_VDY_SHIFT, 0, 0 },
    { 1 << NK_CEL_HDX_SHIFT, -(1 << NK_CEL_VDY_SHIFT), 0, -1 },
    { -(1 << NK_CEL_HDX_SHIFT), 1 << NK_CEL_VDY_SHIFT, -1, 0 },
    { -(1 << NK_CEL_HDX_SHIFT), -(1 << NK_CEL_VDY_SHIFT), -1, -1 }
  };

typedef struct NkCelBank
{
  CCB          **images;
  NkCelTemplate *templates;
  u32            count;
} NkCelBank;

typedef struct NkSingleCel
{
  CCB          *image;
  NkCelTemplate render_template;
} NkSingleCel;

nk_static_assert(sizeof(NkSingleCel) == 32U,
                 NkSingleCelSizeCheck);
nk_static_assert(sizeof(NkEffect) == 68U,
                 NkEffectSizeCheck);

typedef struct NkRuntimeAssets
{
  NkAssetBundle font_bundle;
  NkAssetBundle gameplay_bundle;
  NkAssetBundle select_bundle;
  NkAssetBundle cold_bundles[NK_ASSET_BUNDLE_COUNT];
  u32           cold_bundle_stamps[NK_ASSET_BUNDLE_COUNT];
  u32           cold_cache_clock;
  u8            scene_bundle_index;
  NkCelBank     banks[NK_ANIM_BANK_COUNT];
  NkCelBank     effect_composites;
  NkCelBank     pain_images;
  NkCelBank     scene_images;
  NkCelBank     select_scene_images;
  NkCelBank     game_scene_images;
  NkCelBank     selector_marks;
  NkCelBank     selector_options;
  NkCelBank     selector_gray;
  NkCelBank     cinema_images;
  NkSingleCel   port_credit;
  NkCelBank     fonts[NK_RUNTIME_FONT_COUNT];
  NkSingleCel   scene_layer1a;
  NkSingleCel   scene_layer1b;
  NkSingleCel   scene_layer2;
  NkSingleCel   scene_layer3;
  NkSingleCel   select_layer1a;
  NkSingleCel   select_layer1b;
  NkSingleCel   select_layer2;
  NkSingleCel   select_layer3;
  NkSingleCel   game_layer1_composite;
  NkSingleCel   game_layer2;
  NkSingleCel   game_layer3;
} NkRuntimeAssets;

typedef struct NkRuntime
{
  ScreenContext              display;
  Item                       vbl_request;
  Item                       vram_ioreq;
  NkRuntimeAssets            assets;
  NkAudio                    sound;
  NkMusic                    music;
  nk_flow                    flow;
  NkGame                     game;
  NkSceneState               scene;
  NkCinemaState              cinema;
  NkPortCreditState          port_credit;
  NkDialogueState            dialogue;
  NkSelectState              selection;
  nk_title_state             title;
  nk_options_state           options;
  NkAnimCursor               option_skull;
  nk_rng                     flow_rng;
  nk_input_filter            input_filter[NK_PLAYER_COUNT];
  NkAspectState              aspect;
  const NkEffectDrawVariant *active_effect_draw_variants;
  nk_tick_clock              simulation_clock;
  CCB                       *render_ccbs;
  u32                        render_ccb_count;
  void                      *render_loaded_plut;
  NkCelTemplate              solid_fill_templates[NK_SOLID_FILL_COLOR_COUNT];
  u32                        solid_fill_pixels[NK_SOLID_FILL_COLOR_COUNT];
  s32                        render_correct_y_lookup[NK_RENDER_Y_LOOKUP_COUNT];
  NkMatchPresentation        match_presentation;
  NkStickyTarget             sticky_target;
  NkCelTemplate              sticky_surface_template;
  u32                        last_audio_time;
  u32                        scene_generation;
  u32                        outcome_start_tick;
  s32                        scene_scroll;
  Item                       scene_samples[NK_CINEMA_SOUND_COUNT];
  u32                        scene_sample_cache_mask;
  Item                       select_scream_sample;
  Item                       pause_adjust_sample;
  int                        scene_voice;
  int                        select_scream_voice;
  s8                         match_exit;
  u8                         match_fade_level;
  u8                         match_fade_out;
  u8                         render_fade_level;
  NkFadeCelConfig            render_fade_config;
  s32                        render_compositor_clip_x;
  s32                        render_compositor_clip_y;
  u8                         outcome_started;
  u8                         outcome_continue_pending;
  u8                         cancel_pending;
  u8                         paused;
  u8                         pause_down;
  u8                         pause_volume_stat;
  u8                         display_interpolation_mode;
  u8                         display_interpolation_toggle_down;
  u8                         match_shock_phase;
  u8                         select_match_input_carry;
  u8                         cinematic_input_latched;
  u8                         controller_active;
  u8                         attract_input_latched;
  u8                         fatal_error;
  u8                         control_pad_open;
  u8                         display_open;
  u8                         audio_open;
} NkRuntime;

static NkRuntime g_RUNTIME;
static NkEffectDrawVariant
g_EFFECT_DRAW_VARIANTS[NK_PROJECTION_MODE_COUNT][NK_EFFECT_DRAW_VARIANT_COUNT];

static const char *g_CINEMA_DIRECTORIES[NK_CINEMA_BANK_COUNT] =
  {
    "logo",
    "ncred",
    "end1",
    "end2",
    "end3",
    "end4",
    "end5",
    "end6",
    "end7",
    "end8"
  };


static
uint32
_nk_cel_template_bytes(u32    count_)
{
  uint32 bytes;

  bytes = 0U;
  while(count_ > 0U)
    {
      bytes += 28UL;
      count_--;
    }

  return bytes;
}


static
uint32
_nk_cel_bank_bytes(u32    count_)
{
  return ((sizeof(CCB *) * count_) +
          _nk_cel_template_bytes(count_));
}


static
void
_nk_platform_error(const char *operation_,
                   Err         error_)
{
  kprintf("NK2 platform failed: %s error=%ld\n",
          operation_,
          (long)error_);
  PrintfSysErr(error_);
}


static
void
_nk_link_render_ccb_pool(NkRuntime *state_)
{
  u32    index;

  for(index = 0U; index + 1U < NK_RENDER_CCB_LIMIT; ++index)
    {
      state_->render_ccbs[index].ccb_NextPtr =
        &state_->render_ccbs[index + 1U];
    }

  state_->render_ccbs[NK_RENDER_CCB_LIMIT - 1U].ccb_NextPtr = NULL;
  for(index = 0U; index < NK_RENDER_CCB_LIMIT; ++index)
    {
      state_->render_ccbs[index].ccb_HDY = 0;
      state_->render_ccbs[index].ccb_VDX = 0;
      state_->render_ccbs[index].ccb_HDDX = 0;
      state_->render_ccbs[index].ccb_HDDY = 0;
    }
}


static
void
_nk_initialize_render_y_lookup(NkRuntime *state_)
{
  s32    logical_y;
  u32    index;

  for(index = 0U; index < NK_RENDER_Y_LOOKUP_COUNT; ++index)
    {
      logical_y = NK_RENDER_Y_LOOKUP_MIN + (s32)index;
      state_->render_correct_y_lookup[index] =
        nk_aspect_transform_fixed_y(NK_ASPECT_CORRECT,
                                    logical_y * NK_FIXED_ONE);
    }
}


static
bool
_nk_flush_cels(NkRuntime *state_)
{
  CCB *last;
  Err error;

  if(state_->render_ccb_count == 0U)
    {
      return true;
    }

  if((state_->render_ccbs == NULL) ||
     (state_->render_ccb_count > NK_RENDER_CCB_LIMIT))
    {
      kprintf("NK2 render CCB pool state invalid\n");
      state_->render_ccb_count = 0U;
      state_->fatal_error = 1U;
      return false;
    }

  last = &state_->render_ccbs[state_->render_ccb_count - 1U];
  last->ccb_Flags |= CCB_LAST | CCB_NPABS;
  error = DrawCels(
                   state_->display.sc_BitmapItems[state_->display.sc_curScreen],
                   state_->render_ccbs
                   );
  state_->render_ccb_count = 0U;
  state_->render_loaded_plut = NULL;
  if(error < 0)
    {
      _nk_platform_error("DrawCels(render batch)", error);
      state_->fatal_error = 1U;
      return false;
    }

  return true;
}


static
void
_nk_clear_screen(NkRuntime *state_)
{
  Err error;

  if(!_nk_flush_cels(state_))
    {
      return;
    }

  /*
   * Both display buffers are top-level, page-aligned SPORT destinations.
   * Keep the clipped raw-mode viewport masks on FillRect(); those
   * rectangles are not page operations and still belong on the CEL engine.
   */
  error = SetVRAMPages(
                       state_->vram_ioreq,
                       state_->display.sc_Bitmaps[
                                                  state_->display.sc_curScreen
                                                  ]->bm_Buffer,
                       0,
                       (int32)state_->display.sc_nFrameBufferPages,
                       -1
                       );
  if(error < 0)
    {
      _nk_platform_error("SetVRAMPages(screen clear)", error);
      state_->fatal_error = 1U;
    }
}


static
bool
_nk_prepare_cel_template(const CCB     *cel_,
                         NkCelTemplate *render_template_)
{
  if((cel_ == NULL) || (render_template_ == NULL))
    {
      return false;
    }

  memset(render_template_, 0, sizeof(*render_template_));
  render_template_->source = CEL_DATAPTR(cel_);
  if(render_template_->source == NULL)
    {
      return false;
    }

  if((cel_->ccb_Flags & CCB_PPABS) != 0U)
    {
      render_template_->plut = cel_->ccb_PLUTPtr;
    }
  else if(((cel_->ccb_Flags & CCB_LDPLUT) != 0U) ||
          (cel_->ccb_PLUTPtr != NULL))
    {
      render_template_->plut = CEL_PLUTPTR(cel_);
    }

  if(((cel_->ccb_Flags & CCB_LDPLUT) != 0U) &&
     (render_template_->plut == NULL))
    {
      return false;
    }

  render_template_->flags = cel_->ccb_Flags;
  render_template_->flags &= ~(CCB_SKIP | CCB_LAST);
  render_template_->flags |=
    CCB_NPABS | CCB_SPABS | CCB_PPABS
    | CCB_YOXY | CCB_LDSIZE | CCB_LDPRS
    | CCB_LDPPMP;
  render_template_->pre0 = cel_->ccb_PRE0;
  render_template_->pre1 = cel_->ccb_PRE1;
  render_template_->width = cel_->ccb_Width;
  render_template_->height = cel_->ccb_Height;
  return true;
}


static
bool
_nk_initialize_solid_fill_templates(NkRuntime *state_)
{
  CCB prototype;
  u32    index;

  for(index = 0U; index < NK_SOLID_FILL_COLOR_COUNT; ++index)
    {
      memset(&prototype, 0, sizeof(prototype));
      (void)InitCel(
                    &prototype,
                    1,
                    1,
                    16,
                    INITCEL_UNCODED
                    );
      prototype.ccb_SourcePtr =
        (CelData *)&state_->solid_fill_pixels[index];
      prototype.ccb_Flags |= CCB_BGND;
      prototype.ccb_PRE0 |= PRE0_BGND;
      if(!_nk_prepare_cel_template(
                                   &prototype,
                                   &state_->solid_fill_templates[index]))
        {
          return false;
        }
    }

  return true;
}


static
bool
_nk_initialize_bank(NkCelBank *bank_,
                    u32        count_)
{
  uint32 bank_bytes;

  if((bank_ == NULL) || (count_ > NK_CEL_LIMIT))
    {
      return false;
    }

  memset(bank_, 0, sizeof(*bank_));
  if(count_ == 0U)
    {
      return true;
    }

  bank_bytes = _nk_cel_bank_bytes(count_);
  bank_->images = (CCB **)AllocMem(
                                   bank_bytes,
                                   MEMTYPE_DRAM | MEMTYPE_FILL
                                   );
  if(bank_->images == NULL)
    {
      kprintf("NK2 CEL bank index allocation failed\n");
      return false;
    }

  bank_->templates = (NkCelTemplate *)&bank_->images[count_];
  bank_->count = count_;
  return true;
}


static
void
_nk_delete_bank(NkCelBank *bank_)
{
  if(bank_->images != NULL)
    {
      FreeMem(
              bank_->images,
              (int32)_nk_cel_bank_bytes(bank_->count)
              );
      bank_->images = NULL;
    }

  bank_->templates = NULL;
  bank_->count = 0U;
}


typedef struct NkAssetMapContext
{
  NkRuntimeAssets         *assets;
  const NkAssetBundleSpec *spec;
} NkAssetMapContext;

static
NkCelBank *
_nk_asset_target_bank(NkRuntimeAssets *assets_,
                      u8               target_)
{
  if(target_ <= NK_ASSET_TARGET_FIGHTER_9)
    {
      return &assets_->banks[target_];
    }

  switch(target_)
    {
    case NK_ASSET_TARGET_PAIN:
      return &assets_->pain_images;
    case NK_ASSET_TARGET_EFFECT:
      return &assets_->effect_composites;
    case NK_ASSET_TARGET_SELECTOR_MARKS:
      return &assets_->selector_marks;
    case NK_ASSET_TARGET_SELECTOR_OPTIONS:
      return &assets_->selector_options;
    case NK_ASSET_TARGET_SELECTOR_GRAY:
      return &assets_->selector_gray;
    case NK_ASSET_TARGET_SELECT_SCENE:
      return &assets_->select_scene_images;
    case NK_ASSET_TARGET_GAME_SCENE:
      return &assets_->game_scene_images;
    case NK_ASSET_TARGET_FONT_BLUE:
      return &assets_->fonts[NK_RUNTIME_FONT_BLUE];
    case NK_ASSET_TARGET_FONT_RED:
      return &assets_->fonts[NK_RUNTIME_FONT_RED];
    case NK_ASSET_TARGET_FONT_WHITE:
      return &assets_->fonts[NK_RUNTIME_FONT_WHITE];
    case NK_ASSET_TARGET_FONT_ICER:
      return &assets_->fonts[NK_RUNTIME_FONT_ICER];
    case NK_ASSET_TARGET_FONT_STUMP:
      return &assets_->fonts[NK_RUNTIME_FONT_STUMP];
    case NK_ASSET_TARGET_SCENE:
      return &assets_->scene_images;
    case NK_ASSET_TARGET_CINEMA:
      return &assets_->cinema_images;
    default:
      return NULL;
    }
}


static
NkSingleCel *
_nk_asset_target_cel(NkRuntimeAssets *assets_,
                     u8               target_)
{
  switch(target_)
    {
    case NK_ASSET_TARGET_SELECT_LAYER1A:
      return &assets_->select_layer1a;
    case NK_ASSET_TARGET_SELECT_LAYER1B:
      return &assets_->select_layer1b;
    case NK_ASSET_TARGET_SELECT_LAYER2:
      return &assets_->select_layer2;
    case NK_ASSET_TARGET_SELECT_LAYER3:
      return &assets_->select_layer3;
    case NK_ASSET_TARGET_GAME_LAYER1_COMPOSITE:
      return &assets_->game_layer1_composite;
    case NK_ASSET_TARGET_GAME_LAYER2:
      return &assets_->game_layer2;
    case NK_ASSET_TARGET_GAME_LAYER3:
      return &assets_->game_layer3;
    case NK_ASSET_TARGET_SCENE_LAYER1A:
      return &assets_->scene_layer1a;
    case NK_ASSET_TARGET_SCENE_LAYER1B:
      return &assets_->scene_layer1b;
    case NK_ASSET_TARGET_SCENE_LAYER2:
      return &assets_->scene_layer2;
    case NK_ASSET_TARGET_SCENE_LAYER3:
      return &assets_->scene_layer3;
    case NK_ASSET_TARGET_PORT_CREDIT:
      return &assets_->port_credit;
    default:
      return NULL;
    }
}


static
bool
_nk_prepare_bundle_banks(NkRuntimeAssets         *assets_,
                         const NkAssetBundleSpec *spec_)
{
  NkCelBank *bank;
  NkSingleCel *single;
  u32    counts[NK_ASSET_TARGET_COUNT];
  u32    index;
  u32    target;

  memset(counts, 0, sizeof(counts));
  for(index = 0U; index < spec_->count; ++index)
    {
      target = (u32)NK_ASSET_MAP_TARGET(
                                        spec_->entries[index]
                                        );
      if(target >= NK_ASSET_TARGET_COUNT)
        {
          return false;
        }

      bank = _nk_asset_target_bank(assets_, (u8)target);
      if(bank != NULL)
        {
          if(counts[target]
             <= (u32)NK_ASSET_MAP_INDEX(
                                        spec_->entries[index]))
            {
              counts[target] =
                (u32)NK_ASSET_MAP_INDEX(
                                        spec_->entries[index]
                                        ) + 1U;
            }
        }
      else
        {
          single = _nk_asset_target_cel(assets_, (u8)target);
          if((single == NULL) ||
             (NK_ASSET_MAP_INDEX(
                                 spec_->entries[index]) != 0U) ||
             (single->image != NULL))
            {
              return false;
            }
        }
    }

  for(target = 0U; target < NK_ASSET_TARGET_COUNT; ++target)
    {
      if(counts[target] == 0U)
        {
          continue;
        }

      bank = _nk_asset_target_bank(assets_, (u8)target);
      if((bank == NULL) || (bank->images != NULL) ||
         (bank->templates != NULL) ||
         (bank->count != 0U) ||
         (!_nk_initialize_bank(bank, counts[target])))
        {
          return false;
        }
    }

  return true;
}


static
bool
_nk_map_bundle_cel(void  *context_,
                   u32    index_,
                   CCB   *cel_)
{
  NkAssetMapContext *map_context;
  const NkAssetMapEntry *entry;
  NkCelBank *bank;
  NkSingleCel *single;

  map_context = (NkAssetMapContext *)context_;
  if((map_context == NULL) || (index_ >= map_context->spec->count))
    {
      return false;
    }

  entry = &map_context->spec->entries[index_];
  bank = _nk_asset_target_bank(
                               map_context->assets,
                               NK_ASSET_MAP_TARGET(*entry)
                               );
  if(bank != NULL)
    {
      if(((u32)NK_ASSET_MAP_INDEX(*entry) >= bank->count) ||
         (bank->images[NK_ASSET_MAP_INDEX(*entry)] != NULL) ||
         (!_nk_prepare_cel_template(
                                    cel_,
                                    &bank->templates[NK_ASSET_MAP_INDEX(*entry)])))
        {
          return false;
        }

      bank->images[NK_ASSET_MAP_INDEX(*entry)] = cel_;
      return true;
    }

  single = _nk_asset_target_cel(
                                map_context->assets,
                                NK_ASSET_MAP_TARGET(*entry)
                                );
  if((single == NULL) || (single->image != NULL) ||
     (!_nk_prepare_cel_template(
                                cel_,
                                &single->render_template)))
    {
      return false;
    }

  single->image = cel_;
  return true;
}


static
void
_nk_clear_bundle_targets(NkRuntimeAssets         *assets_,
                         const NkAssetBundleSpec *spec_)
{
  NkCelBank *bank;
  NkSingleCel *single;
  u8    cleared[NK_ASSET_TARGET_COUNT];
  u32    index;
  u8    target;

  memset(cleared, 0, sizeof(cleared));
  for(index = 0U; index < spec_->count; ++index)
    {
      target = NK_ASSET_MAP_TARGET(spec_->entries[index]);
      if((target >= NK_ASSET_TARGET_COUNT) || (cleared[target]))
        {
          continue;
        }

      bank = _nk_asset_target_bank(assets_, target);
      if(bank != NULL)
        {
          _nk_delete_bank(bank);
        }
      else
        {
          single = _nk_asset_target_cel(assets_, target);
          if(single != NULL)
            {
              memset(single, 0, sizeof(*single));
            }
        }

      cleared[target] = 1U;
    }
}


static
bool
_nk_load_asset_bundle(NkRuntimeAssets *assets_,
                      NkAssetBundle   *bundle_,
                      u8               bundle_index_,
                      uint32           memory_type_)
{
  NkAssetMapContext context;
  const NkAssetBundleSpec *spec;

  if(bundle_index_ >= NK_ASSET_BUNDLE_COUNT)
    {
      return false;
    }

  spec = &nk_asset_bundle_specs[bundle_index_];
  if(!_nk_prepare_bundle_banks(assets_, spec))
    {
      _nk_clear_bundle_targets(assets_, spec);
      return false;
    }

  context.assets = assets_;
  context.spec = spec;
  if(!nk_asset_bundle_load(
                           bundle_,
                           spec,
                           memory_type_,
                           _nk_map_bundle_cel,
                           &context))
    {
      _nk_clear_bundle_targets(assets_, spec);
      return false;
    }

  return true;
}


static
void
_nk_unload_asset_bundle(NkRuntimeAssets *assets_,
                        NkAssetBundle   *bundle_,
                        u8               bundle_index_)
{
  const NkAssetBundleSpec *spec;

  if(bundle_index_ >= NK_ASSET_BUNDLE_COUNT)
    {
      return;
    }

  spec = &nk_asset_bundle_specs[bundle_index_];
  _nk_clear_bundle_targets(assets_, spec);
  nk_asset_bundle_unload(bundle_);
}


static
bool
_nk_font_asset_layout_valid(const NkRuntimeAssets *assets_)
{
  return assets_->fonts[NK_RUNTIME_FONT_BLUE].count
    == (u32)nk_font_glyph_count[NK_FONT_BLUE] &&
    assets_->fonts[NK_RUNTIME_FONT_RED].count
    == (u32)nk_font_glyph_count[NK_FONT_RED] &&
    assets_->fonts[NK_RUNTIME_FONT_WHITE].count
    == (u32)nk_font_glyph_count[NK_FONT_SMALL] &&
    assets_->fonts[NK_RUNTIME_FONT_ICER].count
    == (u32)nk_font_glyph_count[NK_FONT_SMALL] &&
    assets_->fonts[NK_RUNTIME_FONT_STUMP].count
    == (u32)nk_font_glyph_count[NK_FONT_SMALL];
}


static
bool
_nk_gameplay_asset_layout_valid(const NkRuntimeAssets *assets_)
{
  const NkAnimBank *bank;
  const NkSceneBank *scene_bank;
  u32    index;

  for(index = 0U; index < NK_ANIM_BANK_COUNT; ++index)
    {
      bank = nk_anim_bank(index);
      if((bank == NULL) || (assets_->banks[index].count
                            != (u32)bank->image_index_limit))
        {
          return false;
        }
    }

  bank = nk_anim_bank(NK_ANIM_PAIN_BANK);
  if((bank == NULL) || (assets_->pain_images.count
                        != (u32)bank->pain_image_index_limit) ||
     (assets_->effect_composites.count != 12U))
    {
      return false;
    }

  scene_bank = nk_scene_bank(NK_SCENE_BANK_GAME);
  return scene_bank != NULL &&
    assets_->game_scene_images.count
    == (u32)scene_bank->image_count &&
    assets_->game_layer1_composite.image != NULL &&
    assets_->game_layer2.image != NULL &&
    assets_->game_layer3.image != NULL;
}


static
bool
_nk_select_asset_layout_valid(const NkRuntimeAssets *assets_)
{
  const NkSceneBank *scene_bank;

  scene_bank = nk_scene_bank(NK_SCENE_BANK_SELECT);
  if((scene_bank == NULL) ||
     (assets_->select_scene_images.count
      != (u32)scene_bank->image_count) ||
     (assets_->selector_gray.count
      != (u32)scene_bank->image_count) ||
     (assets_->selector_marks.count != NK_SELECTOR_MARK_ASSET_COUNT) ||
     (assets_->selector_options.count != NK_SELECTOR_OPTION_ASSET_COUNT))
    {
      return false;
    }

  return assets_->select_layer1a.image != NULL &&
    assets_->select_layer1b.image != NULL &&
    assets_->select_layer2.image != NULL &&
    assets_->select_layer3.image != NULL;
}


static
bool
_nk_load_font_assets(NkRuntimeAssets *assets_)
{
  if(assets_->font_bundle.root != NULL)
    {
      return true;
    }

  if((!_nk_load_asset_bundle(
                             assets_,
                             &assets_->font_bundle,
                             NK_ASSET_BUNDLE_FONTS,
                             MEMTYPE_DRAM)) ||
     (!_nk_font_asset_layout_valid(assets_)))
    {
      _nk_unload_asset_bundle(
                              assets_,
                              &assets_->font_bundle,
                              NK_ASSET_BUNDLE_FONTS
                              );
      return false;
    }

  return true;
}


static
void
_nk_unload_font_assets(NkRuntimeAssets *assets_)
{
  if(assets_->font_bundle.root != NULL)
    {
      _nk_unload_asset_bundle(
                              assets_,
                              &assets_->font_bundle,
                              NK_ASSET_BUNDLE_FONTS
                              );
    }
}


static
bool
_nk_load_gameplay_assets(NkRuntimeAssets *assets_)
{
  if(assets_->gameplay_bundle.root != NULL)
    {
      return true;
    }

  if((!_nk_load_asset_bundle(
                             assets_,
                             &assets_->gameplay_bundle,
                             NK_ASSET_BUNDLE_MAIN_GAME,
                             MEMTYPE_DRAM)) ||
     (!_nk_gameplay_asset_layout_valid(assets_)))
    {
      _nk_unload_asset_bundle(
                              assets_,
                              &assets_->gameplay_bundle,
                              NK_ASSET_BUNDLE_MAIN_GAME
                              );
      return false;
    }

  return true;
}


static
void
_nk_clear_active_scene_assets(NkRuntimeAssets *assets_)
{
  memset(&assets_->scene_images, 0, sizeof(assets_->scene_images));
  memset(&assets_->scene_layer1a, 0, sizeof(assets_->scene_layer1a));
  memset(&assets_->scene_layer1b, 0, sizeof(assets_->scene_layer1b));
  memset(&assets_->scene_layer2, 0, sizeof(assets_->scene_layer2));
  memset(&assets_->scene_layer3, 0, sizeof(assets_->scene_layer3));
}


static
void
_nk_unload_scene_assets(NkRuntimeAssets *assets_)
{
  if(assets_->scene_bundle_index < NK_ASSET_BUNDLE_COUNT)
    {
      _nk_clear_bundle_targets(
                               assets_,
                               &nk_asset_bundle_specs[assets_->scene_bundle_index]
                               );
    }

  assets_->scene_bundle_index = NK_ASSET_BUNDLE_COUNT;
  _nk_clear_active_scene_assets(assets_);
}


static
bool
_nk_is_cold_bundle(u8    bundle_index_)
{
  return bundle_index_ >= NK_ASSET_BUNDLE_TITLE &&
    bundle_index_ < NK_ASSET_BUNDLE_COUNT;
}


static
void
_nk_evict_cold_bundle(NkRuntimeAssets *assets_,
                      u8               bundle_index_)
{
  if(!_nk_is_cold_bundle(bundle_index_))
    {
      return;
    }

  if(assets_->scene_bundle_index == bundle_index_)
    {
      _nk_unload_scene_assets(assets_);
    }

  if(assets_->cold_bundles[bundle_index_].root != NULL)
    {
      nk_asset_bundle_unload(&assets_->cold_bundles[bundle_index_]);
    }

  assets_->cold_bundle_stamps[bundle_index_] = 0U;
}


static
bool
_nk_evict_lru_cold_bundle(NkRuntimeAssets *assets_,
                          u8               excluded_index_)
{
  u32    oldest_stamp;
  u8    bundle_index;
  u8    oldest_index;

  oldest_stamp = NK_U32_MAX;
  oldest_index = NK_ASSET_BUNDLE_COUNT;
  for(bundle_index = NK_ASSET_BUNDLE_TITLE;
      bundle_index < NK_ASSET_BUNDLE_COUNT;
      ++bundle_index)
    {
      if((bundle_index == excluded_index_) ||
         (assets_->cold_bundles[bundle_index].root == NULL))
        {
          continue;
        }

      if(assets_->cold_bundle_stamps[bundle_index] < oldest_stamp)
        {
          oldest_stamp = assets_->cold_bundle_stamps[bundle_index];
          oldest_index = bundle_index;
        }
    }

  if(oldest_index >= NK_ASSET_BUNDLE_COUNT)
    {
      return false;
    }

  _nk_evict_cold_bundle(assets_, oldest_index);
  return true;
}


static
void
_nk_unload_cold_bundles(NkRuntimeAssets *assets_)
{
  u8    bundle_index;

  _nk_unload_scene_assets(assets_);
  for(bundle_index = NK_ASSET_BUNDLE_TITLE;
      bundle_index < NK_ASSET_BUNDLE_COUNT;
      ++bundle_index)
    {
      if(assets_->cold_bundles[bundle_index].root != NULL)
        {
          nk_asset_bundle_unload(&assets_->cold_bundles[bundle_index]);
        }

      assets_->cold_bundle_stamps[bundle_index] = 0U;
    }

  assets_->cold_cache_clock = 0U;
}


static
void
_nk_unload_gameplay_assets(NkRuntimeAssets *assets_)
{
  _nk_clear_active_scene_assets(assets_);
  if(assets_->gameplay_bundle.root != NULL)
    {
      _nk_unload_asset_bundle(
                              assets_,
                              &assets_->gameplay_bundle,
                              NK_ASSET_BUNDLE_MAIN_GAME
                              );
    }
}


static
bool
_nk_load_select_resident_assets(NkRuntimeAssets *assets_)
{
  if(assets_->select_bundle.root != NULL)
    {
      return true;
    }

  if((!_nk_load_asset_bundle(
                             assets_,
                             &assets_->select_bundle,
                             NK_ASSET_BUNDLE_SELECT,
                             MEMTYPE_DRAM)) ||
     (!_nk_select_asset_layout_valid(assets_)))
    {
      _nk_unload_asset_bundle(
                              assets_,
                              &assets_->select_bundle,
                              NK_ASSET_BUNDLE_SELECT
                              );
      return false;
    }

  return true;
}


static
void
_nk_unload_select_resident_assets(NkRuntimeAssets *assets_)
{
  _nk_clear_active_scene_assets(assets_);
  if(assets_->select_bundle.root != NULL)
    {
      _nk_unload_asset_bundle(
                              assets_,
                              &assets_->select_bundle,
                              NK_ASSET_BUNDLE_SELECT
                              );
    }
}


static
bool
_nk_activate_resident_scene(NkRuntimeAssets *assets_,
                            u8               scene_bank_index_)
{
  _nk_clear_active_scene_assets(assets_);
  if(scene_bank_index_ == NK_SCENE_BANK_SELECT)
    {
      memcpy(
             &assets_->scene_images,
             &assets_->select_scene_images,
             sizeof(assets_->scene_images)
             );
      assets_->scene_layer1a = assets_->select_layer1a;
      assets_->scene_layer1b = assets_->select_layer1b;
      assets_->scene_layer2 = assets_->select_layer2;
      assets_->scene_layer3 = assets_->select_layer3;
    }
  else if(scene_bank_index_ == NK_SCENE_BANK_GAME)
    {
      memcpy(
             &assets_->scene_images,
             &assets_->game_scene_images,
             sizeof(assets_->scene_images)
             );
      assets_->scene_layer1a = assets_->game_layer1_composite;
      assets_->scene_layer2 = assets_->game_layer2;
      assets_->scene_layer3 = assets_->game_layer3;
    }
  else
    {
      return false;
    }

  if((assets_->scene_layer1a.image == NULL) ||
     (assets_->scene_layer2.image == NULL) ||
     (assets_->scene_layer3.image == NULL))
    {
      return false;
    }

  return scene_bank_index_ == NK_SCENE_BANK_GAME ||
    assets_->scene_layer1b.image != NULL;
}


static
bool
_nk_load_scene_bundle(NkRuntimeAssets *assets_,
                      u8               bundle_index_)
{
  NkAssetBundle *bundle;
  u32    attempt;

  if((!_nk_is_cold_bundle(bundle_index_)) ||
     (assets_->scene_bundle_index < NK_ASSET_BUNDLE_COUNT))
    {
      return false;
    }

  bundle = &assets_->cold_bundles[bundle_index_];
  for(attempt = 0U;
      attempt <= NK_ASSET_BUNDLE_COUNT;
      ++attempt)
    {
      if(_nk_load_asset_bundle(
                               assets_,
                               bundle,
                               bundle_index_,
                               MEMTYPE_VRAM))
        {
          assets_->scene_bundle_index = bundle_index_;
          assets_->cold_cache_clock++;
          if(assets_->cold_cache_clock == 0U)
            {
              assets_->cold_cache_clock = 1U;
            }

          assets_->cold_bundle_stamps[bundle_index_] =
            assets_->cold_cache_clock;
          return true;
        }

      if(bundle->root != NULL)
        {
          _nk_evict_cold_bundle(assets_, bundle_index_);
        }
      else if(!_nk_evict_lru_cold_bundle(
                                         assets_,
                                         bundle_index_))
        {
          return false;
        }
    }

  return false;
}


static
bool
_nk_initialize_effect_draw_variants(const NkRuntimeAssets *assets_)
{
  const NkAnimBank *metadata;
  const NkAnimFrame *frame;
  const NkAnimImageRef *image;
  const NkAnimImageSize *size;
  const NkCelBank *bank;
  const NkCelTemplate *render_template;
  NkEffectDrawVariant *variant;
  CCB *cel;
  u32    direction;
  u32    frame_index;
  u32    image_index;
  u32    mode;
  u32    projection_index;
  u32    size_index;
  u8    orientation;

  memset(
         g_EFFECT_DRAW_VARIANTS,
         0,
         sizeof(g_EFFECT_DRAW_VARIANTS)
         );
  metadata = &nk_anim_banks[NK_EFFECT_BANK_INDEX];
  bank = &assets_->banks[NK_EFFECT_BANK_INDEX];
  if(metadata->frame_count != NK_EFFECT_DRAW_FRAME_COUNT)
    {
      return false;
    }

  /*
   * Match composition culls every effect image before the target applies
   * its optional height-minus-one flip offset. A height no greater than
   * 1 - NK_RENDER_Y_LOOKUP_MIN therefore keeps the final Y at or above
   * the lookup minimum; this target's 200-line viewport also keeps it
   * below the lookup maximum.
   */
  for(image_index = 0U;
      image_index < (u32)metadata->image_index_limit;
      ++image_index)
    {
      size_index =
        (u32)metadata->image_size_first + image_index;
      if(size_index >= (u32)nk_anim_image_size_count)
        {
          return false;
        }

      size = &nk_anim_image_sizes[size_index];
      if((u32)size->height > NK_EFFECT_LOOKUP_HEIGHT_LIMIT)
        {
          return false;
        }
    }

  for(frame_index = 0U;
      frame_index < (u32)metadata->frame_count;
      ++frame_index)
    {
      frame = &nk_anim_frames[
                              (u32)metadata->frame_first + frame_index
                              ];
      if(frame->image_count != 1U)
        {
          continue;
        }

      image = &nk_anim_image_refs[frame->image_first];
      image_index = (u32)image->image;
      size_index =
        (u32)metadata->image_size_first + image_index;
      if((image_index >= bank->count) ||
         (image_index >= (u32)metadata->image_index_limit) ||
         (size_index >= (u32)nk_anim_image_size_count))
        {
          return false;
        }

      size = &nk_anim_image_sizes[size_index];
      cel = bank->images[image_index];
      render_template = &bank->templates[image_index];
      if((cel == NULL) ||
         (render_template->source == NULL) ||
         (cel->ccb_Width != (int32)size->width) ||
         (cel->ccb_Height != (int32)size->height))
        {
          return false;
        }

      for(direction = 0U; direction < NK_EFFECT_DRAW_DIRECTION_COUNT; ++direction)
        {
          orientation = (u8)(
                             image->orientation ^ (u8)(direction << NK_PROJECTION_DIRECTION_SHIFT)
                             );
          for(mode = 0U; mode < NK_PROJECTION_MODE_COUNT; ++mode)
            {
              variant =
                &g_EFFECT_DRAW_VARIANTS[mode][
                  frame_index * NK_EFFECT_DRAW_DIRECTION_COUNT + direction
                  ];
              variant->render_template = render_template;
              if(direction == 0U)
                {
                  variant->x_offset = (s32)image->x_normal;
                }
              else
                {
                  variant->x_offset = (s32)image->x_flipped;
                }

              variant->y_offset = (s32)image->y - 80;
              variant->width = (s32)size->width;
              variant->height = (s32)size->height;
              projection_index =
                (mode << NK_PROJECTION_MODE_SHIFT) |
                ((u32)orientation & NK_PROJECTION_ORIENTATION_MASK);
              variant->projection =
                &g_PROJECTION_VARIANTS[projection_index];
              variant->flip_x_offset = (s32)(
                                             (variant->width - 1L)
                                             & variant->projection->flip_x_mask
                                             );
              variant->flip_y_offset = (s32)(
                                             (variant->height - 1L)
                                             & variant->projection->flip_y_mask
                                             );
            }
        }
    }

  return true;
}


static
bool
_nk_load_title_assets(NkRuntimeAssets *assets_)
{
  const NkSceneBank *bank;

  bank = nk_scene_bank(NK_SCENE_BANK_TITLE);
  if((bank == NULL) ||
     (!_nk_load_scene_bundle(assets_, NK_ASSET_BUNDLE_TITLE)) ||
     (assets_->scene_images.count != (u32)bank->image_count) ||
     (assets_->scene_layer1a.image == NULL) ||
     (assets_->scene_layer1b.image == NULL) ||
     (assets_->scene_layer2.image == NULL) ||
     (assets_->scene_layer3.image == NULL))
    {
      _nk_unload_scene_assets(assets_);
      return false;
    }

  return true;
}


static
bool
_nk_load_select_assets(NkRuntimeAssets *assets_,
                       int              options_)
{
  (void)options_;
  return _nk_load_select_resident_assets(assets_) &&
    _nk_activate_resident_scene(
                                assets_,
                                NK_SCENE_BANK_SELECT
                                );
}


static
bool
_nk_load_match_assets(NkRuntimeAssets *assets_)
{
  return _nk_activate_resident_scene(
                                     assets_,
                                     NK_SCENE_BANK_GAME
                                     );
}


static
bool
_nk_load_cinema_assets(NkRuntimeAssets *assets_,
                       u8               cinema_index_)
{
  const NkCinemaBank *bank;
  u8    bundle_index;

  if(cinema_index_ >= NK_CINEMA_BANK_COUNT)
    {
      return false;
    }

  bank = nk_cinema_bank(cinema_index_);
  bundle_index = (u8)(
                      NK_ASSET_BUNDLE_CINEMA_FIRST + cinema_index_
                      );
  if((bank == NULL) ||
     (!_nk_load_scene_bundle(assets_, bundle_index)) ||
     (assets_->cinema_images.count
      != (u32)bank->image_count))
    {
      _nk_unload_scene_assets(assets_);
      return false;
    }

  return true;
}


static
bool
_nk_load_port_credit_assets(NkRuntimeAssets *assets_)
{
  return _nk_load_cinema_assets(assets_, NK_CINEMA_CREDITS) &&
    (assets_->port_credit.image != NULL);
}


static
void
_nk_reset_scene_samples(NkRuntime *state_)
{
  int index;

  for(index = 0; index < NK_CINEMA_SOUND_COUNT; ++index)
    {
      state_->scene_samples[index] = -1;
    }

  state_->scene_sample_cache_mask = 0U;
  state_->scene_voice = -1;
  state_->select_scream_sample = -1;
  state_->pause_adjust_sample = -1;
  state_->select_scream_voice = -1;
}


static
void
_nk_unload_select_scream(NkRuntime *state_)
{
  if(state_->select_scream_voice >= 0)
    {
      nk_audio_release_voice(
                             &state_->sound,
                             state_->select_scream_voice
                             );
      state_->select_scream_voice = -1;
    }

  if(state_->select_scream_sample >= 0)
    {
      nk_audio_release_scream_sample(&state_->sound);
      state_->select_scream_sample = -1;
    }
}


static
void
_nk_unload_pause_adjust_sample(NkRuntime *state_)
{
  if(state_->pause_adjust_sample >= 0)
    {
      UnloadSample(state_->pause_adjust_sample);
      state_->pause_adjust_sample = -1;
    }
}


static
bool
_nk_load_pause_adjust_sample(NkRuntime *state_)
{
  char *path;

  path = "nog2/select3/audio/s001.aiff";
  state_->pause_adjust_sample = LoadSample(path);
  if(state_->pause_adjust_sample < 0)
    {
      return false;
    }

  return true;
}


static
bool
_nk_play_select_scream(NkRuntime *state_,
                       s32        character_,
                       s32        scream_index_)
{
  if((character_ < 0) || (character_ >= NK_CHARACTER_COUNT) ||
     (scream_index_ < 0) || (scream_index_ >= NK_AUDIO_SCREAM_VARIANT_COUNT))
    {
      return false;
    }

  _nk_unload_select_scream(state_);
  state_->select_scream_sample = nk_audio_prepare_scream(
                                                         &state_->sound,
                                                         character_,
                                                         scream_index_
                                                         );
  if(state_->select_scream_sample < 0)
    {
      return false;
    }

  state_->select_scream_voice = nk_audio_play_item(
                                                   &state_->sound,
                                                   state_->select_scream_sample
                                                   );
  if(state_->select_scream_voice < 0)
    {
      nk_audio_release_scream_sample(&state_->sound);
      state_->select_scream_sample = -1;
      return false;
    }

  return true;
}


static
void
_nk_unload_scene_samples(NkRuntime *state_)
{
  int index;

  nk_audio_stop_all(&state_->sound);
  for(index = 0; index < NK_CINEMA_SOUND_COUNT; ++index)
    {
      if(state_->scene_samples[index] >= 0)
        {
          if((state_->scene_sample_cache_mask
              & (1UL << index)) == 0U)
            {
              UnloadSample(state_->scene_samples[index]);
            }

          state_->scene_samples[index] = -1;
        }
    }

  state_->scene_sample_cache_mask = 0U;
  state_->scene_voice = -1;
}


static
bool
_nk_load_select_samples(NkRuntime *state_)
{
  if(!nk_audio_load_select(&state_->sound))
    {
      return false;
    }

  state_->scene_samples[NK_MENU_SOUND_ADJUST] = state_->sound.select_samples[0];
  state_->scene_samples[NK_MENU_SOUND_MOVE] = state_->sound.select_samples[1];
  state_->scene_sample_cache_mask = (1U << NK_MENU_SOUND_ADJUST) | (1U << NK_MENU_SOUND_MOVE);
  return true;
}


static
bool
_nk_load_scene_samples(NkRuntime  *state_,
                       const char *directory_,
                       u32         available_mask_)
{
  char path[96];
  int index;

  for(index = 0; index < NK_CINEMA_SOUND_COUNT; ++index)
    {
      if((available_mask_ & (1UL << index)) == 0U)
        {
          continue;
        }

      sprintf(
              path,
              "nog2/%s/audio/s%03d.aiff",
              directory_,
              index
              );
      state_->scene_samples[index] = LoadSample(path);
      if(state_->scene_samples[index] < 0)
        {
          _nk_unload_scene_samples(state_);
          return false;
        }
    }

  return true;
}


static
void
_nk_draw_cel_template(NkRuntime                 *state_,
                      const NkCelTemplate       *render_template_,
                      s32                        fixed_x_,
                      s32                        fixed_y_,
                      const NkProjectionVariant *projection_)
{
  CCB *draw;
  u32    draw_flags;

  /*
   * This is an internal enqueue primitive. Bank templates are validated
   * when their CELs load, and the stack template caller returns immediately
   * if preparation fails. The render pool is allocated before the first
   * scene and the main loop never runs when that allocation fails.
   * Compositor emitters reject an existing fatal state before entering
   * this primitive. Effect callbacks return !fatal_error after every
   * append, so a failed limit flush stops composition before another CEL
   * can arrive here.
   */
  if(!state_->render_fade_config.draw)
    {
      return;
    }

  /*
   * Every successful append flushes immediately on reaching the pool
   * limit, and every flush resets the count before returning.  Therefore
   * the entry count is always below the limit here.
   */
  draw = &state_->render_ccbs[state_->render_ccb_count];
  /*
   * Source-format words and absolute data pointers come from the immutable
   * load-time template. Every changing field is written directly into this
   * reusable absolute-address CCB pool.
   */
  draw_flags =
    (u32)render_template_->flags |
    state_->render_fade_config.required_ccb_flags;
  draw->ccb_Flags = draw_flags;
  draw->ccb_SourcePtr = render_template_->source;
  draw->ccb_PLUTPtr = render_template_->plut;
  draw->ccb_PIXC = state_->render_fade_config.pixc;
  draw->ccb_PRE0 = render_template_->pre0;
  draw->ccb_PRE1 = render_template_->pre1;
  /*
   * ccb_Width and ccb_Height are Portfolio software metadata tacked onto
   * the hardware CCB. Nothing reads those fields from this transient pool;
   * the CEL engine obtains the source dimensions from PRE0/PRE1.
   */
  if((draw_flags & CCB_LDPLUT) != 0U)
    {
      if(draw->ccb_PLUTPtr == state_->render_loaded_plut)
        {
          draw_flags &= ~CCB_LDPLUT;
          draw->ccb_Flags = draw_flags;
        }
      else
        {
          state_->render_loaded_plut = draw->ccb_PLUTPtr;
        }
    }

  draw->ccb_HDX = projection_->hdx;
  draw->ccb_VDY = projection_->vdy;
  /*
   * Callers supply final 16.16 positions after local positioning, flipping,
   * aspect projection, and any active bitmap-clip adjustment. This keeps
   * match effects on their proven full-screen-origin path without
   * duplicating the immutable CCB population below.
   */
  draw->ccb_XPos = fixed_x_;
  draw->ccb_YPos = fixed_y_;
  state_->render_ccb_count++;
  if(state_->render_ccb_count == NK_RENDER_CCB_LIMIT)
    {
      (void)_nk_flush_cels(state_);
    }
}


static
void
_nk_draw_prepared_cel(NkRuntime           *state_,
                      const NkCelTemplate *render_template_,
                      s32                  x_,
                      s32                  y_,
                      u8                   orientation_)
{
  const NkProjectionVariant *projection;
  s32    draw_x;
  s32    draw_y;
  s32    fixed_x;
  s32    fixed_y;
  u32    projection_index;

  if((render_template_ == NULL) ||
     (render_template_->source == NULL) ||
     (state_->fatal_error))
    {
      return;
    }

  projection_index =
    ((u32)state_->aspect.mode << NK_PROJECTION_MODE_SHIFT)
    | ((u32)orientation_ & NK_PROJECTION_ORIENTATION_MASK);
  projection = &g_PROJECTION_VARIANTS[projection_index];
  draw_x = (s32)(
                 x_
                 + ((render_template_->width - 1L) & projection->flip_x_mask)
                 );
  draw_y = (s32)(
                 y_
                 + ((render_template_->height - 1L) & projection->flip_y_mask)
                 );
  fixed_x = draw_x * NK_FIXED_ONE;
  if((state_->aspect.mode == NK_ASPECT_CORRECT) &&
     (draw_y >= NK_RENDER_Y_LOOKUP_MIN) &&
     (draw_y <= NK_RENDER_Y_LOOKUP_MAX))
    {
      fixed_y = state_->render_correct_y_lookup[
                                                (u32)(draw_y - NK_RENDER_Y_LOOKUP_MIN)
                                                ];
    }
  else
    {
      fixed_y = nk_aspect_transform_fixed_y(
                                            state_->aspect.mode,
                                            draw_y * NK_FIXED_ONE
                                            );
    }

  fixed_x -= state_->render_compositor_clip_x * NK_FIXED_ONE;
  fixed_y -= state_->render_compositor_clip_y * NK_FIXED_ONE;
  _nk_draw_cel_template(
                        state_,
                        render_template_,
                        fixed_x,
                        fixed_y,
                        projection
                        );
}


static
void
_nk_fill_rectangle(NkRuntime *state_,
                   s32        left_,
                   s32        top_,
                   s32        right_,
                   s32        bottom_,
                   Color      color_)
{
  Rect rectangle;
  GrafCon graphics;
  Err error;

  if(!_nk_flush_cels(state_))
    {
      return;
    }

  rectangle.rect_XLeft = left_;
  rectangle.rect_YTop = top_;
  rectangle.rect_XRight = right_;
  rectangle.rect_YBottom = bottom_;
  SetFGPen(&graphics, color_);
  error = FillRect(
                   state_->display.sc_BitmapItems[state_->display.sc_curScreen],
                   &graphics,
                   &rectangle
                   );
  if(error < 0)
    {
      _nk_platform_error("FillRect", error);
      state_->fatal_error = 1U;
    }
}


static
void
_nk_draw_solid_rectangle(NkRuntime *state_,
                         u32        color_index_,
                         s32        left_,
                         s32        top_,
                         s32        right_,
                         s32        bottom_,
                         Color      color_)
{
  const NkCelTemplate *render_template;
  CCB *draw;
  s32    width;
  s32    height;

  if((color_index_ >= NK_SOLID_FILL_COLOR_COUNT) ||
     (state_->fatal_error) ||
     (!state_->render_fade_config.draw))
    {
      return;
    }

  width = right_ - left_;
  height = bottom_ - top_;
  if((width <= 0) || (height <= 0))
    {
      return;
    }

  /*
   * The shared append invariant keeps the entry count below the pool
   * limit; the append below flushes immediately when it reaches the limit.
   */
  state_->solid_fill_pixels[color_index_] =
    ((u32)color_) << 16;
  render_template = &state_->solid_fill_templates[color_index_];
  draw = &state_->render_ccbs[state_->render_ccb_count];
  draw->ccb_Flags = render_template->flags;
  draw->ccb_SourcePtr = render_template->source;
  draw->ccb_PLUTPtr = NULL;
  draw->ccb_XPos = left_ * NK_FIXED_ONE;
  draw->ccb_YPos = top_ * NK_FIXED_ONE;
  draw->ccb_HDX = width << NK_CEL_HDX_SHIFT;
  draw->ccb_VDY = height << NK_CEL_VDY_SHIFT;
  draw->ccb_PIXC = PIXC_OPAQUE;
  draw->ccb_PRE0 = render_template->pre0;
  draw->ccb_PRE1 = render_template->pre1;
  state_->render_ccb_count++;
  if(state_->render_ccb_count == NK_RENDER_CCB_LIMIT)
    {
      (void)_nk_flush_cels(state_);
    }
}


static
void
_nk_clip_logical_viewport(NkRuntime *state_)
{
  Color black;

  if(state_->aspect.mode != NK_ASPECT_RAW)
    {
      return;
    }

  black = MakeRGB15(0, 0, 0);
  _nk_fill_rectangle(
                     state_,
                     0,
                     0,
                     NK_SCREEN_WIDTH,
                     NK_ASPECT_RAW_Y_OFFSET,
                     black
                     );
  _nk_fill_rectangle(
                     state_,
                     0,
                     NK_ASPECT_RAW_Y_OFFSET + NK_LOGICAL_HEIGHT,
                     NK_SCREEN_WIDTH,
                     NK_SCREEN_HEIGHT,
                     black
                     );
}


static
bool
_nk_set_bitmap_clip(NkRuntime *state_,
                    s32        x_,
                    s32        y_,
                    s32        width_,
                    s32        height_)
{
  Item bitmap;
  s32    actual_y;
  s32    physical_bottom;
  s32    physical_height;
  int failed;

  if(!_nk_flush_cels(state_))
    {
      return false;
    }

  bitmap = state_->display.sc_BitmapItems[state_->display.sc_curScreen];
  actual_y = nk_aspect_transform_boundary_y(state_->aspect.mode, y_);
  physical_bottom = nk_aspect_transform_boundary_y(
                                                   state_->aspect.mode,
                                                   y_ + height_
                                                   );
  /*
   * Portfolio warns on odd clip origins.  Floor the requested logical Y
   * after projection, and extend the height by the same amount so the
   * logical bottom boundary remains unchanged.  Neutralize the old origin
   * while resizing so it cannot make an intermediate extent invalid.
   */
  actual_y -= actual_y & 1;
  physical_height = physical_bottom - actual_y;
  failed = 0;
  state_->render_compositor_clip_x = 0;
  state_->render_compositor_clip_y = 0;
  if(SetClipOrigin(bitmap, 0, 0) < 0)
    {
      failed = 1;
    }

  if((!failed) && (SetClipWidth(bitmap, width_) < 0))
    {
      failed = 1;
    }

  if((!failed) && (SetClipHeight(bitmap, physical_height) < 0))
    {
      failed = 1;
    }

  if((!failed) && (SetClipOrigin(bitmap, x_, actual_y) < 0))
    {
      failed = 1;
    }

  if(failed)
    {
      /*
       * A resize can fail after one or more clip fields changed.  Restore
       * the full-screen clip best-effort so later fail-closed rendering
       * never inherits a half-applied origin or extent.
       */
      (void)SetClipOrigin(bitmap, 0, 0);
      (void)SetClipWidth(bitmap, NK_SCREEN_WIDTH);
      (void)SetClipHeight(bitmap, NK_SCREEN_HEIGHT);
      (void)SetClipOrigin(bitmap, 0, 0);
      return false;
    }

  state_->render_compositor_clip_x = x_;
  state_->render_compositor_clip_y = actual_y;
  return true;
}


static
bool
_nk_reset_bitmap_clip(NkRuntime *state_)
{
  Item bitmap;

  if(!_nk_flush_cels(state_))
    {
      return false;
    }

  bitmap = state_->display.sc_BitmapItems[state_->display.sc_curScreen];
  if((SetClipOrigin(bitmap, 0, 0) < 0) ||
     (SetClipWidth(bitmap, NK_SCREEN_WIDTH) < 0) ||
     (SetClipHeight(bitmap, NK_SCREEN_HEIGHT) < 0))
    {
      return false;
    }

  state_->render_compositor_clip_x = 0;
  state_->render_compositor_clip_y = 0;
  return true;
}


static
bool
_nk_3do_compositor_emit(void                      *context_,
                        const NkCompositorCommand *command_);

static
void
_nk_render_title(NkRuntime *state_)
{
  NkCompositorSink sink;
  NkTitleView view;
  int voice_playing;

  voice_playing = nk_audio_voice_playing(
                                         &state_->sound,
                                         state_->scene_voice
                                         );
  sink.context = state_;
  sink.emit = _nk_3do_compositor_emit;
  sink.draw_effect = (NkCompositorEffectDraw)0;
  sink.draw_effect_frame = (NkCompositorEffectFrameDraw)0;
  sink.draw_effect_composite = (NkCompositorEffectCompositeDraw)0;
  sink.draw_effect_range = (NkCompositorEffectRangeDraw)0;
  view.title = &state_->title;
  view.scene = &state_->scene;
  view.voice_playing = voice_playing;
  if(!nk_compositor_compose_title(&view, &sink))
    {
      (void)_nk_reset_bitmap_clip(state_);
      kprintf("NK2 title compositor failed\n");
      state_->fatal_error = 1U;
    }
}


static
CCB *
_nk_3do_animation_image(NkRuntime                  *state_,
                        const NkCompositorImageRef *reference_,
                        int                         pain_)
{
  const NkAnimImageSize *size;
  NkCelBank *bank;
  CCB *cel;

  if(pain_)
    {
      if(reference_->bank_index != NK_ANIM_PAIN_BANK)
        {
          return NULL;
        }

      size = nk_anim_pain_image_size(
                                     reference_->bank_index,
                                     reference_->index
                                     );
      bank = &state_->assets.pain_images;
    }
  else
    {
      if(reference_->bank_index >= NK_ANIM_BANK_COUNT)
        {
          return NULL;
        }

      size = nk_anim_image_size(
                                reference_->bank_index,
                                reference_->index
                                );
      bank = &state_->assets.banks[reference_->bank_index];
    }

  if((size == NULL) || (reference_->index >= bank->count))
    {
      return NULL;
    }

  cel = bank->images[reference_->index];
  if((cel == NULL) ||
     (cel->ccb_Width != (int32)size->width) ||
     (cel->ccb_Height != (int32)size->height))
    {
      return NULL;
    }

  return cel;
}


static
const
NkCelTemplate *
_nk_3do_animation_template(NkRuntime                  *state_,
                           const NkCompositorImageRef *reference_,
                           int                         pain_)
{
  const NkAnimImageSize *size;
  const NkCelTemplate *render_template;
  NkCelBank *bank;

  if(pain_)
    {
      if(reference_->bank_index != NK_ANIM_PAIN_BANK)
        {
          return NULL;
        }

      size = nk_anim_pain_image_size(
                                     reference_->bank_index,
                                     reference_->index
                                     );
      bank = &state_->assets.pain_images;
    }
  else
    {
      if(reference_->bank_index >= NK_ANIM_BANK_COUNT)
        {
          return NULL;
        }

      size = nk_anim_image_size(
                                reference_->bank_index,
                                reference_->index
                                );
      bank = &state_->assets.banks[reference_->bank_index];
    }

  if((size == NULL) || (reference_->index >= bank->count))
    {
      return NULL;
    }

  render_template = &bank->templates[reference_->index];
  if((render_template->source == NULL) ||
     (render_template->width != (int32)size->width) ||
     (render_template->height != (int32)size->height))
    {
      return NULL;
    }

  return render_template;
}


static
bool
_nk_3do_draw_projected_effect_template(NkRuntime                 *state_,
                                       const NkCelTemplate       *render_template_,
                                       s32                        x_,
                                       s32                        y_,
                                       const NkProjectionVariant *projection_)
{
  s32    fixed_x;
  s32    fixed_y;

  /*
   * Match effect commands are composed before any scene-local clip and the
   * match compositor never emits a clip command. Title is the only clipped
   * compositor and restores the full-screen origin on success or failure.
   */
  fixed_x = x_ * NK_FIXED_ONE;
  if(state_->aspect.mode == NK_ASPECT_CORRECT)
    {
      fixed_y = state_->render_correct_y_lookup[
                                                (u32)(y_ - NK_RENDER_Y_LOOKUP_MIN)
                                                ];
    }
  else
    {
      fixed_y = nk_aspect_transform_fixed_y(
                                            state_->aspect.mode,
                                            y_ * NK_FIXED_ONE
                                            );
    }

  _nk_draw_cel_template(
                        state_,
                        render_template_,
                        fixed_x,
                        fixed_y,
                        projection_
                        );
  return !state_->fatal_error;
}


static
bool
_nk_3do_draw_effect_template(NkRuntime           *state_,
                             const NkCelTemplate *render_template_,
                             s32                  x_,
                             s32                  y_,
                             u8                   orientation_)
{
  const NkProjectionVariant *projection;
  u32    projection_index;

  projection_index =
    ((u32)state_->aspect.mode << NK_PROJECTION_MODE_SHIFT)
    | ((u32)orientation_ & NK_PROJECTION_ORIENTATION_MASK);
  projection = &g_PROJECTION_VARIANTS[projection_index];
  x_ = (s32)(
             x_
             + ((render_template_->width - 1L) & projection->flip_x_mask)
             );
  y_ = (s32)(
             y_
             + ((render_template_->height - 1L) & projection->flip_y_mask)
             );
  return _nk_3do_draw_projected_effect_template(
                                                state_,
                                                render_template_,
                                                x_,
                                                y_,
                                                projection
                                                );
}


static
bool
_nk_3do_compositor_draw_effect(void  *context_,
                               u32    image_index_,
                               s32    x_,
                               s32    y_,
                               u8     orientation_)
{
  NkRuntime *state;
  const NkAnimBank *metadata;
  const NkAnimImageSize *size;
  const NkCelTemplate *render_template;
  CCB *cel;
  u32    size_index;

  state = (NkRuntime *)context_;
  if((state == NULL) || (state->fatal_error))
    {
      return false;
    }

  metadata = &nk_anim_banks[NK_EFFECT_BANK_INDEX];
  if((image_index_ >= state->assets.banks[NK_EFFECT_BANK_INDEX].count) ||
     (image_index_ >= (u32)metadata->image_index_limit))
    {
      return false;
    }

  size_index = (u32)metadata->image_size_first + image_index_;
  if(size_index >= (u32)nk_anim_image_size_count)
    {
      return false;
    }

  size = &nk_anim_image_sizes[size_index];
  cel = state->assets.banks[NK_EFFECT_BANK_INDEX].images[image_index_];
  render_template =
    &state->assets.banks[NK_EFFECT_BANK_INDEX].templates[image_index_];
  if((cel == NULL) ||
     (render_template->source == NULL) ||
     (cel->ccb_Width != (int32)size->width) ||
     (cel->ccb_Height != (int32)size->height))
    {
      return false;
    }

  return _nk_3do_draw_effect_template(
                                      state,
                                      render_template,
                                      x_,
                                      y_,
                                      orientation_
                                      );
}


static
bool
_nk_3do_compositor_draw_effect_frame(void  *context_,
                                     u32    variant_index_,
                                     s32    x_,
                                     s32    y_)
{
  NkRuntime *state;
  const NkEffectDrawVariant *variant;

  state = (NkRuntime *)context_;
  /*
   * The match sink owns this callback and startup validates every effect
   * frame used to build the fixed variant table. The compositor supplies
   * the validated frame-and-direction variant offset directly. Do not
   * repeat those startup invariants for every blood CCB.
   */
  variant = &state->active_effect_draw_variants[variant_index_];
  x_ += variant->x_offset;
  y_ += variant->y_offset;
  if((x_ >= NK_COMPOSITOR_WIDTH) ||
     (x_ <= -variant->width) ||
     (y_ >= NK_COMPOSITOR_HEIGHT) ||
     (y_ <= -variant->height))
    {
      return true;
    }

  x_ += variant->flip_x_offset;
  y_ += variant->flip_y_offset;
  return _nk_3do_draw_projected_effect_template(
                                                state,
                                                variant->render_template,
                                                x_,
                                                y_,
                                                variant->projection
                                                );
}


static
bool
_nk_3do_compositor_draw_effect_composite(void  *context_,
                                         u32    composite_index_,
                                         s32    x_,
                                         s32    y_)
{
  NkRuntime *state;
  const NkCelTemplate *render_template;

  state = (NkRuntime *)context_;
  if((state == NULL) ||
     (state->fatal_error) ||
     (composite_index_ >= state->assets.effect_composites.count))
    {
      return false;
    }

  render_template =
    &state->assets.effect_composites.templates[composite_index_];
  if(render_template->source == NULL)
    {
      return false;
    }

  return _nk_3do_draw_effect_template(
                                      state,
                                      render_template,
                                      x_,
                                      y_,
                                      0U
                                      );
}


static
bool
_nk_3do_compositor_draw_effect_range(void           *context_,
                                     const NkEffect *effect_,
                                     const u8       *mode_cursor_,
                                     u32             count_)
{
  NkRuntime *state;
  const NkAnimBank *bank;
  const NkAnimFrame *frame;
  const NkAnimImageRef *image;
  const NkAnimImageSize *size;
  const NkCompositorEffectCompositeLayout *composite;
  const NkEffectDrawVariant *variant;
  const NkCelTemplate *render_template;
  const NkProjectionVariant *projection;
  CCB *draw;
  void *plut;
  s32    fixed_x;
  s32    fixed_y;
  s32    x;
  s32    y;
  u32    composite_index;
  u32    draw_flags;
  u32    frame_index;
  u32    image_index;
  u32    variant_index;
  u8    mode;
  u8    orientation;

  state = (NkRuntime *)context_;
  bank = &nk_anim_banks[NK_EFFECT_BANK_INDEX];
  for(; count_ > 0U; --count_, ++effect_, ++mode_cursor_)
    {
      mode = *mode_cursor_;
      if((mode == NK_EFFECT_DRAW_NONE) ||
         (mode == NK_EFFECT_DRAW_STICKY))
        {
          continue;
        }

      if(mode != NK_EFFECT_DRAW_NORMAL)
        {
          return false;
        }

      variant_index = effect_->draw_variant_index;
      frame_index = variant_index >> 1;
      if((frame_index >= NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_FIRST) &&
         (frame_index < NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_FIRST +
          NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_COUNT))
        {
          composite_index = variant_index -
            (NK_COMPOSITOR_EFFECT_COMPOSITE_FRAME_FIRST * 2U);
          composite = &nk_compositor_effect_composite_layouts[
                                                              composite_index
                                                              ];
          x = effect_->x + composite->x;
          y = effect_->y - 80 + composite->y;
          if((x >= NK_COMPOSITOR_WIDTH) ||
             (x <= -(s32)composite->width) ||
             (y >= NK_COMPOSITOR_HEIGHT) ||
             (y <= -(s32)composite->height))
            {
              continue;
            }

          render_template =
            &state->assets.effect_composites.templates[composite_index];
          if(render_template->source == NULL)
            {
              return false;
            }

          projection = &g_PROJECTION_VARIANTS[
                                              (u32)state->aspect.mode << NK_PROJECTION_MODE_SHIFT
                                              ];
          goto effect_prepared;
        }

      frame = effect_->frame;
      if(frame->image_count == 1U)
        {
          variant = &state->active_effect_draw_variants[variant_index];
          x = effect_->x + variant->x_offset;
          y = effect_->y + variant->y_offset;
          if((x >= NK_COMPOSITOR_WIDTH) ||
             (x <= -variant->width) ||
             (y >= NK_COMPOSITOR_HEIGHT) ||
             (y <= -variant->height))
            {
              continue;
            }

          x += variant->flip_x_offset;
          y += variant->flip_y_offset;
          render_template = variant->render_template;
          projection = variant->projection;
        }
      else
        {
          for(image_index = 0U;
              image_index < frame->image_count;
              ++image_index)
            {
              image = &nk_anim_image_refs[
                                          frame->image_first + image_index
                                          ];
              orientation = (u8)(
                                 image->orientation ^
                                 ((variant_index & 1U) << 1)
                                 );
              x = effect_->x;
              if((variant_index & 1U) != 0U)
                {
                  x += image->x_flipped;
                }
              else
                {
                  x += image->x_normal;
                }

              y = effect_->y - 80 + image->y;
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

              if(!_nk_3do_compositor_draw_effect(
                                                 state,
                                                 (u32)image->image,
                                                 x,
                                                 y,
                                                 orientation))
                {
                  return false;
                }
            }

          continue;
        }

    effect_prepared:
      fixed_x = x * NK_FIXED_ONE;
      if(state->aspect.mode == NK_ASPECT_CORRECT)
        {
          fixed_y = state->render_correct_y_lookup[
                                                   (u32)(y - NK_RENDER_Y_LOOKUP_MIN)
                                                   ];
        }
      else
        {
          fixed_y = nk_aspect_transform_fixed_y(
                                                state->aspect.mode,
                                                y * NK_FIXED_ONE
                                                );
        }

      if(state->render_fade_config.draw)
        {
          draw = &state->render_ccbs[state->render_ccb_count];
          draw_flags =
            (u32)render_template->flags |
            state->render_fade_config.required_ccb_flags;
          /*
           * Portfolio ignores PLUTPtr when LDPLUT is clear. Resolve palette
           * reuse before populating this hot-path CCB so a same-palette effect
           * avoids both the ignored pointer store and a second flags store.
           */
          if((draw_flags & CCB_LDPLUT) != 0U)
            {
              plut = render_template->plut;
              if(plut == state->render_loaded_plut)
                {
                  draw_flags &= ~CCB_LDPLUT;
                }
              else
                {
                  draw->ccb_PLUTPtr = plut;
                  state->render_loaded_plut = plut;
                }
            }

          draw->ccb_Flags = draw_flags;
          draw->ccb_SourcePtr = render_template->source;
          draw->ccb_PIXC = state->render_fade_config.pixc;
          draw->ccb_PRE0 = render_template->pre0;
          draw->ccb_PRE1 = render_template->pre1;
          draw->ccb_HDX = projection->hdx;
          draw->ccb_VDY = projection->vdy;
          draw->ccb_XPos = fixed_x;
          draw->ccb_YPos = fixed_y;
          state->render_ccb_count++;
          if(state->render_ccb_count == NK_RENDER_CCB_LIMIT)
            {
              (void)_nk_flush_cels(state);
            }
        }

      if(state->fatal_error)
        {
          return false;
        }
    }

  return true;
}


static
CCB *
_nk_3do_sticky_image(void *context_,
                     u8    image_index_)
{
  NkCompositorImageRef reference;

  reference.kind = (u8)NK_COMPOSITOR_IMAGE_ANIMATION;
  reference.bank_index = NK_EFFECT_BANK_INDEX;
  reference.index = image_index_;
  return _nk_3do_animation_image(
                                 (NkRuntime *)context_,
                                 &reference,
                                 false
                                 );
}


static
const
NkCelTemplate *
_nk_3do_compositor_template(NkRuntime                  *state_,
                            const NkCompositorImageRef *reference_)
{
  s32    font_image;

  if((state_ == NULL) || (reference_ == NULL))
    {
      return NULL;
    }

  switch((NkCompositorImageKind)reference_->kind)
    {
    case NK_COMPOSITOR_IMAGE_SCENE:
      if(reference_->bank_index != state_->scene.bank_index)
        {
          return NULL;
        }

      if(reference_->index < state_->assets.scene_images.count)
        {
          return &state_->assets.scene_images.templates[reference_->index];
        }

      break;
    case NK_COMPOSITOR_IMAGE_SCENE_GRAY:
      if((reference_->bank_index != NK_SCENE_BANK_SELECT) ||
         (state_->scene.bank_index != NK_SCENE_BANK_SELECT))
        {
          return NULL;
        }

      if(reference_->index < state_->assets.selector_gray.count)
        {
          return &state_->assets.selector_gray.templates[reference_->index];
        }

      break;
    case NK_COMPOSITOR_IMAGE_SCENE_LAYER:
      if(reference_->bank_index != state_->scene.bank_index)
        {
          return NULL;
        }

      if(reference_->index == NK_COMPOSITOR_LAYER_1A)
        {
          return &state_->assets.scene_layer1a.render_template;
        }

      if((reference_->index == NK_COMPOSITOR_LAYER_1_COMPOSITE) &&
         (reference_->bank_index == NK_SCENE_BANK_GAME))
        {
          return &state_->assets.scene_layer1a.render_template;
        }

      if(reference_->index == NK_COMPOSITOR_LAYER_1B)
        {
          return &state_->assets.scene_layer1b.render_template;
        }

      if(reference_->index == NK_COMPOSITOR_LAYER_2)
        {
          return &state_->assets.scene_layer2.render_template;
        }

      if(reference_->index == NK_COMPOSITOR_LAYER_3)
        {
          return &state_->assets.scene_layer3.render_template;
        }

      break;
    case NK_COMPOSITOR_IMAGE_SELECTOR_MARK:
      if((reference_->bank_index != NK_SCENE_BANK_SELECT) ||
         (state_->scene.bank_index != NK_SCENE_BANK_SELECT))
        {
          return NULL;
        }

      if(reference_->index < state_->assets.selector_marks.count)
        {
          return &state_->assets.selector_marks.templates[reference_->index];
        }

      break;
    case NK_COMPOSITOR_IMAGE_CINEMA:
      if((reference_->bank_index != state_->cinema.bank_index) ||
         (reference_->index >= state_->assets.cinema_images.count))
        {
          return NULL;
        }

      return &state_->assets.cinema_images.templates[reference_->index];
    case NK_COMPOSITOR_IMAGE_FONT_SMALL_WHITE:
      if((reference_->bank_index != NK_FONT_SMALL) ||
         (reference_->index >= NK_FONT_CHARACTER_COUNT))
        {
          return NULL;
        }

      font_image = nk_font_image_index(
                                       NK_FONT_SMALL,
                                       (unsigned char)reference_->index
                                       );
      if((font_image >= 0) &&
         ((u32)font_image <
          state_->assets.fonts[
                               NK_RUNTIME_FONT_WHITE
                               ].count))
        {
          return &state_->assets.fonts[
                                       NK_RUNTIME_FONT_WHITE
                                       ].templates[font_image];
        }

      break;
    case NK_COMPOSITOR_IMAGE_ANIMATION:
      return _nk_3do_animation_template(state_, reference_, false);
    case NK_COMPOSITOR_IMAGE_FONT_BLUE:
      if((reference_->bank_index != NK_FONT_BLUE) ||
         (reference_->index >= NK_FONT_CHARACTER_COUNT))
        {
          return NULL;
        }

      font_image = nk_font_image_index(
                                       NK_FONT_BLUE,
                                       (unsigned char)reference_->index
                                       );
      if((font_image >= 0) &&
         ((u32)font_image <
          state_->assets.fonts[
                               NK_RUNTIME_FONT_BLUE
                               ].count))
        {
          return &state_->assets.fonts[
                                       NK_RUNTIME_FONT_BLUE
                                       ].templates[font_image];
        }

      break;
    case NK_COMPOSITOR_IMAGE_FONT_RED:
      if((reference_->bank_index != NK_FONT_RED) ||
         (reference_->index >= NK_FONT_CHARACTER_COUNT))
        {
          return NULL;
        }

      font_image = nk_font_image_index(
                                       NK_FONT_RED,
                                       (unsigned char)reference_->index
                                       );
      if((font_image >= 0) &&
         ((u32)font_image <
          state_->assets.fonts[
                               NK_RUNTIME_FONT_RED
                               ].count))
        {
          return &state_->assets.fonts[
                                       NK_RUNTIME_FONT_RED
                                       ].templates[font_image];
        }

      break;
    case NK_COMPOSITOR_IMAGE_ANIMATION_PAIN:
      return _nk_3do_animation_template(state_, reference_, true);
    case NK_COMPOSITOR_IMAGE_FONT_SMALL_ICER:
      if((reference_->bank_index != NK_FONT_SMALL) ||
         (reference_->index >= NK_FONT_CHARACTER_COUNT))
        {
          return NULL;
        }

      font_image = nk_font_image_index(
                                       NK_FONT_SMALL,
                                       (unsigned char)reference_->index
                                       );
      if((font_image >= 0) &&
         ((u32)font_image <
          state_->assets.fonts[
                               NK_RUNTIME_FONT_ICER
                               ].count))
        {
          return &state_->assets.fonts[
                                       NK_RUNTIME_FONT_ICER
                                       ].templates[font_image];
        }

      break;
    case NK_COMPOSITOR_IMAGE_FONT_SMALL_STUMP:
      if((reference_->bank_index != NK_FONT_SMALL) ||
         (reference_->index >= NK_FONT_CHARACTER_COUNT))
        {
          return NULL;
        }

      font_image = nk_font_image_index(
                                       NK_FONT_SMALL,
                                       (unsigned char)reference_->index
                                       );
      if((font_image >= 0) &&
         ((u32)font_image <
          state_->assets.fonts[
                               NK_RUNTIME_FONT_STUMP
                               ].count))
        {
          return &state_->assets.fonts[
                                       NK_RUNTIME_FONT_STUMP
                                       ].templates[font_image];
        }

      break;
    case NK_COMPOSITOR_IMAGE_PORT_CREDIT:
      if((state_->flow.scene != NK_SCENE_PORT_CREDIT) ||
         (reference_->index != 0U))
        {
          return NULL;
        }

      return &state_->assets.port_credit.render_template;
    default:
      break;
    }

  return NULL;
}


static
bool
_nk_3do_compositor_emit(void                      *context_,
                        const NkCompositorCommand *command_)
{
  NkRuntime *state;
  const NkCelTemplate *render_template;
  Color fill_color;
  s32    green;
  bool draw_result;

  state = (NkRuntime *)context_;
  if((state == NULL) || (command_ == NULL) || (state->fatal_error))
    {
      return false;
    }

  switch((NkCompositorCommandType)command_->type)
    {
    case NK_COMPOSITOR_BEGIN:
      if((command_->value.begin.palette.kind ==
          (u8)NK_COMPOSITOR_PALETTE_SCENE) &&
         (command_->value.begin.palette.bank_index ==
          state->scene.bank_index) &&
         (state->scene.valid))
        {
        }
      else if((command_->value.begin.palette.kind ==
               (u8)NK_COMPOSITOR_PALETTE_CINEMA) &&
              (command_->value.begin.palette.bank_index ==
               state->cinema.bank_index) &&
              (((command_->value.begin.palette.index ==
                 NK_CINEMA_NO_INDEX) &&
                (state->cinema.current_background < 0) &&
                (command_->value.begin.fade_level == 0U)) ||
               ((state->cinema.current_background >= 0) &&
                (command_->value.begin.palette.index ==
                 (u32)state->cinema.current_background) &&
                (command_->value.begin.palette.index <
                 state->assets.cinema_images.count))))
        {
        }
      else if((command_->value.begin.palette.kind ==
               (u8)NK_COMPOSITOR_PALETTE_PORT_CREDIT) &&
              (state->flow.scene == NK_SCENE_PORT_CREDIT) &&
              (command_->value.begin.palette.bank_index == 0U) &&
              (command_->value.begin.palette.index == 0U) &&
              (command_->value.begin.fade_level <=
               NK_PORT_CREDIT_FADE_MAX))
        {
        }
      else
        {
          return false;
        }

      state->render_fade_level = command_->value.begin.fade_level;
      nk_fade_configure(
                        state->render_fade_level,
                        &state->render_fade_config
                        );
      return true;
    case NK_COMPOSITOR_CLEAR:
      if(command_->value.clear.color != 0U)
        {
          return false;
        }

      _nk_clear_screen(state);
      return !state->fatal_error;
    case NK_COMPOSITOR_DRAW:
      render_template = _nk_3do_compositor_template(
                                                    state,
                                                    &command_->value.draw.image
                                                    );
      if((render_template == NULL) || (render_template->source == NULL))
        {
          return false;
        }

      if((command_->value.draw.image.kind ==
          (u8)NK_COMPOSITOR_IMAGE_ANIMATION) &&
         (command_->value.draw.image.bank_index ==
          NK_EFFECT_BANK_INDEX))
        {
          draw_result = _nk_3do_draw_effect_template(
                                                     state,
                                                     render_template,
                                                     command_->value.draw.x,
                                                     command_->value.draw.y,
                                                     command_->value.draw.orientation
                                                     );
          return draw_result;
        }

      _nk_draw_prepared_cel(
                            state,
                            render_template,
                            command_->value.draw.x,
                            command_->value.draw.y,
                            command_->value.draw.orientation
                            );
      return !state->fatal_error;
    case NK_COMPOSITOR_STICKY_SURFACE:
      _nk_draw_prepared_cel(
                            state,
                            &state->sticky_surface_template,
                            0,
                            NK_STICKY_FLOOR_Y,
                            0U
                            );
      return !state->fatal_error;
    case NK_COMPOSITOR_CLIP:
      if((command_->value.clip.x == 0) &&
         (command_->value.clip.y == 0) &&
         (command_->value.clip.width == NK_COMPOSITOR_WIDTH) &&
         (command_->value.clip.height == NK_COMPOSITOR_HEIGHT))
        {
          if(!_nk_reset_bitmap_clip(state))
            {
              return false;
            }

          return true;
        }

      if((command_->value.clip.width <= 0) ||
         (command_->value.clip.height <= 0))
        {
          return false;
        }

      if(!_nk_set_bitmap_clip(
                              state,
                              command_->value.clip.x,
                              command_->value.clip.y,
                              command_->value.clip.width,
                              command_->value.clip.height))
        {
          return false;
        }

      return true;
    case NK_COMPOSITOR_FILL:
      if((command_->value.fill.width <= 0) ||
         (command_->value.fill.height <= 0))
        {
          return false;
        }

      if(command_->value.fill.color == 0U)
        {
          fill_color = MakeRGB15(0, 0, 0);
        }
      else if(command_->value.fill.color == 2U)
        {
          green = (
                   21 * state->render_fade_level + 16
                   ) / 32;
          fill_color = MakeRGB15(0, green, 0);
        }
      else
        {
          return false;
        }

      /*
       * Use an opaque, uncoded 1x1 source pixel projected to the requested
       * bounds.  This keeps the status bars in the main DrawCels chain
       * instead of forcing synchronous FillRect barriers.
       */
      _nk_draw_solid_rectangle(
                               state,
                               (command_->value.fill.color == 0U) ? 0U : 1U,
                               command_->value.fill.x -
                               state->render_compositor_clip_x,
                               nk_aspect_transform_boundary_y(
                                                              state->aspect.mode,
                                                              command_->value.fill.y
                                                              ) -
                               state->render_compositor_clip_y,
                               command_->value.fill.x +
                               command_->value.fill.width -
                               state->render_compositor_clip_x,
                               nk_aspect_transform_boundary_y(
                                                              state->aspect.mode,
                                                              command_->value.fill.y +
                                                              command_->value.fill.height
                                                              ) -
                               state->render_compositor_clip_y,
                               fill_color
                               );
      return !state->fatal_error;
    case NK_COMPOSITOR_END:
      return true;
    default:
      break;
    }

  return false;
}


static
bool
_nk_prepare_match_tick(NkRuntime *state_,
                       int        materialize_)
{
  if(!nk_match_present_prepare_tick(
                                    &state_->match_presentation,
                                    &state_->game,
                                    &state_->dialogue,
                                    state_->outcome_started,
                                    state_->flow.options.talking,
                                    false,
                                    (state_->match_exit != 0) || state_->cancel_pending,
                                    state_->paused,
                                    materialize_))
    {
      kprintf("NK2 match presentation prepare failed\n");
      state_->fatal_error = 1U;
      return false;
    }

  if(state_->match_presentation.outcome_began)
    {
      state_->outcome_started = 1U;
      state_->outcome_start_tick = state_->game.tick;
    }

  if(state_->cancel_pending)
    {
      state_->match_exit = 1;
      state_->match_fade_out = 1U;
      state_->cancel_pending = 0U;
    }
  else if(state_->outcome_continue_pending)
    {
      nk_dialogue_finish_match(&state_->dialogue);
      state_->match_exit = 2;
      state_->match_fade_out = 1U;
      state_->outcome_continue_pending = 0U;
    }

  return true;
}


static
void
_nk_render_match(NkRuntime *state_)
{
  NkCompositorSink sink;
  NkMatchView view;

  if(!state_->match_presentation.prepared)
    {
      kprintf("NK2 match presentation is not prepared\n");
      state_->fatal_error = 1U;
      return;
    }

  sink.context = state_;
  sink.emit = _nk_3do_compositor_emit;
  sink.draw_effect = _nk_3do_compositor_draw_effect;
  sink.draw_effect_frame = _nk_3do_compositor_draw_effect_frame;
  sink.draw_effect_composite =
    _nk_3do_compositor_draw_effect_composite;
  sink.draw_effect_range = _nk_3do_compositor_draw_effect_range;
  nk_match_present_set_phase(
                             &state_->match_presentation,
                             state_->paused
                             ? NK_MATCH_PRESENT_PHASE_ONE
                             : nk_tick_clock_phase_16(&state_->simulation_clock)
                             );
  view.game = &state_->game;
  view.presentation = &state_->match_presentation;
  view.scene = &state_->scene;
  view.dialogue =
    &state_->match_presentation.dialogue_snapshot;
  view.scene_scroll = state_->scene_scroll;
  view.fade_level = state_->match_fade_level;
  view.talking = (u8)(state_->flow.options.talking != 0);
  view.paused = state_->paused;
  view.quit = (u8)(
                   state_->match_exit != 0 || state_->cancel_pending
                   );
  /* The final controller mapping does not expose retail background hiding. */
  view.background_hidden = 0U;
  view.shock_phase = state_->match_shock_phase;
  if(!nk_compositor_compose_match(&view, &sink))
    {
      kprintf("NK2 match compositor failed\n");
      state_->fatal_error = 1U;
      return;
    }
}


static
void
_nk_render_select(NkRuntime *state_)
{
  NkCompositorSink sink;
  NkSelectorView view;

  sink.context = state_;
  sink.emit = _nk_3do_compositor_emit;
  sink.draw_effect = (NkCompositorEffectDraw)0;
  sink.draw_effect_frame = (NkCompositorEffectFrameDraw)0;
  sink.draw_effect_composite = (NkCompositorEffectCompositeDraw)0;
  sink.draw_effect_range = (NkCompositorEffectRangeDraw)0;
  view.selection = &state_->selection;
  view.scene = &state_->scene;
  view.scene_scroll = state_->scene_scroll;
  if(!nk_compositor_compose_selector(&view, &sink))
    {
      kprintf("NK2 selector compositor failed\n");
      state_->fatal_error = 1U;
    }
}


static
void
_nk_render_options(NkRuntime *state_)
{
  NkCompositorSink sink;
  NkOptionsView view;

  sink.context = state_;
  sink.emit = _nk_3do_compositor_emit;
  sink.draw_effect = (NkCompositorEffectDraw)0;
  sink.draw_effect_frame = (NkCompositorEffectFrameDraw)0;
  sink.draw_effect_composite = (NkCompositorEffectCompositeDraw)0;
  sink.draw_effect_range = (NkCompositorEffectRangeDraw)0;
  view.options = &state_->options;
  view.scene = &state_->scene;
  view.skull = &state_->option_skull;
  view.scene_scroll = state_->scene_scroll;
  if(!nk_compositor_compose_options(&view, &sink))
    {
      kprintf("NK2 options compositor failed\n");
      state_->fatal_error = 1U;
    }
}


static
void
_nk_render_cinema(NkRuntime *state_)
{
  NkCompositorSink sink;
  NkCinemaView view;

  sink.context = state_;
  sink.emit = _nk_3do_compositor_emit;
  sink.draw_effect = (NkCompositorEffectDraw)0;
  sink.draw_effect_frame = (NkCompositorEffectFrameDraw)0;
  sink.draw_effect_composite = (NkCompositorEffectCompositeDraw)0;
  sink.draw_effect_range = (NkCompositorEffectRangeDraw)0;
  view.cinema = &state_->cinema;
  if(!nk_compositor_compose_cinema(&view, &sink))
    {
      kprintf("NK2 cinema compositor failed\n");
      state_->fatal_error = 1U;
    }
}


static
void
_nk_render_port_credit(NkRuntime *state_)
{
  NkCompositorSink sink;
  NkPortCreditView view;

  sink.context = state_;
  sink.emit = _nk_3do_compositor_emit;
  sink.draw_effect = (NkCompositorEffectDraw)0;
  sink.draw_effect_frame = (NkCompositorEffectFrameDraw)0;
  sink.draw_effect_composite = (NkCompositorEffectCompositeDraw)0;
  sink.draw_effect_range = (NkCompositorEffectRangeDraw)0;
  view.credit = &state_->port_credit;
  if(!nk_compositor_compose_port_credit(&view, &sink))
    {
      kprintf("NK2 port credit compositor failed\n");
      state_->fatal_error = 1U;
    }
}


static
void
_nk_render(NkRuntime *state_)
{
  state_->render_fade_level = NK_UI_FADE_LEVEL_MAX;
  if(state_->flow.scene == NK_SCENE_MATCH)
    {
      state_->render_fade_level = state_->match_fade_level;
    }

  switch(state_->flow.scene)
    {
    case NK_SCENE_LOGO:
    case NK_SCENE_CREDITS:
    case NK_SCENE_ENDING:
      _nk_render_cinema(state_);
      break;
    case NK_SCENE_PORT_CREDIT:
      _nk_render_port_credit(state_);
      break;
    case NK_SCENE_TITLE:
      _nk_render_title(state_);
      break;
    case NK_SCENE_OPTIONS:
      _nk_render_options(state_);
      break;
    case NK_SCENE_SELECT:
      _nk_render_select(state_);
      break;
    case NK_SCENE_MATCH:
      _nk_render_match(state_);
      break;
    default:
      _nk_clear_screen(state_);
      break;
    }

  if((state_->flow.scene == NK_SCENE_LOGO) ||
     (state_->flow.scene == NK_SCENE_CREDITS) ||
     (state_->flow.scene == NK_SCENE_ENDING) ||
     (state_->flow.scene == NK_SCENE_PORT_CREDIT) ||
     (state_->flow.scene == NK_SCENE_TITLE) ||
     (state_->flow.scene == NK_SCENE_OPTIONS) ||
     (state_->flow.scene == NK_SCENE_SELECT) ||
     (state_->flow.scene == NK_SCENE_MATCH))
    {
      _nk_clip_logical_viewport(state_);
    }
}


static
Err
_nk_set_display_interpolation(const ScreenContext *display_,
                              u8                   mode_)
{
  Err error;
  int screen_index;

  for(screen_index = 0;
      screen_index < display_->sc_NumScreens;
      ++screen_index)
    {
      if((mode_ & NK_DISPLAY_INTERPOLATION_HORIZONTAL) != 0U)
        {
          error = EnableHAVG(display_->sc_Screens[screen_index]);
        }
      else
        {
          error = DisableHAVG(display_->sc_Screens[screen_index]);
        }

      if(error < 0)
        {
          return error;
        }

      if((mode_ & NK_DISPLAY_INTERPOLATION_VERTICAL) != 0U)
        {
          error = EnableVAVG(display_->sc_Screens[screen_index]);
        }
      else
        {
          error = DisableVAVG(display_->sc_Screens[screen_index]);
        }

      if(error < 0)
        {
          return error;
        }
    }

  return 0;
}


static
const
char *
_nk_display_interpolation_name(u8    mode_)
{
  switch(mode_)
    {
    case NK_DISPLAY_INTERPOLATION_OFF:
      return "off";
    case NK_DISPLAY_INTERPOLATION_VERTICAL:
      return "vertical";
    case NK_DISPLAY_INTERPOLATION_HORIZONTAL:
      return "horizontal";
    case NK_DISPLAY_INTERPOLATION_BOTH:
      return "both";
    default:
      return "unknown";
    }
}


static
Err
_nk_update_display_interpolation(NkRuntime *state_,
                                 u8         commands_)
{
  u8    previous_mode;
  Err error;

  previous_mode = state_->display_interpolation_mode;
  nk_display_interpolation_toggle_step(
                                       &state_->display_interpolation_mode,
                                       &state_->display_interpolation_toggle_down,
                                       commands_
                                       );
  if(state_->display_interpolation_mode == previous_mode)
    {
      return 0;
    }

  error = _nk_set_display_interpolation(
                                        &state_->display,
                                        state_->display_interpolation_mode
                                        );
  if(error < 0)
    {
      return error;
    }

  kprintf("NK2 display interpolation=%s\n",
          _nk_display_interpolation_name(state_->display_interpolation_mode));
  return 0;
}


static
void
_nk_make_input(nk_input_filter *filter_,
               u32              buttons_,
               nk_input_sample *input_)
{
  u8    direction;
  u8    action;
  u8    commands;

  direction = 0U;
  action = 0U;
  commands = 0U;
  if((buttons_ & ControlLeft) != 0U)
    {
      direction |= NK_DIR_LEFT;
    }

  if((buttons_ & ControlRight) != 0U)
    {
      direction |= NK_DIR_RIGHT;
    }

  if((buttons_ & ControlUp) != 0U)
    {
      direction |= NK_DIR_UP;
    }

  if((buttons_ & ControlDown) != 0U)
    {
      direction |= NK_DIR_DOWN;
    }

  if((buttons_ & ControlA) != 0U)
    {
      action |= NK_BUTTON_ONE;
    }

  if((buttons_ & ControlB) != 0U)
    {
      action |= NK_BUTTON_TWO;
    }

  if((buttons_ & ControlStart) != 0U)
    {
      commands |= NK_COMMAND_PAUSE;
    }

  if((buttons_ & ControlC) != 0U)
    {
      commands |= NK_COMMAND_CANCEL;
    }

  if((buttons_ & ControlX) != 0U)
    {
      commands |= NK_COMMAND_ASPECT_TOGGLE;
    }

  if((buttons_ & ControlLeftShift) != 0U)
    {
      commands |= NK_COMMAND_INTERPOLATION_TOGGLE;
    }

  nk_input_sample_make(filter_, direction, action, commands, input_);
}


static
void
_nk_leave_scene(NkRuntime *state_)
{
  _nk_unload_scene_samples(state_);
  _nk_unload_pause_adjust_sample(state_);
  _nk_unload_select_scream(state_);
  nk_audio_leave_select(&state_->sound);
  _nk_unload_scene_assets(&state_->assets);
  memset(
         g_EFFECT_DRAW_VARIANTS,
         0,
         sizeof(g_EFFECT_DRAW_VARIANTS)
         );
}


static
bool
_nk_load_title_scene(NkRuntime *state_)
{
  if((_nk_load_gameplay_assets(&state_->assets)) &&
     (_nk_load_title_assets(&state_->assets)) &&
     (_nk_load_scene_samples(state_, "titlesnd", 0x1fU)) &&
     (nk_scene_begin(&state_->scene, NK_SCENE_BANK_TITLE)))
    {
      return true;
    }

  _nk_unload_scene_samples(state_);
  _nk_unload_scene_assets(&state_->assets);
  return false;
}


static
bool
_nk_load_title_scene_with_eviction(NkRuntime *state_)
{
  if(_nk_load_title_scene(state_))
    {
      return true;
    }

  if(state_->assets.select_bundle.root == NULL)
    {
      return false;
    }

  kprintf("NK2 asset cache evict name=select reason=title_load_retry\n");
  _nk_unload_select_resident_assets(&state_->assets);
  nk_audio_unload_select(&state_->sound);
  return _nk_load_title_scene(state_);
}


static
bool
_nk_load_combat_audio_with_eviction(NkRuntime *state_)
{
  u8    player_zero_type;
  u8    player_one_type;

  player_zero_type = state_->game.players[0].character_type;
  player_one_type = state_->game.players[1].character_type;
  if(nk_audio_load_combat(
                          &state_->sound,
                          player_zero_type,
                          player_one_type))
    {
      return true;
    }

  if(state_->assets.select_bundle.root == NULL)
    {
      return false;
    }

  kprintf("NK2 asset cache evict name=select reason=combat_load_retry\n");
  _nk_unload_select_resident_assets(&state_->assets);
  nk_audio_unload_select(&state_->sound);
  return nk_audio_load_combat(
                              &state_->sound,
                              player_zero_type,
                              player_one_type
                              );
}


static
bool
_nk_load_select_assets_with_eviction(NkRuntime *state_,
                                     int        options_)
{
  if(_nk_load_select_assets(&state_->assets, options_))
    {
      return true;
    }

  kprintf("NK2 audio cache evict bank=combat reason=select_load_retry\n");
  nk_audio_unload_combat(&state_->sound);
  if(_nk_load_select_assets(&state_->assets, options_))
    {
      return true;
    }

  if(state_->music.initialized)
    {
      /*
       * The selector has no music of its own.  A completed match can leave
       * the stopped spool player holding enough fragmented DRAM that the
       * selector CEL cannot be reloaded even after combat samples are freed.
       * Release that inactive cache and recreate it when the next music scene
       * starts.
       */
      kprintf("NK2 music cache evict reason=select_load_retry\n");
      nk_music_shutdown(&state_->music);
      return _nk_load_select_assets(&state_->assets, options_);
    }

  return false;
}


static
bool
_nk_load_screams_with_eviction(NkRuntime *state_)
{
  u32    attempt;

  for(attempt = 0U;
      attempt <= NK_ASSET_BUNDLE_COUNT;
      ++attempt)
    {
      if(nk_audio_load_screams(&state_->sound))
        {
          return true;
        }

      if(!_nk_evict_lru_cold_bundle(
                                    &state_->assets,
                                    NK_ASSET_BUNDLE_COUNT))
        {
          return false;
        }
    }

  return false;
}


static
const
char *
_nk_scene_name(u8    scene_)
{
  switch(scene_)
    {
    case NK_SCENE_LOGO:
      return "logo";
    case NK_SCENE_CREDITS:
      return "credits";
    case NK_SCENE_TITLE:
      return "title";
    case NK_SCENE_OPTIONS:
      return "options";
    case NK_SCENE_SELECT:
      return "select";
    case NK_SCENE_MATCH:
      return "match";
    case NK_SCENE_ENDING:
      return "ending";
    case NK_SCENE_EXIT:
      return "exit";
    case NK_SCENE_PORT_CREDIT:
      return "port-credit";
    default:
      return "unknown";
    }
}


static
void
_nk_log_scene_enter(u8            scene_,
                    unsigned long asset_load_start_)
{
  unsigned long asset_load_end;

  asset_load_end = (unsigned long)GetAudioTime();
  kprintf("NK2 scene entered=%s\n", _nk_scene_name(scene_));
  kprintf("NK2 asset load scene=%u ticks=%lu\n",
          (unsigned int)scene_,
          asset_load_end - asset_load_start_);
}


static
bool
_nk_enter_scene(NkRuntime *state_)
{
  const NkCinemaBank *cinema_bank;
  unsigned long asset_load_start;
  s32    match_difficulty;
  u8    cinema_index;
  u8    carry_select_input;

  asset_load_start = (unsigned long)GetAudioTime();
  _nk_leave_scene(state_);
  carry_select_input = (u8)(
                            state_->select_match_input_carry &&
                            state_->flow.scene == NK_SCENE_MATCH
                            );
  state_->select_match_input_carry = 0U;
  state_->scene_generation++;
  state_->scene_scroll = 0;
  state_->outcome_started = 0U;
  state_->outcome_continue_pending = 0U;
  state_->cancel_pending = 0U;
  state_->outcome_start_tick = 0U;
  state_->match_exit = 0;
  state_->match_fade_level = 0U;
  state_->match_fade_out = 0U;
  state_->paused = 0U;
  state_->pause_down = 0U;
  state_->pause_volume_stat = 0U;

  switch(state_->flow.scene)
    {
    case NK_SCENE_LOGO:
      cinema_index = NK_CINEMA_LOGO;
      break;
    case NK_SCENE_CREDITS:
      cinema_index = NK_CINEMA_CREDITS;
      break;
    case NK_SCENE_ENDING:
      if((state_->flow.ending_number < 1) ||
         (state_->flow.ending_number > 8))
        {
          return false;
        }

      cinema_index = (u8)(
                          NK_CINEMA_ENDING_FIRST + state_->flow.ending_number - 1
                          );
      break;
    default:
      cinema_index = NK_CINEMA_BANK_COUNT;
      break;
    }

  if(cinema_index < NK_CINEMA_BANK_COUNT)
    {
      nk_audio_unload_combat(&state_->sound);
      nk_audio_unload_select(&state_->sound);
      nk_audio_unload_screams(&state_->sound);
      _nk_unload_select_resident_assets(&state_->assets);
      _nk_unload_gameplay_assets(&state_->assets);
      cinema_bank = nk_cinema_bank(cinema_index);
      if((cinema_bank == NULL) ||
         (!_nk_load_cinema_assets(
                                  &state_->assets,
                                  cinema_index)) ||
         (!_nk_load_scene_samples(
                                  state_,
                                  g_CINEMA_DIRECTORIES[cinema_index],
                                  cinema_bank->sound_mask)) ||
         (!nk_cinema_begin(&state_->cinema, cinema_index)))
        {
          return false;
        }

      _nk_log_scene_enter(state_->flow.scene, asset_load_start);
      return true;
    }

  switch(state_->flow.scene)
    {
    case NK_SCENE_PORT_CREDIT:
      nk_audio_unload_combat(&state_->sound);
      nk_audio_unload_select(&state_->sound);
      nk_audio_unload_screams(&state_->sound);
      _nk_unload_select_resident_assets(&state_->assets);
      _nk_unload_gameplay_assets(&state_->assets);
      if(!_nk_load_port_credit_assets(&state_->assets))
        {
          return false;
        }

      nk_port_credit_begin(&state_->port_credit);
      _nk_log_scene_enter(state_->flow.scene, asset_load_start);
      return true;

    case NK_SCENE_TITLE:
      /*
       * A return to title ends the active tournament.  Its combat and
       * selector-scream banks are no longer useful, while select3 remains
       * warm for either the options screen or the next character selection.
       */
      nk_audio_unload_combat(&state_->sound);
      nk_audio_unload_screams(&state_->sound);
      if(!_nk_load_title_scene_with_eviction(state_))
        {
          return false;
        }

      nk_title_begin(&state_->title);
      _nk_log_scene_enter(state_->flow.scene, asset_load_start);
      return true;

    case NK_SCENE_OPTIONS:
      if((!_nk_load_gameplay_assets(&state_->assets)) ||
         (!_nk_load_select_assets_with_eviction(
                                                state_,
                                                true)) ||
         (!_nk_load_select_samples(state_)) ||
         (!nk_scene_begin(&state_->scene, NK_SCENE_BANK_SELECT)) ||
         (!nk_anim_cursor_start(&state_->option_skull, 9U, 27U)))
        {
          return false;
        }

      /* OptionsScreen starts its background at bg.scrx = 160. */
      state_->scene_scroll = 160;
      nk_options_begin(&state_->options, &state_->flow.options);
      _nk_log_scene_enter(state_->flow.scene, asset_load_start);
      return true;

    case NK_SCENE_SELECT:
      if((!_nk_load_gameplay_assets(&state_->assets)) ||
         (!_nk_load_select_assets_with_eviction(
                                                state_,
                                                false)) ||
         (!_nk_load_select_samples(state_)) ||
         (!_nk_load_screams_with_eviction(state_)) ||
         (!nk_scene_begin(&state_->scene, NK_SCENE_BANK_SELECT)))
        {
          return false;
        }

      nk_select_begin(&state_->selection, &state_->flow, &state_->flow_rng);
      _nk_log_scene_enter(state_->flow.scene, asset_load_start);
      return true;

    case NK_SCENE_MATCH:
      match_difficulty = nk_flow_match_difficulty(
                                                  &state_->flow,
                                                  &state_->flow_rng
                                                  );
      if(!nk_game_reconfigure(
                              &state_->game,
                              state_->flow.selected_character[0],
                              state_->flow.control[0],
                              state_->flow.selected_character[1],
                              state_->flow.control[1],
                              nk_options_round_target(&state_->flow.options),
                              match_difficulty,
                              state_->flow.options.fix_orig_bugs,
                              state_->flow_rng.state))
        {
          return false;
        }

      nk_game_begin_match(&state_->game);
      if(carry_select_input)
        {
          u8    directions[NK_PLAYER_COUNT];

          directions[0] = state_->selection.previous_stat[0];
          directions[1] = state_->selection.previous_stat[1];
          nk_game_seed_match_directions(
                                        &state_->game,
                                        directions
                                        );
        }

      nk_match_present_reset(&state_->match_presentation);
      state_->match_shock_phase = 0U;
      nk_dialogue_begin_match(&state_->dialogue);
      if((!_nk_load_gameplay_assets(&state_->assets)) ||
         (!_nk_load_combat_audio_with_eviction(state_)) ||
         (!_nk_load_pause_adjust_sample(state_)) ||
         (!nk_scene_begin(&state_->scene, NK_SCENE_BANK_GAME)) ||
         (!_nk_load_match_assets(&state_->assets)) ||
         (!_nk_initialize_effect_draw_variants(&state_->assets)))
        {
          return false;
        }

      /*
       * The DOS floor is created once immediately before PlayGame and
       * survives every round in that match.  Seed this bounded equivalent
       * from the exact visible layer1b crop at the same lifetime boundary.
       */
      if(!nk_sticky_target_reset(
                                 &state_->sticky_target,
                                 state_->assets.scene_layer1a.image))
        {
          return false;
        }

      if(!_nk_prepare_match_tick(state_, true))
        {
          return false;
        }

      kprintf("NK2 match player1=%u (%s)\n",
              (unsigned int)state_->flow.selected_character[0],
              nk_character_name(state_->flow.selected_character[0]));
      kprintf("NK2 match player2=%u (%s)\n",
              (unsigned int)state_->flow.selected_character[1],
              nk_character_name(state_->flow.selected_character[1]));
      kprintf("NK2 match difficulty=%ld\n",
              (long)match_difficulty);
      _nk_log_scene_enter(state_->flow.scene, asset_load_start);
      return true;

    case NK_SCENE_EXIT:
      _nk_log_scene_enter(state_->flow.scene, asset_load_start);
      return true;

    default:
      return false;
    }
}


static
u8
_nk_scene_music_track(const NkRuntime *state_)
{
  switch(state_->flow.scene)
    {
    case NK_SCENE_TITLE:
      return NK_MUSIC_TRACK_TITLE_MIDI;
    case NK_SCENE_OPTIONS:
      /*
       * The DOS data carries a second title MIDI selected by its AWE32 path.
       * Use the options screen to keep that preserved alternative reachable.
       */
      return NK_MUSIC_TRACK_TITLE_AWE32;
    case NK_SCENE_MATCH:
      return NK_MUSIC_TRACK_MATCH_CMF;
    default:
      return NK_MUSIC_TRACK_NONE;
    }
}


static
bool
_nk_apply_music_volume(NkRuntime *state_,
                       s32        volume_)
{
  u8    track;

  track = _nk_scene_music_track(state_);
  if(volume_ == 0)
    {
      if(state_->music.initialized)
        {
          nk_music_shutdown(&state_->music);
        }

      kprintf("NK2 music disabled skip track=%u\n",
              (unsigned int)track);
      return true;
    }

  if(track == NK_MUSIC_TRACK_NONE)
    {
      if(!state_->music.initialized)
        {
          return true;
        }

      if(!nk_music_set_volume(&state_->music, volume_))
        {
          return false;
        }

      return nk_music_stop(&state_->music);
    }

  if(!state_->music.initialized)
    {
      if(!nk_music_initialize(&state_->music, &state_->sound, volume_))
        {
          return false;
        }
    }
  else if(!nk_music_set_volume(&state_->music, volume_))
    {
      return false;
    }

  return nk_music_play(&state_->music, track);
}


static
bool
_nk_start_scene_music(NkRuntime *state_)
{
  u8    track;

  track = _nk_scene_music_track(state_);
  if(!_nk_apply_music_volume(
                             state_,
                             state_->flow.options.music_volume))
    {
      kprintf("NK2 music start failed track=%u scene=%s\n",
              (unsigned int)track,
              _nk_scene_name(state_->flow.scene));
      return false;
    }

  return true;
}


static
bool
_nk_change_scene(NkRuntime *state_)
{
  Err error;

  /*
   * The DOS screens install fadelevel 0 with UpdatePalette(), which waits
   * for VSync, before they release the old screen and load the next one.
   * The 3DO can consume two 100 Hz fade ticks in one 60 Hz display frame;
   * without an explicit presentation here, a dim nonzero frame can remain
   * visible for the whole blocking asset load.  Put a black back buffer on
   * screen and let one field latch it before touching either scene's files.
   */
  _nk_clear_screen(state_);
  if(state_->fatal_error)
    {
      return false;
    }

  if(!_nk_flush_cels(state_))
    {
      return false;
    }

  error = DisplayScreen(
                        state_->display.sc_Screens[state_->display.sc_curScreen],
                        0
                        );
  if(error < 0)
    {
      _nk_platform_error("DisplayScreen(scene blackout)", error);
      state_->fatal_error = 1U;
      return false;
    }

  error = WaitVBL(state_->vbl_request, 1);
  if(error < 0)
    {
      _nk_platform_error("WaitVBL(scene blackout)", error);
      state_->fatal_error = 1U;
      return false;
    }

  state_->display.sc_curScreen ^= 1;

  if((state_->music.initialized) &&
     (!nk_music_stop(&state_->music)))
    {
      kprintf("NK2 music stop failed during scene change\n");
      state_->fatal_error = 1U;
      return false;
    }

  if(!_nk_enter_scene(state_))
    {
      kprintf("NK2 scene enter failed scene=%s\n",
              _nk_scene_name(state_->flow.scene));
      state_->fatal_error = 1U;
      return false;
    }

  if(!_nk_start_scene_music(state_))
    {
      state_->fatal_error = 1U;
      return false;
    }

  /*
   * The DOS screens install their 100 Hz timer handler only after all VOL
   * loading and scene initialization are complete.  Do not turn blocking
   * CD reads into simulation time on the first 3DO frame of a new scene.
   * SetTimerSpeed() also clears the source timer's fractional accumulator.
   */
  state_->simulation_clock.accumulator = 0U;
  state_->last_audio_time = (u32)GetAudioTime();
  return state_->flow.scene != NK_SCENE_EXIT;
}


static
bool
_nk_cinematic_input_active(const nk_input_sample input_[NK_PLAYER_COUNT])
{
  u32    index;

  for(index = 0U; index < NK_PLAYER_COUNT; ++index)
    {
      if(((input_[index].stat & NK_DIR_MASK) != 0U) ||
         ((input_[index].buttons & NK_BUTTON_MASK) != 0U) ||
         ((input_[index].commands &
           (NK_COMMAND_CANCEL | NK_COMMAND_PAUSE)) != 0U))
        {
          return true;
        }
    }

  return false;
}


static
bool
_nk_tick_cinema(NkRuntime            *state_,
                const nk_input_sample input_[NK_PLAYER_COUNT])
{
  int skip_requested;

  nk_cinema_tick(&state_->cinema);
  if((state_->cinema.pending_sound >= 0) &&
     (state_->cinema.pending_sound < NK_CINEMA_SOUND_COUNT))
    {
      nk_audio_play_item(
                         &state_->sound,
                         state_->scene_samples[state_->cinema.pending_sound]
                         );
    }

  skip_requested = _nk_cinematic_input_active(input_);
  if(skip_requested)
    {
      kprintf("NK2 cinema skipped scene=%s\n",
              _nk_scene_name(state_->flow.scene));
      state_->cinematic_input_latched = 1U;
      state_->cinema.completed = 1U;
    }

  if(state_->cinema.completed)
    {
      if(skip_requested)
        {
          nk_flow_cinematic_skip(&state_->flow);
        }
      else
        {
          nk_flow_cinematic_complete(&state_->flow);
        }

      return _nk_change_scene(state_);
    }

  return true;
}


static
bool
_nk_tick_port_credit(NkRuntime            *state_,
                     const nk_input_sample input_[NK_PLAYER_COUNT])
{
  int skip_requested;

  nk_port_credit_tick(&state_->port_credit);
  skip_requested = _nk_cinematic_input_active(input_);
  if(skip_requested)
    {
      kprintf("NK2 cinema skipped scene=port-credit\n");
      state_->cinematic_input_latched = 1U;
      nk_port_credit_skip(&state_->port_credit);
    }

  if(!state_->port_credit.completed)
    {
      return true;
    }

  if(skip_requested)
    {
      nk_flow_cinematic_skip(&state_->flow);
    }
  else
    {
      nk_flow_cinematic_complete(&state_->flow);
    }

  return _nk_change_scene(state_);
}


static
bool
_nk_tick_title(NkRuntime            *state_,
               const nk_input_sample input_[NK_PLAYER_COUNT])
{
  nk_ui_events events;
  int result;
  int voice_playing;

  (void)input_[1];
  voice_playing = nk_audio_voice_playing(
                                         &state_->sound,
                                         state_->scene_voice
                                         );
  if(!voice_playing)
    {
      state_->scene_voice = -1;
    }

  nk_title_step(
                &state_->title,
                &input_[0],
                state_->controller_active,
                true,
                voice_playing,
                &events
                );
  if(((events.flags & NK_UI_EVENT_VOICE) != 0U) &&
     (events.voice_index >= 0) && (events.voice_index < NK_TITLE_VOICE_COUNT))
    {
      if(((events.flags & NK_UI_EVENT_MOVE) != 0U) &&
         (state_->scene_voice >= 0))
        {
          nk_audio_release_voice(
                                 &state_->sound,
                                 state_->scene_voice
                                 );
          state_->scene_voice = -1;
        }

      state_->scene_voice = nk_audio_play_item(
                                               &state_->sound,
                                               state_->scene_samples[(int)events.voice_index]
                                               );
    }

  nk_scene_tick(&state_->scene);
  result = nk_title_result(&state_->title);
  if(result != NK_TITLE_PENDING)
    {
      kprintf("NK2 title selected=%d\n", result);
      nk_flow_title_complete(&state_->flow, result);
      if(result == NK_TITLE_ATTRACT)
        {
          kprintf("NK2 attract start\n");
        }

      return _nk_change_scene(state_);
    }

  return true;
}


static
void
_nk_log_option_adjustment(const nk_options_state *options_)
{
  switch(options_->cursor)
    {
    case NK_OPTION_DIFFICULTY:
      kprintf("NK2 option difficulty=%ld\n", (long)options_->pending.difficulty);
      break;
    case NK_OPTION_SOUND:
      kprintf("NK2 option sound=%ld\n", (long)options_->pending.sound_volume);
      break;
    case NK_OPTION_MUSIC:
      kprintf("NK2 option music=%ld\n", (long)options_->pending.music_volume);
      break;
    case NK_OPTION_TALKING:
      kprintf("NK2 option talking=%ld\n", (long)options_->pending.talking);
      break;
    case NK_OPTION_ROUND:
      kprintf("NK2 option rounds=%ld\n", (long)options_->pending.round_setting);
      break;
    case NK_OPTION_FIX_ORIG_BUGS:
      kprintf(
              "NK2 option fix_orig_bugs=%ld\n",
              (long)options_->pending.fix_orig_bugs
              );
      break;
    default:
      break;
    }
}


static
bool
_nk_tick_options(NkRuntime            *state_,
                 const nk_input_sample input_[NK_PLAYER_COUNT])
{
  nk_ui_events events;
  int step;

  (void)input_[1];
  nk_options_step(&state_->options, &input_[0], &events);
  if((events.flags & NK_UI_EVENT_MOVE) != 0U)
    {
      kprintf("NK2 option cursor=%u\n", (unsigned int)state_->options.cursor);
    }

  if((events.flags & NK_UI_EVENT_ADJUST) != 0U)
    {
      _nk_log_option_adjustment(&state_->options);
    }

  if(((events.flags & NK_UI_EVENT_ADJUST) != 0U) &&
     (state_->options.cursor == NK_OPTION_SOUND) &&
     (!nk_audio_set_volume(
                           &state_->sound,
                           state_->options.pending.sound_volume
                           )))
    {
      kprintf("NK2 option sound update failed\n");
      return false;
    }

  if(((events.flags & NK_UI_EVENT_ADJUST) != 0U) &&
     (state_->options.cursor == NK_OPTION_MUSIC) &&
     (!_nk_apply_music_volume(
                              state_,
                              state_->options.pending.music_volume
                              )))
    {
      kprintf("NK2 option music update failed\n");
      return false;
    }

  if(events.menu_sound != NK_MENU_SOUND_NONE)
    {
      nk_audio_play_item(
                         &state_->sound,
                         state_->scene_samples[events.menu_sound]
                         );
    }

  step = nk_anim_cursor_tick(&state_->option_skull);
  if(step == NK_ANIM_STEP_COMPLETE)
    {
      nk_anim_cursor_start(&state_->option_skull, 9U, 27U);
    }

  state_->scene_scroll++;
  nk_scene_tick(&state_->scene);
  if(state_->options.done)
    {
      kprintf("NK2 options selected difficulty=%ld sound=%ld music=%ld\n",
              (long)state_->options.pending.difficulty,
              (long)state_->options.pending.sound_volume,
              (long)state_->options.pending.music_volume);
      kprintf("NK2 options selected talking=%ld rounds=%ld fix_orig_bugs=%ld\n",
              (long)state_->options.pending.talking,
              (long)state_->options.pending.round_setting,
              (long)state_->options.pending.fix_orig_bugs);
      nk_flow_options_complete(&state_->flow, &state_->options.pending);
      if(!nk_storage_save_options(&state_->flow.options))
        {
          kprintf("NK2 options save to NVRAM failed\n");
        }

      return _nk_change_scene(state_);
    }

  return true;
}


static
bool
_nk_tick_select(NkRuntime            *state_,
                const nk_input_sample input_[NK_PLAYER_COUNT])
{
  NkSelectEvents events;
  u8    character;
  int result;
  int index;
  int scream_playing;
  const nk_input_sample *selection_input;

  scream_playing = nk_audio_voice_playing(
                                          &state_->sound,
                                          state_->select_scream_voice
                                          );
  if((!scream_playing) && (state_->select_scream_voice >= 0))
    {
      _nk_unload_select_scream(state_);
    }

  selection_input = input_;
  nk_select_step(
                 &state_->selection,
                 selection_input,
                 &state_->flow_rng,
                 scream_playing,
                 &events
                 );
  if((events.scream_character >= 0) && (events.scream_index >= 0))
    {
      _nk_play_select_scream(
                             state_,
                             events.scream_character,
                             events.scream_index
                             );
    }

  for(index = 0; index < NK_PLAYER_COUNT; ++index)
    {
      if((events.moved_mask & (1U << index)) != 0U)
        {
          nk_audio_play_item(&state_->sound, state_->scene_samples[NK_MENU_SOUND_ADJUST]);
          character = nk_cursor_to_character(state_->selection.cursor[index]);
          kprintf("NK2 option player%d-character=%u %s\n",
                  index + 1,
                  (unsigned int)character,
                  nk_character_name(character));
        }

      if(((events.locked_mask & (1U << index)) != 0U) &&
         (state_->selection.control[index] == NK_CONTROL_HUMAN))
        {
          nk_audio_play_item(&state_->sound, state_->scene_samples[NK_MENU_SOUND_MOVE]);
          character = nk_cursor_to_character(state_->selection.cursor[index]);
          kprintf("NK2 option player%d-character selected=%u %s\n",
                  index + 1,
                  (unsigned int)character,
                  nk_character_name(character));
        }
    }

  state_->scene_scroll++;
  if(state_->scene_scroll > NK_SCREEN_WIDTH)
    {
      state_->scene_scroll = 0;
    }

  nk_scene_tick(&state_->scene);
  result = nk_select_result(&state_->selection);
  if(result == NK_SELECT_CANCELLED)
    {
      nk_flow_abort_to_title(&state_->flow);
      return _nk_change_scene(state_);
    }

  if(result == NK_SELECT_COMPLETE)
    {
      /*
       * Selector lock freezes previous_stat.  Carry its direction nibble
       * across this boundary so the first GAME painter sees it.  The raw
       * button filters persist across every source screen.
       */
      state_->select_match_input_carry = 1U;
      nk_flow_selection_complete(
                                 &state_->flow,
                                 state_->selection.cursor[0],
                                 state_->selection.cursor[1]
                                 );
      return _nk_change_scene(state_);
    }

  return true;
}


static
bool
_nk_update_paused_sound_volume(NkRuntime *state_,
                               u8         stat_)
{
  s32    volume;

  stat_ &= (u8)(NK_DIR_UP | NK_DIR_DOWN);
  if(state_->pause_volume_stat == stat_)
    {
      return true;
    }

  state_->pause_volume_stat = stat_;
  volume = state_->flow.options.sound_volume;
  if(((stat_ & NK_DIR_UP) != 0U) && (volume < NK_VOLUME_MAX))
    {
      volume++;
    }
  else if(((stat_ & NK_DIR_DOWN) != 0U) && (volume > NK_VOLUME_MIN))
    {
      volume--;
    }
  else
    {
      return true;
    }

  if(!nk_audio_set_volume(&state_->sound, volume))
    {
      return false;
    }

  state_->flow.options.sound_volume = volume;
  if((state_->pause_adjust_sample >= 0) &&
     (nk_audio_play_item(
                         &state_->sound,
                         state_->pause_adjust_sample
                         ) < 0))
    {
      /*
       * Combat follows a first-free seven-voice drop policy, so every voice
       * may be occupied when pause is entered.  Paused menu feedback takes
       * priority over stale gameplay sounds.
       */
      nk_audio_stop_all(&state_->sound);
      (void)nk_audio_play_item(
                               &state_->sound,
                               state_->pause_adjust_sample
                               );
    }

  if(!nk_storage_save_options(&state_->flow.options))
    {
      kprintf("NK2 paused sound volume save failed\n");
    }

  kprintf("NK2 paused sound volume=%ld\n", (long)volume);
  return true;
}


static
bool
_nk_tick_match(NkRuntime            *state_,
               const nk_input_sample input_[NK_PLAYER_COUNT],
               int                   materialize_presentation_)
{
  nk_input_sample player_zero_input;
  int pause_pressed;
  int continue_pressed;

  player_zero_input.stat = input_[0].stat;
  player_zero_input.buttons = input_[0].buttons;
  player_zero_input.attack_code = input_[0].attack_code;
  player_zero_input.commands = input_[0].commands;

  if(((player_zero_input.commands & NK_COMMAND_CANCEL) != 0U) ||
     ((input_[1].commands & NK_COMMAND_CANCEL) != 0U))
    {
      if((state_->match_exit == 0) && (!state_->cancel_pending))
        {
          /*
           * ESC marks the current logical snapshot as quitting.  Match
           * preparation arms fadeout after that snapshot is complete and
           * does not clear the independent pause flag.
           */
          state_->cancel_pending = 1U;
          state_->outcome_continue_pending = 0U;
          kprintf("NK2 match exit=cancel\n");
        }
    }

  pause_pressed = ((player_zero_input.commands | input_[1].commands)
                   & NK_COMMAND_PAUSE) != 0U;
  if((pause_pressed) && (!state_->pause_down))
    {
      state_->paused ^= 1U;
      kprintf("NK2 match paused=%u\n", (unsigned int)state_->paused);
      if((state_->music.initialized) &&
         (!nk_music_set_paused(&state_->music, state_->paused)))
        {
          kprintf("NK2 paused music update failed\n");
          return false;
        }
    }

  state_->pause_down = (u8)pause_pressed;
  if(state_->paused)
    {
      if(!_nk_update_paused_sound_volume(
                                         state_,
                                         (u8)(player_zero_input.stat | input_[1].stat)))
        {
          kprintf("NK2 paused sound volume update failed\n");
          return false;
        }
    }
  else
    {
      state_->pause_volume_stat = 0U;
    }

  continue_pressed =
    (player_zero_input.buttons | input_[1].buttons) != 0U;
  if((state_->flow.mode == NK_MODE_ATTRACT) &&
     (state_->outcome_started) &&
     (!state_->outcome_continue_pending) &&
     ((!state_->flow.options.talking) ||
      (state_->dialogue.terminal)))
    {
      state_->outcome_continue_pending = 1U;
    }

  if((state_->game.game_state == NK_GAME_STATE_MATCH_COMPLETE) &&
     (continue_pressed) &&
     (state_->match_exit == 0) &&
     (!state_->cancel_pending) &&
     (!state_->outcome_continue_pending))
    {
      /*
       * PlayGame accepts the held outcome button even while TickUpdate is
       * paused.  The logical presentation snapshot is completed before
       * quit/fade is committed, and the fade then stalls until unpause.
       */
      state_->outcome_continue_pending = 1U;
    }

  if(state_->paused)
    {
      return _nk_prepare_match_tick(
                                    state_,
                                    materialize_presentation_
                                    );
    }

  /*
   * TickUpdate freezes uu while paused, but advances it when DrawText is
   * disabled by TALKING. Dialogue parsing follows that same 100 Hz logical
   * clock.
   */
  nk_dialogue_advance_clock(&state_->dialogue);
  nk_game_set_input(&state_->game, 0U, &player_zero_input);
  nk_game_set_input(&state_->game, 1U, &input_[1]);
  nk_game_step(&state_->game);
  nk_audio_play_events(&state_->sound, &state_->game);
  nk_scene_tick(&state_->scene);
  state_->scene_scroll++;
  if(state_->scene_scroll > NK_SCREEN_WIDTH)
    {
      state_->scene_scroll = 0;
    }

  if((state_->cancel_pending) ||
     (state_->outcome_continue_pending))
    {
      /* Match preparation commits this transition after the current tick. */
    }
  else if(state_->match_fade_out)
    {
      if(state_->match_fade_level > 0U)
        {
          state_->match_fade_level--;
        }
      else if(state_->match_exit == 1)
        {
          state_->flow_rng = state_->game.rng;
          nk_flow_abort_to_title(&state_->flow);
          return _nk_change_scene(state_);
        }
      else if(state_->match_exit == 2)
        {
          state_->flow_rng = state_->game.rng;
          kprintf("NK2 match complete winner=player%ld character=%u %s\n",
                  (long)state_->game.winner + 1L,
                  (unsigned int)state_->game.players[
                                                     state_->game.winner
                                                     ].character_type,
                  nk_character_name(
                                    state_->game.players[state_->game.winner].character_type
                                    ));
          nk_flow_match_complete(&state_->flow, state_->game.winner);
          return _nk_change_scene(state_);
        }
    }
  else if(state_->match_fade_level < NK_UI_FADE_LEVEL_MAX)
    {
      state_->match_fade_level++;
    }

  return _nk_prepare_match_tick(
                                state_,
                                materialize_presentation_
                                );
}


static
bool
_nk_tick_scene(NkRuntime            *state_,
               const nk_input_sample input_[NK_PLAYER_COUNT],
               int                   materialize_presentation_)
{
  if(state_->cinematic_input_latched)
    {
      if(_nk_cinematic_input_active(input_))
        {
          return true;
        }

      state_->cinematic_input_latched = 0U;
    }

  if((state_->flow.mode == NK_MODE_ATTRACT) &&
     ((state_->flow.scene == NK_SCENE_SELECT) ||
      (state_->flow.scene == NK_SCENE_MATCH)) &&
     (state_->controller_active))
    {
      kprintf("NK2 attract exit=input\n");
      state_->attract_input_latched = 1U;
      nk_flow_abort_to_title(&state_->flow);
      return _nk_change_scene(state_);
    }

  switch(state_->flow.scene)
    {
    case NK_SCENE_LOGO:
    case NK_SCENE_CREDITS:
    case NK_SCENE_ENDING:
      return _nk_tick_cinema(state_, input_);
    case NK_SCENE_PORT_CREDIT:
      return _nk_tick_port_credit(state_, input_);
    case NK_SCENE_TITLE:
      return _nk_tick_title(state_, input_);
    case NK_SCENE_OPTIONS:
      return _nk_tick_options(state_, input_);
    case NK_SCENE_SELECT:
      return _nk_tick_select(state_, input_);
    case NK_SCENE_MATCH:
      return _nk_tick_match(
                            state_,
                            input_,
                            materialize_presentation_
                            );
    default:
      return false;
    }
}


static
void
_nk_cleanup(NkRuntime *state_)
{
  state_->render_ccb_count = 0U;
  if(state_->render_ccbs != NULL)
    {
      FreeMem(
              state_->render_ccbs,
              (int32)(sizeof(CCB) * NK_RENDER_CCB_LIMIT)
              );
      state_->render_ccbs = NULL;
    }

  nk_sticky_target_shutdown(&state_->sticky_target);
  _nk_leave_scene(state_);
  _nk_unload_cold_bundles(&state_->assets);
  _nk_unload_select_resident_assets(&state_->assets);
  _nk_unload_gameplay_assets(&state_->assets);
  _nk_unload_font_assets(&state_->assets);
  nk_music_shutdown(&state_->music);
  nk_audio_shutdown(&state_->sound);
  if(state_->vbl_request >= 0)
    {
      DeleteVBLIOReq(state_->vbl_request);
      state_->vbl_request = -1;
    }

  if(state_->vram_ioreq >= 0)
    {
      DeleteVRAMIOReq(state_->vram_ioreq);
      state_->vram_ioreq = -1;
    }

  if(state_->audio_open)
    {
      CloseAudioFolio();
      state_->audio_open = 0U;
    }

  if(state_->display_open)
    {
      DeleteBasicDisplay(&state_->display);
      state_->display_open = 0U;
    }

  if(state_->control_pad_open)
    {
      KillControlPad();
      state_->control_pad_open = 0U;
    }
}


static
bool
_nk_initialize(NkRuntime *state_)
{
  Err error;

  memset(state_, 0, sizeof(*state_));
  state_->vbl_request = -1;
  state_->vram_ioreq = -1;
  state_->music.thread = -1;
  state_->display_interpolation_mode = NK_DISPLAY_INTERPOLATION_OFF;
  state_->assets.scene_bundle_index = NK_ASSET_BUNDLE_COUNT;
  _nk_reset_scene_samples(state_);
  nk_flow_init(&state_->flow);
  (void)nk_storage_load_options(&state_->flow.options);
  nk_dialogue_init(&state_->dialogue);
  /*
   * The DOS release seeds Watcom rand() from the live BIOS tick counter.
   * The 3DO release follows that behavior with live hardware randomness.
   */
  nk_rng_seed(
              &state_->flow_rng,
              (u32)ReadHardwareRandomNumber()
              );
  nk_input_filter_reset(&state_->input_filter[0]);
  nk_input_filter_reset(&state_->input_filter[1]);
  nk_aspect_init(&state_->aspect);
  _nk_initialize_render_y_lookup(state_);
  nk_fade_configure(32U, &state_->render_fade_config);
  if(!nk_sticky_data_valid())
    {
      kprintf("NK2 sticky animation catalog is invalid\n");
      return false;
    }

  error = InitControlPad(2);
  if(error < 0)
    {
      _nk_platform_error("InitControlPad", error);
      return false;
    }

  state_->control_pad_open = 1U;
  error = CreateBasicDisplay(&state_->display, DI_TYPE_DEFAULT, 2);
  if(error < 0)
    {
      _nk_platform_error("CreateBasicDisplay", error);
      _nk_cleanup(state_);
      return false;
    }

  state_->display_open = 1U;
  state_->display.sc_curScreen = 0;
  kprintf("NK2 display type=%u size=%ux%u\n",
          (unsigned int)state_->display.sc_DisplayType,
          (unsigned int)state_->display.sc_BitmapWidth,
          (unsigned int)state_->display.sc_BitmapHeight);
  error = _nk_set_display_interpolation(
                                        &state_->display,
                                        state_->display_interpolation_mode
                                        );
  if(error < 0)
    {
      _nk_platform_error("Disable display interpolation", error);
      _nk_cleanup(state_);
      return false;
    }

  kprintf("NK2 display interpolation=off\n");
  state_->render_ccbs = (CCB *)AllocMem(
                                        (int32)(sizeof(CCB) * NK_RENDER_CCB_LIMIT),
                                        MEMTYPE_DRAM | MEMTYPE_FILL
                                        );
  if(state_->render_ccbs == NULL)
    {
      kprintf("NK2 render CCB pool allocation failed\n");
      _nk_cleanup(state_);
      return false;
    }

  _nk_link_render_ccb_pool(state_);
  if(!_nk_initialize_solid_fill_templates(state_))
    {
      kprintf("NK2 solid-fill CEL template initialization failed\n");
      _nk_cleanup(state_);
      return false;
    }

  state_->vram_ioreq = CreateVRAMIOReq();
  if(state_->vram_ioreq < 0)
    {
      _nk_platform_error("CreateVRAMIOReq", state_->vram_ioreq);
      _nk_cleanup(state_);
      return false;
    }

  if(!nk_sticky_target_init(
                            &state_->sticky_target,
                            state_->vram_ioreq))
    {
      kprintf("NK2 sticky floor surface initialization failed\n");
      _nk_cleanup(state_);
      return false;
    }

  if(!_nk_prepare_cel_template(
                               nk_sticky_target_surface_cel(&state_->sticky_target),
                               &state_->sticky_surface_template))
    {
      kprintf("NK2 sticky floor CEL template preparation failed\n");
      _nk_cleanup(state_);
      return false;
    }

  state_->vbl_request = CreateVBLIOReq();
  if(state_->vbl_request < 0)
    {
      _nk_platform_error("CreateVBLIOReq", state_->vbl_request);
      _nk_cleanup(state_);
      return false;
    }

  error = OpenAudioFolio();
  if(error < 0)
    {
      _nk_platform_error("OpenAudioFolio", error);
      _nk_cleanup(state_);
      return false;
    }

  state_->audio_open = 1U;
  if(!nk_audio_initialize(&state_->sound))
    {
      kprintf("NK2 audio mixer initialization failed\n");
      _nk_cleanup(state_);
      return false;
    }

  if(!nk_tick_clock_init(
                         &state_->simulation_clock,
                         nk_audio_clock_rate_f16(&state_->sound),
                         NK_AUDIO_RATE_F16_ONE))
    {
      kprintf("NK2 audio-paced simulation clock initialization failed\n");
      _nk_cleanup(state_);
      return false;
    }

  if(!nk_audio_set_volume(
                          &state_->sound,
                          state_->flow.options.sound_volume
                          ))
    {
      kprintf("NK2 initial audio volume update failed\n");
      _nk_cleanup(state_);
      return false;
    }

  if(!_nk_load_font_assets(&state_->assets))
    {
      kprintf("NK2 resident font asset load failed\n");
      _nk_cleanup(state_);
      return false;
    }

  if(!_nk_enter_scene(state_))
    {
      kprintf("NK2 opening cinema load failed\n");
      _nk_cleanup(state_);
      return false;
    }

  if(!_nk_start_scene_music(state_))
    {
      _nk_cleanup(state_);
      return false;
    }

  state_->last_audio_time = (u32)GetAudioTime();
  return true;
}


int
main(int    argc_,
     char **argv_)
{
  nk_input_sample input[NK_PLAYER_COUNT];
  u32    audio_now;
  u32    audio_elapsed;
  u32    simulation_steps;
  u32    scene_generation;
  NkAspectMode previous_aspect_mode;
  u32 pad[2];
  Err error;
  int running;

  (void)argc_;
  (void)argv_;
  kprintf("Noggin Knockers 2 3DO port v%s (original v%s)\n",
          NK_PORT_VERSION,
          NK_ORIGINAL_VERSION);
  if(!_nk_initialize(&g_RUNTIME))
    {
      return 1;
    }

  running = true;
  while(running)
    {
      /*
       * DisplayScreen() does not latch the queued screen until this VBL.
       * Wait before reusing sc_curScreen so CEL rendering never targets the
       * buffer that is still being scanned out.
       */
      error = WaitVBL(g_RUNTIME.vbl_request, 1);
      if(error < 0)
        {
          _nk_platform_error("WaitVBL", error);
          break;
        }

      if((g_RUNTIME.music.initialized) &&
         (!nk_music_service(&g_RUNTIME.music)))
        {
          kprintf("NK2 music streaming failed\n");
          g_RUNTIME.fatal_error = 1U;
          break;
        }

      pad[0] = 0U;
      pad[1] = 0U;
      error = DoControlPad(1, &pad[0], NK_PAD_CONTINUOUS);
      if(error < 0)
        {
          _nk_platform_error("DoControlPad(1)", error);
          break;
        }

      error = DoControlPad(2, &pad[1], NK_PAD_CONTINUOUS);
      if(error < 0)
        {
          pad[1] = 0U;
        }

      _nk_make_input(&g_RUNTIME.input_filter[0], pad[0], &input[0]);
      _nk_make_input(&g_RUNTIME.input_filter[1], pad[1], &input[1]);
      g_RUNTIME.controller_active = (u8)(
                                         ((pad[0] | pad[1]) & NK_PAD_ATTRACT_ACTIVITY) != 0U
                                         );
      if(g_RUNTIME.attract_input_latched)
        {
          input[0].stat = 0U;
          input[0].buttons = 0U;
          input[0].attack_code = NK_ATTACK_CODE_NONE;
          input[0].commands = 0U;
          input[1].stat = 0U;
          input[1].buttons = 0U;
          input[1].attack_code = NK_ATTACK_CODE_NONE;
          input[1].commands = 0U;
          if(!g_RUNTIME.controller_active)
            {
              g_RUNTIME.attract_input_latched = 0U;
            }
        }

      error = _nk_update_display_interpolation(
                                               &g_RUNTIME,
                                               (u8)(input[0].commands | input[1].commands)
                                               );
      if(error < 0)
        {
          _nk_platform_error("Update display interpolation", error);
          g_RUNTIME.fatal_error = 1U;
          break;
        }

      previous_aspect_mode = g_RUNTIME.aspect.mode;
      nk_aspect_update(
                       &g_RUNTIME.aspect,
                       (input[0].commands & NK_COMMAND_ASPECT_TOGGLE) != 0U
                       );
      if(g_RUNTIME.aspect.mode != previous_aspect_mode)
        {
          kprintf("NK2 aspect mode=%s\n",
                  g_RUNTIME.aspect.mode == NK_ASPECT_CORRECT ? "correct" : "raw");
        }

      g_RUNTIME.active_effect_draw_variants =
        g_EFFECT_DRAW_VARIANTS[(u32)g_RUNTIME.aspect.mode];
      audio_now = (u32)GetAudioTime();
      audio_elapsed = audio_now - g_RUNTIME.last_audio_time;
      g_RUNTIME.last_audio_time = audio_now;
      simulation_steps = nk_tick_clock_advance(
                                               &g_RUNTIME.simulation_clock,
                                               audio_elapsed
                                               );
      while(simulation_steps > 0U)
        {
          scene_generation = g_RUNTIME.scene_generation;
          running = _nk_tick_scene(
                                   &g_RUNTIME,
                                   input,
                                   simulation_steps == 1U
                                   );
          nk_input_sample_consume_edges(&input[0]);
          nk_input_sample_consume_edges(&input[1]);
          if((!running) ||
             (g_RUNTIME.scene_generation != scene_generation))
            {
              break;
            }

          simulation_steps--;
        }

      _nk_render(&g_RUNTIME);
      if(g_RUNTIME.fatal_error)
        {
          running = false;
          break;
        }

      if(!_nk_flush_cels(&g_RUNTIME))
        {
          running = false;
          break;
        }

      /*
       * The current visible-frame CEL chain has now sampled the old floor.
       * Burn this presentation's sticky operations into the offscreen floor
       * only afterward, matching PutStickyImage's next-presentation delay.
       */
      if((g_RUNTIME.flow.scene == NK_SCENE_MATCH) &&
         (g_RUNTIME.match_presentation.prepared) &&
         (!nk_sticky_target_commit(
                                   &g_RUNTIME.sticky_target,
                                   &g_RUNTIME.match_presentation.sticky_pending,
                                   _nk_3do_sticky_image,
                                   &g_RUNTIME)))
        {
          kprintf("NK2 sticky floor commit failed\n");
          g_RUNTIME.fatal_error = 1U;
          running = false;
          break;
        }

      if((g_RUNTIME.flow.scene == NK_SCENE_MATCH) &&
         (g_RUNTIME.match_presentation.prepared))
        {
          nk_sticky_queue_reset(
                                &g_RUNTIME.match_presentation.sticky_pending
                                );
        }

      error = DisplayScreen(
                            g_RUNTIME.display.sc_Screens[g_RUNTIME.display.sc_curScreen],
                            0
                            );
      if(error < 0)
        {
          _nk_platform_error("DisplayScreen(frame)", error);
          g_RUNTIME.fatal_error = 1U;
          running = false;
          break;
        }

      if((g_RUNTIME.flow.scene == NK_SCENE_MATCH) && (!g_RUNTIME.paused))
        {
          /*
           * The next loop's WaitVBL gates reuse of this buffer, so this queued
           * phase remains visible for one complete NTSC field.
           */
          g_RUNTIME.match_shock_phase++;
          if(g_RUNTIME.match_shock_phase >=
             NK_MATCH_SHOCK_PERIOD_PRESENTATIONS)
            {
              g_RUNTIME.match_shock_phase = 0U;
            }
        }

      g_RUNTIME.display.sc_curScreen ^= 1;
    }

  _nk_cleanup(&g_RUNTIME);
  return g_RUNTIME.fatal_error ? 1 : 0;
}
