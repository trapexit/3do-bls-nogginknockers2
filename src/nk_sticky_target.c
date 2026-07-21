#include "nk_sticky_target.h"

#include "hardware.h"
#include "mem.h"

#include "string.h"


static
CCB *
_nk_sticky_target_create_surface_cel(void *buffer_)
{
  CCB *cel;

  cel = CreateCel(
    NK_STICKY_SURFACE_WIDTH,
    NK_STICKY_SURFACE_HEIGHT,
    16,
    CREATECEL_UNCODED,
    buffer_
    );
  if(cel == NULL)
    {
      return NULL;
    }

  cel->ccb_Flags |= CCB_BGND | PMODE_ONE;
  cel->ccb_PIXC = PIXC_OPAQUE;
  cel->ccb_PRE0 = (
    (
      (
        (NK_STICKY_SURFACE_HEIGHT + 1) >> 1
      ) - PRE0_VCNT_PREFETCH
    ) << PRE0_VCNT_SHIFT
    ) | PRE0_LINEAR | PRE0_BPP_16;
  cel->ccb_PRE1 = (
    (
      NK_STICKY_SURFACE_WIDTH - PRE1_WOFFSET_PREFETCH
    ) << PRE1_WOFFSET10_SHIFT
    ) | PRE1_TLLSB_PDC0 | PRE1_LRFORM |
                  (
    (
      NK_STICKY_SURFACE_WIDTH -
      PRE1_TLHPCNT_PREFETCH
    ) << PRE1_TLHPCNT_SHIFT
                  );
  return cel;
}


static
bool
_nk_sticky_target_flush(NkStickyTarget *target_,
                        u32             count_)
{
  CCB *last;
  Err error;

  if(count_ == 0U)
    {
      return true;
    }

  last = &target_->draw_ccbs[count_ - 1U];
  last->ccb_Flags |= CCB_LAST | CCB_NPABS;
  last->ccb_NextPtr = NULL;
  error = DrawCels(target_->bitmap_item, target_->draw_ccbs);
  return error >= 0;
}


static
bool
_nk_sticky_target_prepare_draw(CCB              *draw_,
                               CCB              *previous_,
                               CCB              *source_,
                               const NkStickyOp *operation_)
{
  s32    x;
  s32    y;

  (void)memcpy(draw_, source_, sizeof(*draw_));
  draw_->ccb_SourcePtr = CEL_DATAPTR(source_);
  if(draw_->ccb_SourcePtr == NULL)
    {
      return false;
    }

  if((source_->ccb_Flags & CCB_PPABS) != 0U)
    {
      draw_->ccb_PLUTPtr = source_->ccb_PLUTPtr;
    }
  else if(((source_->ccb_Flags & CCB_LDPLUT) != 0U) ||
          (source_->ccb_PLUTPtr != NULL))
    {
      draw_->ccb_PLUTPtr = CEL_PLUTPTR(source_);
    }
  else
    {
      draw_->ccb_PLUTPtr = NULL;
    }

  if(((source_->ccb_Flags & CCB_LDPLUT) != 0U) &&
     (draw_->ccb_PLUTPtr == NULL))
    {
      return false;
    }

  draw_->ccb_Flags &= ~(CCB_SKIP | CCB_LAST);
  draw_->ccb_Flags |= CCB_NPABS | CCB_SPABS | CCB_PPABS |
                      CCB_YOXY | CCB_LDSIZE | CCB_LDPRS | CCB_LDPPMP | CCB_LAST;
  draw_->ccb_NextPtr = NULL;
  draw_->ccb_PIXC = PIXC_OPAQUE;
  draw_->ccb_HDX = 1 << NK_CEL_HDX_SHIFT;
  draw_->ccb_HDY = 0;
  draw_->ccb_VDX = 0;
  draw_->ccb_VDY = 1 << NK_CEL_VDY_SHIFT;
  draw_->ccb_HDDX = 0;
  draw_->ccb_HDDY = 0;

  x = operation_->x;
  /*
   * SetClipOrigin makes (0,0) the top-left of the clipped floor window.
   * Convert the source painter's logical 160..199 floor coordinates to
   * that relative 0..39 coordinate system.
   */
  y = operation_->y - NK_STICKY_FLOOR_Y;
  if((operation_->orientation & 2U) != 0U)
    {
      x += draw_->ccb_Width - 1;
      draw_->ccb_HDX = -(1 << NK_CEL_HDX_SHIFT);
    }

  if((operation_->orientation & 1U) != 0U)
    {
      y += draw_->ccb_Height - 1;
      draw_->ccb_VDY = -(1 << NK_CEL_VDY_SHIFT);
    }

  draw_->ccb_XPos = x * NK_FIXED_ONE;
  draw_->ccb_YPos = y * NK_FIXED_ONE;

  if(previous_ != NULL)
    {
      previous_->ccb_Flags &= ~CCB_LAST;
      previous_->ccb_Flags |= CCB_NPABS;
      previous_->ccb_NextPtr = draw_;
    }

  return true;
}


bool
nk_sticky_target_init(NkStickyTarget *target_,
                      Item            vram_ioreq_)
{
  int32 page_size;
  int32 surface_size;

  if((target_ == NULL) || (vram_ioreq_ < 0))
    {
      return false;
    }

  (void)memset(target_, 0, sizeof(*target_));
  target_->bitmap_item = -1;
  target_->vram_ioreq = vram_ioreq_;
  page_size = GetPageSize(MEMTYPE_VRAM);
  if(page_size <= 0)
    {
      nk_sticky_target_shutdown(target_);
      return false;
    }

  surface_size =
    NK_STICKY_SURFACE_WIDTH *
    NK_STICKY_SURFACE_HEIGHT * 2;
  target_->surface_pages =
    (surface_size + page_size - 1) / page_size;
  target_->surface_bytes = target_->surface_pages * page_size;
  target_->surface_buffer = AllocMem(
    target_->surface_bytes,
    MEMTYPE_VRAM | MEMTYPE_CEL | MEMTYPE_STARTPAGE
    );
  if(target_->surface_buffer == NULL)
    {
      nk_sticky_target_shutdown(target_);
      return false;
    }

  target_->bitmap_item = CreateBitmapVA(
    CBM_TAG_WIDTH,
    NK_STICKY_SURFACE_WIDTH,
    CBM_TAG_HEIGHT,
    NK_STICKY_SURFACE_HEIGHT,
    CBM_TAG_BUFFER,
    target_->surface_buffer,
    TAG_END
    );
  if(target_->bitmap_item < 0)
    {
      nk_sticky_target_shutdown(target_);
      return false;
    }

  target_->surface_cel = _nk_sticky_target_create_surface_cel(
    target_->surface_buffer
    );
  if(target_->surface_cel == NULL)
    {
      nk_sticky_target_shutdown(target_);
      return false;
    }

  target_->draw_ccbs = (CCB *)AllocMem(
    (int32)(sizeof(CCB) * NK_STICKY_TARGET_BATCH_LIMIT),
    MEMTYPE_DRAM | MEMTYPE_FILL
    );
  if(target_->draw_ccbs == NULL)
    {
      nk_sticky_target_shutdown(target_);
      return false;
    }

  if((SetClipWidth(
        target_->bitmap_item,
        NK_STICKY_SURFACE_WIDTH) < 0) ||
     (SetClipHeight(
        target_->bitmap_item,
        NK_STICKY_SURFACE_HEIGHT) < 0) ||
     (SetClipOrigin(
        target_->bitmap_item,
        0,
        0) < 0))
    {
      nk_sticky_target_shutdown(target_);
      return false;
    }

  target_->ready = 1U;
  return true;
}


void
nk_sticky_target_shutdown(NkStickyTarget *target_)
{
  if(target_ == NULL)
    {
      return;
    }

  if(target_->surface_cel != NULL)
    {
      target_->surface_cel = DeleteCel(target_->surface_cel);
    }

  if(target_->draw_ccbs != NULL)
    {
      FreeMem(
        target_->draw_ccbs,
        (int32)(sizeof(CCB) * NK_STICKY_TARGET_BATCH_LIMIT)
        );
      target_->draw_ccbs = NULL;
    }

  if(target_->bitmap_item >= 0)
    {
      DeleteBitmap(target_->bitmap_item);
      target_->bitmap_item = -1;
    }

  if(target_->surface_buffer != NULL)
    {
      FreeMem(target_->surface_buffer, target_->surface_bytes);
      target_->surface_buffer = NULL;
    }

  target_->bitmap_item = -1;
  target_->vram_ioreq = -1;
  target_->surface_bytes = 0;
  target_->surface_pages = 0;
  target_->ready = 0U;
}


bool
nk_sticky_target_reset(NkStickyTarget *target_,
                       CCB            *background_composite_)
{
  Bitmap *bitmap;
  NkStickyOp operation;
  Err error;

  if((target_ == NULL) || (target_->ready == 0U) ||
     (background_composite_ == NULL) ||
     (background_composite_->ccb_Width <
      NK_STICKY_VISIBLE_CROP_X + NK_STICKY_SURFACE_WIDTH) ||
     (background_composite_->ccb_Height <
      NK_STICKY_FLOOR_Y +
      NK_STICKY_SURFACE_HEIGHT))
    {
      return false;
    }

  bitmap = (Bitmap *)LookupItem(target_->bitmap_item);
  if((bitmap == NULL) || (bitmap->bm_Buffer == NULL))
    {
      return false;
    }

  error = SetVRAMPages(
    target_->vram_ioreq,
    bitmap->bm_Buffer,
    0,
    target_->surface_pages,
    -1
    );
  if(error < 0)
    {
      return false;
    }

  /*
   * The combined layer1 CEL is drawn at logical (-160,0).  Preparing that
   * draw relative to the logical 160..199 floor selects source rows 160..199
   * and columns 160..479: the same layer1b rows 60..99 used by DrawFloorBG().
   */
  operation.x = -NK_STICKY_VISIBLE_CROP_X;
  operation.y = 0;
  operation.image = 0U;
  operation.orientation = 0U;
  if(!_nk_sticky_target_prepare_draw(
       &target_->draw_ccbs[0],
       NULL,
       background_composite_,
       &operation))
    {
      return false;
    }

  return _nk_sticky_target_flush(target_, 1U);
}


CCB *
nk_sticky_target_surface_cel(NkStickyTarget *target_)
{
  if((target_ == NULL) || (target_->ready == 0U))
    {
      return NULL;
    }

  return target_->surface_cel;
}


bool
nk_sticky_target_commit(NkStickyTarget       *target_,
                        const NkStickyQueue  *queue_,
                        NkStickyTargetResolve resolve_,
                        void                 *resolve_context_)
{
  const NkStickyOp *operation;
  const NkAnimImageSize *size;
  CCB *source;
  CCB *previous;
  void *loaded_plut;
  u32    batch_count;
  u32    index;

  if((target_ == NULL) || (target_->ready == 0U) ||
     (queue_ == NULL) ||
     (queue_->count > NK_STICKY_PENDING_LIMIT) ||
     (resolve_ == (NkStickyTargetResolve)0))
    {
      return false;
    }

  batch_count = 0U;
  loaded_plut = NULL;
  for(index = 0U; index < queue_->count; ++index)
    {
      operation = &queue_->ops[index];
      source = resolve_(resolve_context_, operation->image);
      size = nk_anim_image_size(
        NK_EFFECT_BANK_INDEX,
        operation->image
        );
      if((source == NULL) ||
         (size == NULL) ||
         (source->ccb_Width != (int32)size->width) ||
         (source->ccb_Height != (int32)size->height))
        {
          return false;
        }

      previous = (batch_count == 0U) ? NULL :
                 &target_->draw_ccbs[batch_count - 1U];
      if(!_nk_sticky_target_prepare_draw(
           &target_->draw_ccbs[batch_count],
           previous,
           source,
           operation))
        {
          return false;
        }

      if((target_->draw_ccbs[batch_count].ccb_Flags &
          CCB_LDPLUT) != 0U)
        {
          if(target_->draw_ccbs[batch_count].ccb_PLUTPtr ==
             loaded_plut)
            {
              target_->draw_ccbs[batch_count].ccb_Flags &=
                ~CCB_LDPLUT;
            }
          else
            {
              loaded_plut =
                target_->draw_ccbs[batch_count].ccb_PLUTPtr;
            }
        }

      batch_count++;
      if(batch_count == NK_STICKY_TARGET_BATCH_LIMIT)
        {
          if(!_nk_sticky_target_flush(target_, batch_count))
            {
              return false;
            }

          batch_count = 0U;
          loaded_plut = NULL;
        }
    }

  return _nk_sticky_target_flush(target_, batch_count);
}
