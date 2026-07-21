#pragma once

#include "nk_types.h"

#define NK_CINEMA_BANK_COUNT (10)
#define NK_CINEMA_SPRITES_PER_BANK (50)
#define NK_CINEMA_TEXTS_PER_BANK (25)
#define NK_CINEMA_ACTIVE_SPRITE_COUNT (15)
#define NK_CINEMA_NO_INDEX (0xffffU)

#define NK_CINEMA_SOUND_COUNT (26)

#define NK_CINEMA_EVENT_DELAY          (0U)
#define NK_CINEMA_EVENT_FADE           (1U)
#define NK_CINEMA_EVENT_BACKGROUND     (2U)
#define NK_CINEMA_EVENT_START_SPRITE   (3U)
#define NK_CINEMA_EVENT_RELEASE_SPRITE (4U)
#define NK_CINEMA_EVENT_PLAY_SOUND     (5U)
#define NK_CINEMA_EVENT_SHOW_TEXT      (6U)

#define NK_CINEMA_SPRITE_INDEX_MASK (0x7fU)
#define NK_CINEMA_SPRITE_LOOP_FLAG  (0x80U)

#define NK_CINEMA_LOGO (0U)
#define NK_CINEMA_CREDITS (1U)
#define NK_CINEMA_ENDING_FIRST (2U)

typedef struct NkCinemaEvent
{
  u8    type;
  u8    data;
  s16    x;
  s16    y;
} NkCinemaEvent;

typedef struct NkCinemaImageRef
{
  u8    image;
  u8    orientation;
  s16    x;
  s16    y;
} NkCinemaImageRef;

typedef struct NkCinemaFrame
{
  u16    first_image;
  u8    image_count;
  u8    duration_100hz;
  s16    delta_x_256;
  s16    delta_y_256;
} NkCinemaFrame;

typedef struct NkCinemaSprite
{
  u16    first_frame;
  u8    frame_count;
  u8    reserved;
} NkCinemaSprite;

typedef struct NkCinemaBank
{
  char name[8];
  u16    first_event;
  u16    event_count;
  u16    first_sprite;
  u16    first_text;
  u8    image_count;
  u8    reserved;
  u32    sound_mask;
} NkCinemaBank;

typedef struct NkCinemaActiveSprite
{
  s32    x;
  s32    y;
  s32    fraction_x;
  s32    fraction_y;
  s32    remaining;
  u8    active;
  u8    sprite_index;
  u8    frame_index;
  u8    loop;
} NkCinemaActiveSprite;

typedef struct NkCinemaState
{
  NkCinemaActiveSprite sprites[NK_CINEMA_ACTIVE_SPRITE_COUNT];
  s32    remaining;
  s32    current_background;
  s32    current_text;
  s32    pending_sound;
  s32    pending_text;
  u32    event_index;
  u8    bank_index;
  u8    fade_level;
  u8    fade_state;
  u8    fade_active;
  u8    valid;
  u8    completed;
} NkCinemaState;

extern const NkCinemaEvent nk_cinema_events[];
extern const NkCinemaImageRef nk_cinema_image_refs[];
extern const NkCinemaFrame nk_cinema_frames[];
extern const NkCinemaSprite nk_cinema_sprites[];
extern const char *const nk_cinema_texts[];
extern const NkCinemaBank nk_cinema_banks[];
extern const u16    nk_cinema_event_count;
extern const u16    nk_cinema_frame_count;
extern const u16    nk_cinema_image_ref_count;

const
NkCinemaBank *
nk_cinema_bank(u8    bank_index_);
const
NkCinemaFrame *
nk_cinema_active_frame(const NkCinemaState *state_,
                       u8                   active_index_);
const
NkCinemaImageRef *
nk_cinema_frame_image(const NkCinemaFrame *frame_,
                      u8                   image_index_);
const
char *
nk_cinema_current_text(const NkCinemaState *state_);
bool
nk_cinema_begin(NkCinemaState *state_,
                u8             bank_index_);
void
nk_cinema_tick(NkCinemaState *state_);
bool
nk_cinema_data_valid(void);
