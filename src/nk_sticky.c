#include "nk_sticky.h"

#include "stddef.h"

#define NK_STICKY_IMAGE_DIMENSION_MAX (255U)

void
nk_sticky_queue_reset(NkStickyQueue *queue_)
{
  if(queue_ == NULL)
    {
      return;
    }

  queue_->count = 0U;
}


bool
nk_sticky_source_accepts(s32    x_,
                         s32    y_,
                         u32    width_,
                         u32    height_)
{
  s32    floor_x;
  s32    floor_y;

  if((width_ == 0U) || (height_ == 0U))
    {
      return false;
    }

  floor_x = nk_wrap_add(x_, NK_STICKY_SOURCE_SCROLL_X);
  floor_y = nk_wrap_sub(y_, NK_STICKY_FLOOR_Y);

  if(nk_wrap_add(floor_y, (s32)height_) < 0)
    {
      return false;
    }

  if(floor_y >
     NK_STICKY_FLOOR_HEIGHT - NK_STICKY_BOTTOM_GATE_MARGIN)
    {
      return false;
    }

  if((nk_wrap_add(floor_x, (s32)width_) < 0) ||
     (floor_x > NK_STICKY_SOURCE_WIDTH))
    {
      return false;
    }

  /*
   * PutStickyImage computes a top clip and then immediately returns.
   * Preserve that retail quirk instead of accepting partially visible art.
   */
  if(floor_y < 0)
    {
      return false;
    }

  return true;
}


bool
nk_sticky_queue_effect(NkStickyQueue  *queue_,
                       const NkEffect *effect_)
{
  const NkAnimFrame *frame;
  const NkAnimImageRef *image;
  const NkAnimImageSize *size;
  NkAnimProjectedImage projected;
  NkStickyOp *operation;
  u32    image_index;
  u32    operation_index;
  int duplicate;

  if((queue_ == NULL) ||
     (effect_ == NULL))
    {
      return false;
    }

  frame = nk_effect_frame(effect_);
  if((frame == NULL) ||
     (frame->image_count > NK_STICKY_IMAGES_PER_EFFECT_LIMIT))
    {
      return false;
    }

  for(image_index = 0U;
      image_index < frame->image_count;
      ++image_index)
    {
      image = nk_anim_frame_image(frame, image_index);
      if(image == NULL)
        {
          return false;
        }

      nk_anim_project_image(
        image,
        effect_->x,
        effect_->y,
        -80,
        (u8)(effect_->draw_variant_index & NK_EFFECT_DIRECTION_MASK),
        &projected
        );
      size = nk_anim_image_size(
        NK_EFFECT_BANK_INDEX,
        projected.image
        );
      if(size == NULL)
        {
          return false;
        }

      if(!nk_sticky_source_accepts(
           projected.x,
           projected.y,
           size->width,
           size->height))
        {
          continue;
        }

      duplicate = false;
      for(operation_index = 0U;
          operation_index < queue_->count;
          ++operation_index)
        {
          operation = &queue_->ops[operation_index];
          if((operation->x == projected.x) &&
             (operation->y == projected.y) &&
             (operation->image == projected.image) &&
             (operation->orientation == projected.orientation))
            {
              duplicate = true;
              break;
            }
        }

      if(duplicate)
        {
          continue;
        }

      if(queue_->count >= NK_STICKY_PENDING_LIMIT)
        {
          return false;
        }

      operation = &queue_->ops[queue_->count];
      operation->x = projected.x;
      operation->y = projected.y;
      operation->image = projected.image;
      operation->orientation = projected.orientation;
      queue_->count++;
    }

  return true;
}


bool
nk_sticky_data_valid(void)
{
  const NkAnimMove *move;
  const NkAnimFrame *frame;
  const NkAnimImageRef *image;
  const NkAnimImageSize *size;
  u32    frame_index;
  u32    image_index;
  u32    move_index;

  for(move_index = 0U;
      move_index < NK_ANIM_MOVES_PER_BANK;
      ++move_index)
    {
      move = nk_anim_move(
        NK_EFFECT_BANK_INDEX,
        move_index
        );
      if(move == NULL)
        {
          return false;
        }

      for(frame_index = 0U;
          frame_index < move->frame_count;
          ++frame_index)
        {
          frame = nk_anim_move_frame(
            NK_EFFECT_BANK_INDEX,
            move_index,
            frame_index
            );
          if((frame == NULL) ||
             (frame->image_count >
              NK_STICKY_IMAGES_PER_EFFECT_LIMIT))
            {
              return false;
            }

          for(image_index = 0U;
              image_index < frame->image_count;
              ++image_index)
            {
              image = nk_anim_frame_image(frame, image_index);
              if(image == NULL)
                {
                  return false;
                }

              size = nk_anim_image_size(
                NK_EFFECT_BANK_INDEX,
                image->image
                );
              if((size == NULL) ||
                 (size->width == 0U) ||
                 (size->height == 0U) ||
                 (size->width > NK_STICKY_IMAGE_DIMENSION_MAX) ||
                 (size->height > NK_STICKY_IMAGE_DIMENSION_MAX))
                {
                  return false;
                }
            }
        }
    }

  return true;
}
