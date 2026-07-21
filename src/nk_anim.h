#pragma once

#include "nk_types.h"

#define NK_ANIM_BANK_COUNT (10)
#define NK_ANIM_MOVES_PER_BANK (130)

#define NK_ANIM_BANK_FIGHTER (0)
#define NK_ANIM_BANK_HEAD (1)
#define NK_ANIM_BANK_EFFECTS (2)
#define NK_ANIM_PAIN_BANK (8U)

#define NK_ANIM_NO_ATTACHMENT ((s16) - 1)

#define NK_ANIM_NO_IMAGE ((u8)0xffU)

#define NK_ANIM_SOUND_CODE_BITS   (4U)
#define NK_ANIM_SOUND_CODE_MASK   (0x0fU)
#define NK_ANIM_SOUND_PLAYER_FLAG (0x08U)
#define NK_ANIM_SOUND_SAMPLE_MASK (0x07U)
#define NK_ANIM_FRAME_DY_SHIFT (8U)
#define NK_ANIM_FRAME_TX_SHIFT (16U)
#define NK_ANIM_FRAME_TY_SHIFT (24U)

#define NK_ANIM_STEP_INVALID (-1)
#define NK_ANIM_STEP_NONE (0)
#define NK_ANIM_STEP_FRAME (1)
#define NK_ANIM_STEP_COMPLETE (2)

typedef struct NkAnimImageRef
{
  u8 image;
  u8 orientation;
  s8 x_normal;
  s8 x_flipped;
  s8 y;
} NkAnimImageRef;

typedef struct NkAnimImageSize
{
  u16 width;
  u16 height;
} NkAnimImageSize;

typedef struct NkAnimRect
{
  s16 x1;
  s16 y1;
  s16 x2;
  s16 y2;
} NkAnimRect;

typedef struct NkAnimAttachment
{
  u8  kind_flags;
  s16 x;
  s16 y;
} NkAnimAttachment;

typedef struct NkAnimFrame
{
  u16 image_first;
  u8  image_count;
  u16 vulnerable_first;
  u8  vulnerable_count;
  u16 attack_first;
  u8  attack_count;
  s16 attachment_index;

  u16 source_offset;
  u16 source_size;
  u8  shadow;
  u8  duration_100hz;
  u8  sound_effect_bits;
  u16 parameter_bits;
  u8  energy;
  s8  dx;
  s8  dy;
  s8  tx;
  s8  ty;
} NkAnimFrame;

typedef struct NkAnimMove
{
  u16 first_frame;
  u8  frame_count;
} NkAnimMove;

typedef struct NkAnimPainLayout
{
  u8  base_image;
  u16 image_first;
  u8  image_count;
} NkAnimPainLayout;

typedef struct NkAnimJoyPosition
{
  u8  direction_bits;
  s16 minimum_ticks;
  s16 maximum_ticks;
} NkAnimJoyPosition;

typedef struct NkAnimSpecialMove
{
  u16 position_first;
  u8  position_count;
} NkAnimSpecialMove;

typedef struct NkAnimBank
{
  char name[10];
  s8   character_type;
  u8   kind;
  u16  move_first;
  u16  frame_first;
  u16  frame_count;
  u16  pain_layout_first;
  u8   pain_layout_count;
  u16  special_move_first;
  u8   special_move_count;
  u16  image_size_first;
  u16  pain_image_size_first;
  u16  image_index_limit;
  u16  pain_image_index_limit;
} NkAnimBank;

typedef struct NkAnimCursor
{
  u32 bank_index;
  u32 move_index;
  u32 frame_index;
  s32 remaining;
  u8  valid;
} NkAnimCursor;

typedef struct NkAnimProjectedImage
{
  u8  image;
  u8  orientation;
  s32 x;
  s32 y;
} NkAnimProjectedImage;

typedef struct NkAnimProjectedAttachment
{
  u8  kind_flags;
  s32 x;
  s32 y;
} NkAnimProjectedAttachment;

extern const NkAnimBank nk_anim_banks[];
extern const NkAnimMove nk_anim_moves[];
extern const NkAnimFrame nk_anim_frames[];
extern const NkAnimImageRef nk_anim_image_refs[];
extern const NkAnimImageSize nk_anim_image_sizes[];
extern const NkAnimImageSize nk_anim_pain_image_sizes[];
extern const NkAnimRect nk_anim_rects[];
extern const NkAnimAttachment nk_anim_attachments[];
extern const NkAnimPainLayout nk_anim_pain_layouts[];
extern const NkAnimSpecialMove nk_anim_special_moves[];
extern const NkAnimJoyPosition nk_anim_joy_positions[];

extern const u16 nk_anim_bank_count;
extern const u16 nk_anim_move_count;
extern const u16 nk_anim_frame_count;
extern const u16 nk_anim_image_ref_count;
extern const u16 nk_anim_image_size_count;
extern const u16 nk_anim_pain_image_size_count;
extern const u16 nk_anim_rect_count;
extern const u16 nk_anim_attachment_count;
extern const u16 nk_anim_pain_layout_count;
extern const u16 nk_anim_special_move_count;
extern const u16 nk_anim_joy_position_count;

const
NkAnimBank *
nk_anim_bank(u32 bank_index_);
const
NkAnimMove *
nk_anim_move(u32 bank_index_,
             u32 move_index_);
const
NkAnimFrame *
nk_anim_move_frame(u32 bank_index_,
                   u32 move_index_,
                   u32 frame_index_);
const
NkAnimImageRef *
nk_anim_frame_image(const NkAnimFrame *frame_,
                    u32                image_index_);
const
NkAnimImageSize *
nk_anim_image_size(u32 bank_index_,
                   u32 image_index_);
const
NkAnimImageSize *
nk_anim_pain_image_size(u32 bank_index_,
                        u32 image_index_);
const
NkAnimRect *
nk_anim_frame_vulnerable(const NkAnimFrame *frame_,
                         u32                rectangle_index_);
const
NkAnimRect *
nk_anim_frame_attack(const NkAnimFrame *frame_,
                     u32                rectangle_index_);
const
NkAnimAttachment *
nk_anim_frame_attachment(const NkAnimFrame *frame_);
const
NkAnimPainLayout *
nk_anim_pain_layout(u32 bank_index_,
                    u8  base_image_);
const
NkAnimImageRef *
nk_anim_pain_layout_image(const NkAnimPainLayout *layout_,
                          u32                     image_index_);
bool
nk_anim_bank_uses_image(u32    bank_index_,
                        u32    image_index_);
bool
nk_anim_bank_uses_pain_image(u32    bank_index_,
                             u32    image_index_);
u32
nk_anim_bank_sound_mask(u32    bank_index_);

void
nk_anim_cursor_reset(NkAnimCursor *cursor_);
bool
nk_anim_cursor_start(NkAnimCursor *cursor_,
                     u32           bank_index_,
                     u32           move_index_);
void
nk_anim_cursor_stop(NkAnimCursor *cursor_);
const
NkAnimFrame *
nk_anim_cursor_frame(const NkAnimCursor *cursor_);
int
nk_anim_cursor_tick(NkAnimCursor *cursor_);
bool
nk_anim_cursor_hold_last(NkAnimCursor *cursor_,
                         s32           remaining_);

s32
nk_anim_frame_delta_x(const NkAnimFrame *frame_);
s32
nk_anim_frame_delta_y(const NkAnimFrame *frame_);
u32
nk_anim_frame_clip_bits(const NkAnimFrame *frame_);
void
nk_anim_project_image(const NkAnimImageRef *image_,
                      s32                   origin_x_,
                      s32                   origin_y_,
                      s32                   y_bias_,
                      u8                    facing_,
                      NkAnimProjectedImage *projected_);
void
nk_anim_project_rect(const NkAnimRect *rectangle_,
                     s32               origin_x_,
                     s32               origin_y_,
                     u8                facing_,
                     NkAnimRect       *projected_);
bool
nk_anim_rects_overlap(const NkAnimRect *left_,
                      const NkAnimRect *right_);
bool
nk_anim_project_attachment(const NkAnimFrame         *frame_,
                           s32                        origin_x_,
                           s32                        origin_y_,
                           u8                         facing_,
                           NkAnimProjectedAttachment *projected_);
bool
nk_anim_data_valid(void);
