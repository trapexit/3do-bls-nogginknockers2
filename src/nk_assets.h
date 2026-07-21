#pragma once

#include "nk_types.h"

#include "graphics.h"
#include "types.h"

#define NK_ASSET_BUNDLE_FONTS (0U)
#define NK_ASSET_BUNDLE_MAIN_GAME (1U)
#define NK_ASSET_BUNDLE_SELECT (2U)
#define NK_ASSET_BUNDLE_TITLE (3U)
#define NK_ASSET_BUNDLE_LOGO (4U)
#define NK_ASSET_BUNDLE_CREDITS (5U)
#define NK_ASSET_BUNDLE_ENDING_1 (6U)
#define NK_ASSET_BUNDLE_ENDING_2 (7U)
#define NK_ASSET_BUNDLE_ENDING_3 (8U)
#define NK_ASSET_BUNDLE_ENDING_4 (9U)
#define NK_ASSET_BUNDLE_ENDING_5 (10U)
#define NK_ASSET_BUNDLE_ENDING_6 (11U)
#define NK_ASSET_BUNDLE_ENDING_7 (12U)
#define NK_ASSET_BUNDLE_ENDING_8 (13U)
#define NK_ASSET_BUNDLE_COUNT (14U)
#define NK_ASSET_BUNDLE_CINEMA_FIRST (NK_ASSET_BUNDLE_LOGO)

#define NK_ASSET_TARGET_FIGHTER_0 (0U)
#define NK_ASSET_TARGET_FIGHTER_1 (1U)
#define NK_ASSET_TARGET_FIGHTER_2 (2U)
#define NK_ASSET_TARGET_FIGHTER_3 (3U)
#define NK_ASSET_TARGET_FIGHTER_4 (4U)
#define NK_ASSET_TARGET_FIGHTER_5 (5U)
#define NK_ASSET_TARGET_FIGHTER_6 (6U)
#define NK_ASSET_TARGET_FIGHTER_7 (7U)
#define NK_ASSET_TARGET_FIGHTER_8 (8U)
#define NK_ASSET_TARGET_FIGHTER_9 (9U)
#define NK_ASSET_TARGET_PAIN (10U)
#define NK_ASSET_TARGET_EFFECT (11U)
#define NK_ASSET_TARGET_SELECTOR_MARKS (12U)
#define NK_ASSET_TARGET_SELECTOR_OPTIONS (13U)
#define NK_ASSET_TARGET_SELECTOR_GRAY (14U)
#define NK_ASSET_TARGET_SELECT_SCENE (15U)
#define NK_ASSET_TARGET_SELECT_LAYER1A (16U)
#define NK_ASSET_TARGET_SELECT_LAYER1B (17U)
#define NK_ASSET_TARGET_SELECT_LAYER2 (18U)
#define NK_ASSET_TARGET_SELECT_LAYER3 (19U)
#define NK_ASSET_TARGET_GAME_SCENE (20U)
#define NK_ASSET_TARGET_GAME_LAYER1_COMPOSITE (21U)
#define NK_ASSET_TARGET_GAME_LAYER2 (23U)
#define NK_ASSET_TARGET_GAME_LAYER3 (24U)
#define NK_ASSET_TARGET_FONT_BLUE (25U)
#define NK_ASSET_TARGET_FONT_RED (26U)
#define NK_ASSET_TARGET_FONT_WHITE (27U)
#define NK_ASSET_TARGET_FONT_ICER (28U)
#define NK_ASSET_TARGET_FONT_STUMP (29U)
#define NK_ASSET_TARGET_SCENE (30U)
#define NK_ASSET_TARGET_SCENE_LAYER1A (31U)
#define NK_ASSET_TARGET_SCENE_LAYER1B (32U)
#define NK_ASSET_TARGET_SCENE_LAYER2 (33U)
#define NK_ASSET_TARGET_SCENE_LAYER3 (34U)
#define NK_ASSET_TARGET_CINEMA (35U)
#define NK_ASSET_TARGET_PORT_CREDIT (36U)
#define NK_ASSET_TARGET_COUNT (37U)

typedef u16    NkAssetMapEntry;

#define NK_ASSET_MAP(target, index) \
  ((NkAssetMapEntry)((((u16)(target)) << 8) | (u16)(index)))
#define NK_ASSET_MAP_TARGET(entry) ((u8)((entry) >> 8))
#define NK_ASSET_MAP_INDEX(entry) ((u8)((entry) & 0xffU))

typedef struct NkAssetBundleSpec
{
  const char *path;
  const NkAssetMapEntry *entries;
  u32    count;
} NkAssetBundleSpec;

typedef struct NkAssetBundle
{
  CCB *root;
  u32    count;
  uint32 memory_type;
} NkAssetBundle;

typedef bool (*NkAssetBundleVisitor)(void  *context_,
                                    u32    index_,
                                    CCB   *cel_);

extern const NkAssetBundleSpec
  nk_asset_bundle_specs[NK_ASSET_BUNDLE_COUNT];

bool
nk_asset_bundle_load(NkAssetBundle           *bundle_,
                     const NkAssetBundleSpec *spec_,
                     uint32                   memory_type_,
                     NkAssetBundleVisitor     visitor_,
                     void                    *context_);
bool
nk_asset_bundle_visit(const NkAssetBundle     *bundle_,
                      const NkAssetBundleSpec *spec_,
                      NkAssetBundleVisitor     visitor_,
                      void                    *context_);
void
nk_asset_bundle_unload(NkAssetBundle *bundle_);
