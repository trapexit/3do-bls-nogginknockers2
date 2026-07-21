#pragma once

#include "nk_types.h"

#define NK_SCENE_BANK_COUNT (3)
#define NK_SCENE_SERIES_LIMIT (40)

#define NK_SCENE_BANK_TITLE (0U)
#define NK_SCENE_BANK_GAME (1U)
#define NK_SCENE_BANK_SELECT (2U)

typedef struct NkSceneFrame
{
  s16    x;
  s16    y;
  u16    duration_100hz;
  u8    image;
} NkSceneFrame;

typedef struct NkSceneSeries
{
  u16    first_frame;
  u8    frame_count;
  u8    parameter;
} NkSceneSeries;

typedef struct NkSceneBank
{
  char name[12];
  u16    first_series;
  u8    series_count;
  u8    image_count;
  u16    first_frame;
  u16    frame_count;
  u16    layer2_height;
} NkSceneBank;

typedef struct NkSceneSeriesState
{
  u8    current_frame;
  s32    remaining;
} NkSceneSeriesState;

typedef struct NkSceneState
{
  u8    bank_index;
  u8    valid;
  NkSceneSeriesState series[NK_SCENE_SERIES_LIMIT];
} NkSceneState;

extern const NkSceneFrame nk_scene_frames[];
extern const NkSceneSeries nk_scene_series[];
extern const NkSceneBank nk_scene_banks[];
extern const u16    nk_scene_frame_count;
extern const u16    nk_scene_series_count;

const
NkSceneBank *
nk_scene_bank(u8    bank_index_);
const
NkSceneSeries *
nk_scene_series_def(u8    bank_index_,
                    u8    series_index_);
const
NkSceneFrame *
nk_scene_series_frame(const NkSceneState *state_,
                      u8                  series_index_);
bool
nk_scene_begin(NkSceneState *state_,
               u8            bank_index_);
void
nk_scene_tick(NkSceneState *state_);
bool
nk_scene_series_range_valid(const NkSceneBank   *bank_,
                            const NkSceneSeries *series_);
bool
nk_scene_data_valid(void);
