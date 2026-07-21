#pragma once

#include "nk_anim.h"

typedef struct NkCollisionContact
{
  NkAnimRect attack;
  NkAnimRect vulnerable;
  s32    attack_center_y_relative;
  s32    separation_x_fixed;
} NkCollisionContact;

bool
nk_collision_first_contact(const NkAnimFrame  *attacker_frame_,
                           s32                 attacker_x_,
                           s32                 attacker_y_,
                           u8                  attacker_facing_,
                           const NkAnimFrame  *defender_frame_,
                           s32                 defender_x_,
                           s32                 defender_y_,
                           u8                  defender_facing_,
                           NkCollisionContact *contact_);
