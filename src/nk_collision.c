#include "nk_collision.h"

#include "nk_math.h"

#include "stddef.h"


static
s32
_nk_collision_half_toward_zero(s32    value_)
{
  if(value_ < 0)
    {
      return -((-value_) / 2);
    }

  return value_ / 2;
}


bool
nk_collision_first_contact(const NkAnimFrame  *attacker_frame_,
                           s32                 attacker_x_,
                           s32                 attacker_y_,
                           u8                  attacker_facing_,
                           const NkAnimFrame  *defender_frame_,
                           s32                 defender_x_,
                           s32                 defender_y_,
                           u8                  defender_facing_,
                           NkCollisionContact *contact_)
{
  const NkAnimRect *attack;
  const NkAnimRect *vulnerable;
  s32    center_sum;
  s32    separation;

  if(contact_ == NULL)
    {
      return false;
    }

  attack = nk_anim_frame_attack(attacker_frame_, 0U);
  vulnerable = nk_anim_frame_vulnerable(defender_frame_, 0U);
  if((attack == NULL) || (vulnerable == NULL))
    {
      return false;
    }

  nk_anim_project_rect(
    attack,
    attacker_x_,
    attacker_y_,
    attacker_facing_,
    &contact_->attack
    );
  nk_anim_project_rect(
    vulnerable,
    defender_x_,
    defender_y_,
    defender_facing_,
    &contact_->vulnerable
    );
  if(!nk_anim_rects_overlap(&contact_->attack, &contact_->vulnerable))
    {
      return false;
    }

  center_sum = (s32)contact_->attack.y1 + contact_->attack.y2;
  contact_->attack_center_y_relative = nk_wrap_sub(
    _nk_collision_half_toward_zero(center_sum),
    defender_y_
    );
  if(attacker_facing_ == 0U)
    {
      separation = (s32)contact_->attack.x2 - contact_->vulnerable.x1;
    }
  else
    {
      separation = (s32)contact_->attack.x1 - contact_->vulnerable.x2;
    }

  contact_->separation_x_fixed = nk_fixed_from_int(separation);
  return true;
}
