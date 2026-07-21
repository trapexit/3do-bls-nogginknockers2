#pragma once

#include "nk_sticky.h"

#include "celutils.h"
#include "graphics.h"
#include "types.h"

#define NK_STICKY_TARGET_BATCH_LIMIT (32)
#define NK_CEL_HDX_SHIFT (20U)
#define NK_CEL_VDY_SHIFT (16U)

typedef CCB *(*NkStickyTargetResolve)(void *context_,
                                      u8    image_index_);

typedef struct NkStickyTarget
{
  CCB *surface_cel;
  CCB *draw_ccbs;
  void *surface_buffer;
  Item bitmap_item;
  Item vram_ioreq;
  int32 surface_bytes;
  int32 surface_pages;
  u8    ready;
} NkStickyTarget;

/*
 * Allocate an opaque 320x40 LRFORM floor crop in a dedicated VRAM bitmap.
 * The target owns the bitmap item, its page-aligned buffer, and the source
 * CEL which projects that buffer into the visible match.
 */
bool
nk_sticky_target_init(NkStickyTarget *target_,
                      Item            vram_ioreq_);

void
nk_sticky_target_shutdown(NkStickyTarget *target_);

/*
 * Reset once at match entry: SPORT-clear the bitmap, then seed the persistent
 * crop from the combined NOGGINBG layer1 CEL's opaque visible floor rows.
 */
bool
nk_sticky_target_reset(NkStickyTarget *target_,
                       CCB            *background_composite_);

CCB *
nk_sticky_target_surface_cel(NkStickyTarget *target_);

/*
 * Burn the pending image operations into the LRFORM bitmap at full intensity.
 * Call only after the old surface has already been submitted for the current
 * presentation; the writes then become visible on the next presentation.
 */
bool
nk_sticky_target_commit(NkStickyTarget       *target_,
                        const NkStickyQueue  *queue_,
                        NkStickyTargetResolve resolve_,
                        void                 *resolve_context_);
