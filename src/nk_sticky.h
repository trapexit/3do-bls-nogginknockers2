#pragma once

#include "nk_effects.h"

/*
 * NOGGIN.CPP accumulates sticky effect images in the match background's
 * 720-byte floor rows.  The game uses a fixed floor scroll and copies only
 * the central 320 pixels into logical rows 160 through 199 of the 320x200
 * painter buffer.  A bounded port therefore needs only that opaque 320x40
 * floor crop, not a history of every mark or a transparent full-screen mask.
 */
#define NK_STICKY_SURFACE_WIDTH (320)
#define NK_STICKY_SURFACE_HEIGHT (40)
#define NK_STICKY_FLOOR_Y (160)
#define NK_STICKY_FLOOR_HEIGHT (40)
#define NK_STICKY_BACKGROUND_LAYER_Y (100)
#define NK_STICKY_BACKGROUND_FLOOR_ROW \
  (NK_STICKY_FLOOR_Y - NK_STICKY_BACKGROUND_LAYER_Y)
#define NK_STICKY_SOURCE_WIDTH (640)
#define NK_STICKY_SOURCE_SCROLL_X (160)
#define NK_STICKY_VISIBLE_CROP_X (160)
#define NK_STICKY_BOTTOM_GATE_MARGIN (10)

/*
 * The generated retail animation catalog contains at most eight images per
 * frame.  One presentation can visit every member of the fixed 80-effect pool
 * once.  Validation rejects data which would violate this compile-time bound.
 */
#define NK_STICKY_IMAGES_PER_EFFECT_LIMIT (8)
#define NK_STICKY_PENDING_LIMIT \
  (NK_EFFECT_POOL_COUNT * NK_STICKY_IMAGES_PER_EFFECT_LIMIT)

typedef struct NkStickyOp
{
  s32    x;
  s32    y;
  u8    image;
  u8    orientation;
} NkStickyOp;

typedef struct NkStickyQueue
{
  NkStickyOp ops[NK_STICKY_PENDING_LIMIT];
  u32    count;
} NkStickyQueue;

void
nk_sticky_queue_reset(NkStickyQueue *queue_);

/*
 * Apply PutStickyImage's source-space rejection rules.  x/y are the
 * projected effect-image coordinates in the DOS 320x200 painter.
 */
bool
nk_sticky_source_accepts(s32    x_,
                         s32    y_,
                         u32    width_,
                         u32    height_);

/*
 * Append every accepted image in the effect's current frame.  Identical
 * operations already awaiting the next presentation are coalesced because
 * painting the same source at the same position is idempotent.  The resulting
 * operations retain painter coordinates; sinks clip them to the fixed
 * 320x200 accumulation surface.
 */
bool
nk_sticky_queue_effect(NkStickyQueue  *queue_,
                       const NkEffect *effect_);

/*
 * Verify the retail sticky moves fit NK_STICKY_PENDING_LIMIT by construction.
 */
bool
nk_sticky_data_valid(void);
