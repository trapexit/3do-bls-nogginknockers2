#include "nk_anim.h"

#include "nk_math.h"

#include "stddef.h"

const
NkAnimBank *
nk_anim_bank(u32 bank_index_)
{
  if(bank_index_ >= nk_anim_bank_count)
    return NULL;

  return &nk_anim_banks[bank_index_];
}


const
NkAnimMove *
nk_anim_move(u32 bank_index_,
             u32 move_index_)
{
  const NkAnimBank *bank;

  bank = nk_anim_bank(bank_index_);
  if((bank == NULL) || (move_index_ >= NK_ANIM_MOVES_PER_BANK))
    return NULL;

  return &nk_anim_moves[bank->move_first + move_index_];
}


const
NkAnimFrame *
nk_anim_move_frame(u32 bank_index_,
                   u32 move_index_,
                   u32 frame_index_)
{
  const NkAnimBank *bank;
  const NkAnimMove *move;
  u32 global_frame;

  bank = nk_anim_bank(bank_index_);
  move = nk_anim_move(bank_index_, move_index_);
  if((bank == NULL) || (move == NULL) || (frame_index_ >= move->frame_count))
    return NULL;

  global_frame = (u32)move->first_frame + frame_index_;
  if((global_frame < bank->frame_first) ||
     (global_frame >= (u32)bank->frame_first + (u32)bank->frame_count))
    return NULL;

  return &nk_anim_frames[global_frame];
}


const
NkAnimImageRef *
nk_anim_frame_image(const NkAnimFrame *frame_,
                    u32                image_index_)
{
  if((frame_ == NULL) || (image_index_ >= frame_->image_count))
    return NULL;

  return &nk_anim_image_refs[frame_->image_first + image_index_];
}


const
NkAnimImageSize *
nk_anim_image_size(u32 bank_index_,
                   u32 image_index_)
{
  const NkAnimBank *bank;
  const NkAnimImageSize *size;
  u32    global_index;

  bank = nk_anim_bank(bank_index_);
  if((bank == NULL) || (image_index_ >= bank->image_index_limit))
    return NULL;

  global_index = (u32)bank->image_size_first + image_index_;
  if(global_index >= nk_anim_image_size_count)
    return NULL;

  size = &nk_anim_image_sizes[global_index];
  if((size->width == 0U) || (size->height == 0U))
    return NULL;

  return size;
}


const
NkAnimImageSize *
nk_anim_pain_image_size(u32 bank_index_,
                        u32 image_index_)
{
  const NkAnimBank *bank;
  const NkAnimImageSize *size;
  u32 global_index;

  bank = nk_anim_bank(bank_index_);
  if((bank == NULL) || (image_index_ >= bank->pain_image_index_limit))
    return NULL;

  global_index = (u32)bank->pain_image_size_first + image_index_;
  if(global_index >= nk_anim_pain_image_size_count)
    return NULL;

  size = &nk_anim_pain_image_sizes[global_index];
  if((size->width == 0U) || (size->height == 0U))
    return NULL;

  return size;
}


const
NkAnimRect *
nk_anim_frame_vulnerable(const NkAnimFrame *frame_,
                         u32                rectangle_index_)
{
  if((frame_ == NULL) || (rectangle_index_ >= frame_->vulnerable_count))
    return NULL;

  return &nk_anim_rects[frame_->vulnerable_first + rectangle_index_];
}


const
NkAnimRect *
nk_anim_frame_attack(const NkAnimFrame *frame_,
                     u32                rectangle_index_)
{
  if((frame_ == NULL) || (rectangle_index_ >= frame_->attack_count))
    return NULL;

  return &nk_anim_rects[frame_->attack_first + rectangle_index_];
}


const
NkAnimAttachment *
nk_anim_frame_attachment(const NkAnimFrame *frame_)
{
  if((frame_ == NULL) || (frame_->attachment_index == NK_ANIM_NO_ATTACHMENT))
    return NULL;

  if((frame_->attachment_index < 0) ||
     ((u32)frame_->attachment_index >= (u32)nk_anim_attachment_count))
    {
      return NULL;
    }

  return &nk_anim_attachments[frame_->attachment_index];
}


const
NkAnimPainLayout *
nk_anim_pain_layout(u32 bank_index_,
                    u8  base_image_)
{
  const NkAnimBank *bank;
  const NkAnimPainLayout *layout;
  u32 index;

  bank = nk_anim_bank(bank_index_);
  if(bank == NULL)
    return NULL;

  for(index = 0U; index < bank->pain_layout_count; ++index)
    {
      layout = &nk_anim_pain_layouts[bank->pain_layout_first + index];
      if(layout->base_image == base_image_)
        return layout;
    }

  return NULL;
}


const
NkAnimImageRef *
nk_anim_pain_layout_image(const NkAnimPainLayout *layout_,
                          u32                     image_index_)
{
  if((layout_ == NULL) || (image_index_ >= layout_->image_count))
    return NULL;

  return &nk_anim_image_refs[layout_->image_first + image_index_];
}


bool
nk_anim_bank_uses_image(u32 bank_index_,
                        u32 image_index_)
{
  const NkAnimBank *bank;
  const NkAnimFrame *frame;
  const NkAnimImageRef *image;
  u32 frame_index;
  u32 reference_index;

  bank = nk_anim_bank(bank_index_);
  if((bank == NULL) || (image_index_ >= bank->image_index_limit))
    return false;

  for(frame_index = 0U;
      frame_index < bank->frame_count;
      ++frame_index)
    {
      frame = &nk_anim_frames[bank->frame_first + frame_index];
      for(reference_index = 0U;
          reference_index < frame->image_count;
          ++reference_index)
        {
          image = nk_anim_frame_image(frame, reference_index);
          if((image != NULL) && (image->image == image_index_))
            return true;
        }
    }

  return false;
}


bool
nk_anim_bank_uses_pain_image(u32 bank_index_,
                             u32 image_index_)
{
  const NkAnimBank *bank;
  const NkAnimPainLayout *layout;
  const NkAnimImageRef *image;
  u32 layout_index;
  u32 reference_index;

  bank = nk_anim_bank(bank_index_);
  if((bank == NULL) || (image_index_ >= bank->pain_image_index_limit))
    return false;

  for(layout_index = 0U;
      layout_index < bank->pain_layout_count;
      ++layout_index)
    {
      layout = &nk_anim_pain_layouts[bank->pain_layout_first + layout_index];
      for(reference_index = 0U;
          reference_index < layout->image_count;
          ++reference_index)
        {
          image = nk_anim_pain_layout_image(layout, reference_index);
          if((image != NULL) && (image->image == image_index_))
            return true;
        }
    }

  return false;
}


u32
nk_anim_bank_sound_mask(u32    bank_index_)
{
  const NkAnimBank *bank;
  const NkAnimFrame *frame;
  u32 frame_index;
  u32 mask;
  u8  code;

  bank = nk_anim_bank(bank_index_);
  if(bank == NULL)
    return 0U;

  /*
   * The v0.78 frame player passes sound type zero, so the low nibble is
   * the only frame-addressable player sample selector.  Direct head
   * hit/score calls are added separately by the Portfolio audio adapter.
   */
  mask = 0U;
  for(frame_index = 0U;
      frame_index < bank->frame_count;
      ++frame_index)
    {
      frame = &nk_anim_frames[bank->frame_first + frame_index];
      code = (u8)(frame->sound_effect_bits & NK_ANIM_SOUND_CODE_MASK);
      if(((code & NK_ANIM_SOUND_PLAYER_FLAG) != 0U) && ((code & NK_ANIM_SOUND_SAMPLE_MASK) != 0U))
        mask |= 1U << ((code & NK_ANIM_SOUND_SAMPLE_MASK) - 1U);
    }

  return mask;
}


void
nk_anim_cursor_reset(NkAnimCursor *cursor_)
{
  cursor_->bank_index = 0U;
  cursor_->move_index = 0U;
  cursor_->frame_index = 0U;
  cursor_->remaining = 0;
  cursor_->valid = 0U;
}


bool
nk_anim_cursor_start(NkAnimCursor *cursor_,
                     u32           bank_index_,
                     u32           move_index_)
{
  const NkAnimMove *move;
  const NkAnimFrame *frame;

  move = nk_anim_move(bank_index_, move_index_);
  if((move == NULL) || (move->frame_count == 0U))
    {
      nk_anim_cursor_reset(cursor_);
      return false;
    }

  frame = nk_anim_move_frame(bank_index_, move_index_, 0U);
  if(frame == NULL)
    {
      nk_anim_cursor_reset(cursor_);
      return false;
    }

  cursor_->bank_index = bank_index_;
  cursor_->move_index = move_index_;
  cursor_->frame_index = 0U;
  cursor_->remaining = frame->duration_100hz;
  cursor_->valid = 1U;

  return true;
}


void
nk_anim_cursor_stop(NkAnimCursor *cursor_)
{
  cursor_->valid = 0U;
}


const
NkAnimFrame *
nk_anim_cursor_frame(const NkAnimCursor *cursor_)
{
  if((cursor_ == NULL) || (cursor_->valid == 0U))
    return NULL;

  return nk_anim_move_frame(cursor_->bank_index,
                            cursor_->move_index,
                            cursor_->frame_index);
}


int
nk_anim_cursor_tick(NkAnimCursor *cursor_)
{
  const NkAnimMove *move;
  const NkAnimFrame *frame;

  if((cursor_ == NULL) || (cursor_->valid == 0U))
    return NK_ANIM_STEP_INVALID;

  move = nk_anim_move(cursor_->bank_index, cursor_->move_index);
  if((move == NULL) || (cursor_->frame_index >= move->frame_count))
    return NK_ANIM_STEP_INVALID;

  if(cursor_->remaining > 0)
    {
      cursor_->remaining--;
      return NK_ANIM_STEP_NONE;
    }

  cursor_->frame_index++;
  if(cursor_->frame_index >= move->frame_count)
    return NK_ANIM_STEP_COMPLETE;

  frame = nk_anim_cursor_frame(cursor_);
  if(frame == NULL)
    return NK_ANIM_STEP_INVALID;

  cursor_->remaining = frame->duration_100hz;

  return NK_ANIM_STEP_FRAME;
}


bool
nk_anim_cursor_hold_last(NkAnimCursor *cursor_,
                         s32           remaining_)
{
  const NkAnimMove *move;

  if((cursor_ == NULL) || (cursor_->valid == 0U))
    return false;

  move = nk_anim_move(cursor_->bank_index, cursor_->move_index);
  if((move == NULL) || (move->frame_count == 0U))
    return false;

  cursor_->frame_index = (u32)move->frame_count - 1U;
  cursor_->remaining = remaining_;

  return true;
}


s32
nk_anim_frame_delta_x(const NkAnimFrame *frame_)
{
  u16 bits;

  if(frame_ == NULL)
    return 0;

  bits = (u16)(u8)frame_->dx;
  bits = (u16)(bits | ((u16)(u8)frame_->dy << NK_ANIM_FRAME_DY_SHIFT));

  return nk_s16_from_bits(bits);
}


s32
nk_anim_frame_delta_y(const NkAnimFrame *frame_)
{
  u16 bits;

  if(frame_ == NULL)
    return 0;

  bits = (u16)(u8)frame_->tx;
  bits = (u16)(bits | ((u16)(u8)frame_->ty << NK_ANIM_FRAME_DY_SHIFT));

  return nk_s16_from_bits(bits);
}


u32
nk_anim_frame_clip_bits(const NkAnimFrame *frame_)
{
  u32 bits;

  if(frame_ == NULL)
    return 0U;

  bits = (u32)(u8)frame_->dx;
  bits |= (u32)(u8)frame_->dy << NK_ANIM_FRAME_DY_SHIFT;
  bits |= (u32)(u8)frame_->tx << NK_ANIM_FRAME_TX_SHIFT;
  bits |= (u32)(u8)frame_->ty << NK_ANIM_FRAME_TY_SHIFT;

  return bits;
}


void
nk_anim_project_image(const NkAnimImageRef *image_,
                      s32                   origin_x_,
                      s32                   origin_y_,
                      s32                   y_bias_,
                      u8                    facing_,
                      NkAnimProjectedImage *projected_)
{
  s32 offset_x;

  offset_x = facing_ != 0U ? image_->x_flipped : image_->x_normal;
  projected_->image = image_->image;
  projected_->orientation = (u8)(image_->orientation ^ ((facing_ & 1U) << 1));
  projected_->x = nk_wrap_add(origin_x_, offset_x);
  projected_->y = nk_wrap_add(nk_wrap_add(origin_y_, y_bias_), image_->y);
}


static
s16
_nk_anim_project_coordinate(s32 origin_,
                            s16 offset_,
                            int subtract_)
{
  u16 origin_bits;
  u16 offset_bits;
  u16 result;

  origin_bits = (u16)(u32)origin_;
  offset_bits = (u16)offset_;
  if(subtract_)
    result = (u16)(origin_bits - offset_bits);
  else
    result = (u16)(origin_bits + offset_bits);

  return nk_s16_from_bits(result);
}


void
nk_anim_project_rect(const NkAnimRect *rectangle_,
                     s32               origin_x_,
                     s32               origin_y_,
                     u8                facing_,
                     NkAnimRect       *projected_)
{
  projected_->y1 = _nk_anim_project_coordinate(origin_y_, rectangle_->y1, false);
  projected_->y2 = _nk_anim_project_coordinate(origin_y_, rectangle_->y2, false);
  if(facing_ == 0U)
    {
      projected_->x1 = _nk_anim_project_coordinate(origin_x_, rectangle_->x1, false);
      projected_->x2 = _nk_anim_project_coordinate(origin_x_, rectangle_->x2, false);
    }
  else
    {
      projected_->x1 = _nk_anim_project_coordinate(origin_x_, rectangle_->x2, true);
      projected_->x2 = _nk_anim_project_coordinate(origin_x_, rectangle_->x1, true);
    }
}


bool
nk_anim_rects_overlap(const NkAnimRect *left_,
                      const NkAnimRect *right_)
{
  if((left_->x1 > right_->x2) || (left_->x2 < right_->x1) ||
     (left_->y1 > right_->y2) || (left_->y2 < right_->y1))
    return false;

  return true;
}


bool
nk_anim_project_attachment(const NkAnimFrame         *frame_,
                           s32                        origin_x_,
                           s32                        origin_y_,
                           u8                         facing_,
                           NkAnimProjectedAttachment *projected_)
{
  const NkAnimAttachment *attachment;

  attachment = nk_anim_frame_attachment(frame_);
  if(attachment == NULL)
    return false;

  projected_->kind_flags = attachment->kind_flags;
  if(facing_ == 0U)
    projected_->x = nk_wrap_add(origin_x_, attachment->x);
  else
    projected_->x = nk_wrap_sub(origin_x_, attachment->x);

  projected_->y = nk_wrap_add(origin_y_, attachment->y);

  return true;
}


static
bool
_nk_anim_frame_valid(const NkAnimFrame *frame_,
                     const NkAnimBank  *bank_)
{
  const NkAnimImageRef *image;
  const NkAnimImageSize *size;
  u32    index;

  if(((u32)frame_->image_first + frame_->image_count > nk_anim_image_ref_count) ||
     ((u32)frame_->vulnerable_first + frame_->vulnerable_count > nk_anim_rect_count) ||
     ((u32)frame_->attack_first + frame_->attack_count > nk_anim_rect_count))
    {
      return false;
    }

  if((frame_->attachment_index != NK_ANIM_NO_ATTACHMENT) &&
     ((frame_->attachment_index < 0) ||
      ((u32)frame_->attachment_index >=
       (u32)nk_anim_attachment_count)))
    {
      return false;
    }

  for(index = 0; index < frame_->image_count; ++index)
    {
      image = &nk_anim_image_refs[frame_->image_first + index];
      if(image->image >= bank_->image_index_limit)
        {
          return false;
        }

      size = &nk_anim_image_sizes[bank_->image_size_first + image->image];
      if((size->width == 0U) || (size->height == 0U))
        {
          return false;
        }
    }

  return true;
}


bool
nk_anim_data_valid(void)
{
  u32    bank_index;

  if((nk_anim_bank_count != NK_ANIM_BANK_COUNT) ||
     (nk_anim_move_count != NK_ANIM_BANK_COUNT * NK_ANIM_MOVES_PER_BANK))
    {
      return false;
    }

  for(bank_index = 0; bank_index < nk_anim_bank_count; ++bank_index)
    {
      const NkAnimBank *bank;
      u32    move_index;
      u32    frame_index;
      u32    pain_index;
      u32    special_index;

      bank = &nk_anim_banks[bank_index];
      if(((u32)bank->move_first + NK_ANIM_MOVES_PER_BANK > nk_anim_move_count) ||
         ((u32)bank->frame_first + bank->frame_count > nk_anim_frame_count) ||
         ((u32)bank->pain_layout_first + bank->pain_layout_count >
          nk_anim_pain_layout_count) ||
         ((u32)bank->special_move_first + bank->special_move_count >
          nk_anim_special_move_count) ||
         ((u32)bank->image_size_first + bank->image_index_limit >
          nk_anim_image_size_count) ||
         ((u32)bank->pain_image_size_first +
          bank->pain_image_index_limit > nk_anim_pain_image_size_count))
        {
          return false;
        }

      for(move_index = 0; move_index < NK_ANIM_MOVES_PER_BANK; ++move_index)
        {
          const NkAnimMove *move;

          move = &nk_anim_moves[bank->move_first + move_index];
          if(move->frame_count == 0)
            {
              if(move->first_frame != 0)
                {
                  return false;
                }
            }
          else if((move->first_frame < bank->frame_first) ||
                  ((u32)move->first_frame + move->frame_count >
                   (u32)bank->frame_first + bank->frame_count))
            {
              return false;
            }
        }

      for(frame_index = 0; frame_index < bank->frame_count; ++frame_index)
        {
          if(!_nk_anim_frame_valid(&nk_anim_frames[bank->frame_first + frame_index], bank))
            {
              return false;
            }
        }

      for(pain_index = 0; pain_index < bank->pain_layout_count; ++pain_index)
        {
          const NkAnimPainLayout *layout;
          const NkAnimImageRef *image;
          const NkAnimImageSize *size;
          u32    image_index;

          layout = &nk_anim_pain_layouts[bank->pain_layout_first + pain_index];
          if((layout->base_image >= bank->image_index_limit) ||
             ((u32)layout->image_first + layout->image_count >
              nk_anim_image_ref_count) ||
             (nk_anim_image_sizes[
                                  bank->image_size_first + layout->base_image
                                  ].width == 0U) ||
             (nk_anim_image_sizes[
                                  bank->image_size_first + layout->base_image
                                  ].height == 0U))
            {
              return false;
            }

          for(image_index = 0; image_index < layout->image_count; ++image_index)
            {
              image = &nk_anim_image_refs[layout->image_first + image_index];
              if(image->image >= bank->pain_image_index_limit)
                {
                  return false;
                }

              size = &nk_anim_pain_image_sizes[
                                               bank->pain_image_size_first + image->image
                                               ];
              if((size->width == 0U) || (size->height == 0U))
                {
                  return false;
                }
            }
        }

      for(special_index = 0; special_index < bank->special_move_count; ++special_index)
        {
          const NkAnimSpecialMove *special;

          special = &nk_anim_special_moves[bank->special_move_first + special_index];
          if((u32)special->position_first + special->position_count >
             nk_anim_joy_position_count)
            {
              return false;
            }
        }
    }

  return true;
}
