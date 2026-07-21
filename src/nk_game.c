#include "nk_game.h"

#include "stddef.h"
#include "string.h"

#define NK_HEAD_ANIMATION_BANK (8U)
#define NK_HEAD_CHARACTER_TYPE (10U)
#define NK_EFFECT_PRESENTATION_HZ (30U)

#define NK_GAME_DIFFICULTY_LIMIT  (8)
#define NK_GAME_INITIAL_ENERGY    (20)
#define NK_GAME_ENERGY_LIMIT      (128)
#define NK_GAME_ARENA_CENTER_X    (160)
#define NK_GAME_ARENA_TOP         (8)
#define NK_GAME_ARENA_BOTTOM      (192)
#define NK_GAME_LEFT_GOAL_X       (-45)
#define NK_GAME_RIGHT_GOAL_X      (365)

#define NK_GAME_PLAYER_START_X_LEFT  (-20)
#define NK_GAME_PLAYER_START_X_RIGHT (340)
#define NK_GAME_PLAYER_START_Y       (100)

#define NK_GAME_BALL_START_X (NK_GAME_ARENA_CENTER_X)
#define NK_GAME_BALL_START_Y (100)


static
s32
_nk_game_abs(s32    value_)
{
  if(value_ < 0)
    {
      return nk_wrap_neg(value_);
    }

  return value_;
}


static
s32
_nk_game_half(s32    value_)
{
  u32    magnitude;

  if(value_ >= 0)
    {
      return value_ / 2;
    }

  magnitude = 0U - (u32)value_;
  return -(s32)(magnitude / 2U);
}


static
void
_nk_game_event(NkGame *game_,
               u8      type_,
               u8      actor_,
               u8      value_,
               u8      flags_,
               s32     x_,
               s32     y_)
{
  NkGameEvent *event;

  if(game_->event_count >= NK_GAME_EVENT_COUNT)
    {
      return;
    }

  event = &game_->events[game_->event_count++];
  event->type = type_;
  event->actor = actor_;
  event->value = value_;
  event->flags = flags_;
  event->x = x_;
  event->y = y_;
}


void
nk_game_clear_events(NkGame *game_)
{
  if(game_ != NULL)
    {
      game_->event_count = 0U;
    }
}


const
NkAnimFrame *
nk_game_player_frame(const NkGamePlayer *player_)
{
  if(player_ == NULL)
    {
      return NULL;
    }

  return nk_anim_cursor_frame(&player_->animation);
}


static
bool
_nk_game_start_animation(NkGamePlayer *player_,
                         s32           move_)
{
  if((move_ < 0) || (move_ >= NK_ANIM_MOVES_PER_BANK))
    {
      nk_anim_cursor_stop(&player_->animation);
      return false;
    }

  player_->move = move_;
  player_->new_move = 0U;
  return nk_anim_cursor_start(
    &player_->animation,
    player_->animation_bank,
    (u32)move_
    );
}


static
void
_nk_game_request_move(NkGamePlayer *player_,
                      s32           move_)
{
  player_->move = move_;
  player_->new_move = 1U;
}


static
void
_nk_game_reset_projectiles(NkGamePlayer *player_)
{
  int index;

  for(index = 0; index < NK_GAME_PROJECTILE_COUNT; ++index)
    {
      NkProjectile *projectile;

      projectile = &player_->projectiles[index];
      nk_anim_cursor_reset(&projectile->animation);
      projectile->active = 0U;
      projectile->state = 0U;
      projectile->type = 0U;
      projectile->facing = 0U;
      projectile->x = 0;
      projectile->y = 0;
      projectile->fraction_x = 0;
      projectile->fraction_y = 0;
      projectile->shake = 0;
      projectile->stick_x = 0;
      projectile->stick_y = 0;
      player_->projectile_previous_x[index] = 0;
      player_->projectile_previous_y[index] = 0;
      player_->projectile_interpolation_ready[index] = 0U;
    }

  player_->projectile_exists = 0U;
}


static
bool
_nk_game_reset_player(NkGamePlayer *player_)
{
  player_->freeze = 0;
  player_->control_ball = 0;
  player_->stuck = 0;
  if(player_->control_type == NK_CONTROL_BALL)
    {
      player_->move = NK_MOVE_PROJECTILE_REPEAT;
    }
  else
    {
      player_->move = NK_MOVE_STANCE;
    }

  player_->draw_me = 1U;
  player_->new_move = 0U;
  player_->dizzy_duration = 0;
  player_->maximum_energy = NK_GAME_INITIAL_ENERGY;
  player_->energy = NK_GAME_INITIAL_ENERGY;
  player_->destination_x = -1;
  player_->destination_y = -1;
  player_->fraction_x = 0;
  player_->fraction_y = 0;
  player_->presentation_previous_x = player_->x * 256;
  player_->presentation_previous_y = player_->y * 256;
  player_->presentation_draw_me = player_->draw_me;
  player_->presentation_frozen = 0U;
  player_->pain = 0;
  player_->strategy = 0;
  player_->strategy_duration = 0;
  _nk_game_reset_projectiles(player_);
  return _nk_game_start_animation(player_, player_->move);
}


static
void
_nk_game_set_character_vector(NkGamePlayer            *player_,
                              const nk_motion_profile *profile_,
                              int                      mirrored_)
{
  player_->vector.bound_min = profile_->bound_min;
  player_->vector.bound_max = profile_->bound_max;
  if(mirrored_)
    {
      player_->vector.bound_min.x = nk_wrap_sub(
        nk_fixed_from_int(NK_LOGICAL_WIDTH),
        profile_->bound_max.x
        );
      player_->vector.bound_max.x = nk_wrap_sub(
        nk_fixed_from_int(NK_LOGICAL_WIDTH),
        profile_->bound_min.x
        );
    }

  nk_vector_reset(
    &player_->vector,
    profile_->acceleration,
    profile_->deceleration,
    profile_->maximum
    );
}


static
void
_nk_game_initialize_player(NkGamePlayer *player_,
                           u8            character_type_,
                           u8            control_type_)
{
  memset(player_, 0, sizeof(*player_));
  player_->character_type = character_type_;
  player_->animation_bank = character_type_;
  player_->control_type = control_type_;
  player_->destination_x = -1;
  player_->destination_y = -1;
  nk_anim_cursor_reset(&player_->animation);
}


static
void
_nk_game_initialize_head(NkGamePlayer *player_,
                         u8            control_type_)
{
  memset(player_, 0, sizeof(*player_));
  player_->character_type = NK_HEAD_CHARACTER_TYPE;
  player_->animation_bank = NK_HEAD_ANIMATION_BANK;
  player_->control_type = control_type_;
  player_->destination_x = -1;
  player_->destination_y = -1;
  nk_anim_cursor_reset(&player_->animation);
}


static
void
_nk_game_set_difficulty(NkGame *game_,
                        s32     difficulty_)
{
  s32    negative;
  s32    squared;

  if(difficulty_ < 0)
    {
      difficulty_ = 0;
    }

  if(difficulty_ > NK_GAME_DIFFICULTY_LIMIT)
    {
      difficulty_ = NK_GAME_DIFFICULTY_LIMIT;
    }

  negative = 8 - difficulty_;
  squared = difficulty_ * difficulty_;
  game_->difficulty_y_miss = negative * negative;
  if(game_->players[1].character_type == NK_CHARACTER_SINAMMON)
    {
      game_->difficulty_y_miss += 7;
    }

  if(game_->players[1].character_type == NK_CHARACTER_GURDIP)
    {
      game_->difficulty_y_miss += 10;
    }

  game_->difficulty_idle_time = negative * 35 + 10;
  game_->difficulty_special_time = squared * 2 + 10;
}


bool
nk_game_configure(NkGame *game_,
                  u8      player_zero_type_,
                  u8      player_zero_control_,
                  u8      player_one_type_,
                  u8      player_one_control_,
                  s32     round_target_,
                  s32     difficulty_,
                  s32     fix_orig_bugs_,
                  u32     seed_)
{
  if((game_ == NULL) ||
     (player_zero_type_ >= NK_CHARACTER_COUNT) ||
     (player_one_type_ >= NK_CHARACTER_COUNT) ||
     (round_target_ <= 0) ||
     (fix_orig_bugs_ < 0) ||
     (fix_orig_bugs_ > 1))
    {
      return false;
    }

  if((player_zero_control_ < NK_CONTROL_HUMAN) ||
     (player_zero_control_ > NK_CONTROL_COMPUTER) ||
     (player_one_control_ < NK_CONTROL_HUMAN) ||
     (player_one_control_ > NK_CONTROL_COMPUTER))
    {
      return false;
    }

  memset(game_, 0, sizeof(*game_));
  _nk_game_initialize_player(&game_->players[0],
                             player_zero_type_,
                             player_zero_control_);
  _nk_game_initialize_player(&game_->players[1],
                             player_one_type_,
                             player_one_control_);
  _nk_game_initialize_head(&game_->ball, NK_CONTROL_BALL);
  _nk_game_initialize_head(&game_->chopper, NK_CONTROL_CHOPPER);
  game_->round_target = round_target_;
  game_->ready = 1;
  game_->multiplayer_master = 1;
  game_->sticky_blood = 1;
  game_->fix_orig_bugs = fix_orig_bugs_;
  nk_rng_seed(&game_->rng, seed_);
  _nk_game_set_difficulty(game_, difficulty_);
  return true;
}


bool
nk_game_reconfigure(NkGame *game_,
                    u8      player_zero_type_,
                    u8      player_zero_control_,
                    u8      player_one_type_,
                    u8      player_one_control_,
                    s32     round_target_,
                    s32     difficulty_,
                    s32     fix_orig_bugs_,
                    u32     seed_)
{
  u32    pain_duration[4];

  if(game_ == NULL)
    {
      return false;
    }

  pain_duration[0] = game_->players[0].pain_duration;
  pain_duration[1] = game_->players[1].pain_duration;
  pain_duration[2] = game_->ball.pain_duration;
  pain_duration[3] = game_->chopper.pain_duration;
  if(!nk_game_configure(game_,
                        player_zero_type_,
                        player_zero_control_,
                        player_one_type_,
                        player_one_control_,
                        round_target_,
                        difficulty_,
                        fix_orig_bugs_,
                        seed_))
    {
      return false;
    }

  game_->players[0].pain_duration = pain_duration[0];
  game_->players[1].pain_duration = pain_duration[1];
  game_->ball.pain_duration = pain_duration[2];
  game_->chopper.pain_duration = pain_duration[3];
  return true;
}


void
nk_game_begin_match(NkGame *game_)
{
  if(game_ == NULL)
    {
      return;
    }

  nk_effect_pool_reset(&game_->effects);
  _nk_game_reset_player(&game_->players[0]);
  _nk_game_reset_player(&game_->players[1]);
  _nk_game_reset_player(&game_->chopper);
  _nk_game_reset_player(&game_->ball);
  game_->ball.draw_me = 0U;

  game_->players[0].x = NK_GAME_PLAYER_START_X_LEFT;
  game_->players[0].y = NK_GAME_PLAYER_START_Y;
  game_->players[0].facing = 0U;
  game_->players[1].x = NK_GAME_PLAYER_START_X_RIGHT;
  game_->players[1].y = NK_GAME_PLAYER_START_Y;
  game_->players[1].facing = 1U;
  game_->ball.x = NK_GAME_BALL_START_X;
  game_->ball.y = NK_GAME_BALL_START_Y;
  game_->ball.facing = 0U;
  game_->players[0].presentation_previous_x = NK_GAME_PLAYER_START_X_LEFT * NK_SUBPIXEL_ONE;
  game_->players[0].presentation_previous_y = NK_GAME_PLAYER_START_Y * NK_SUBPIXEL_ONE;
  game_->players[1].presentation_previous_x = NK_GAME_PLAYER_START_X_RIGHT * NK_SUBPIXEL_ONE;
  game_->players[1].presentation_previous_y = NK_GAME_PLAYER_START_Y * NK_SUBPIXEL_ONE;
  game_->ball.presentation_previous_x = NK_GAME_BALL_START_X * NK_SUBPIXEL_ONE;
  game_->ball.presentation_previous_y = NK_GAME_PLAYER_START_Y * NK_SUBPIXEL_ONE;
  game_->ball.presentation_draw_me = 0U;

  _nk_game_set_character_vector(
    &game_->players[0],
    &nk_character_motion[game_->players[0].character_type],
    false
    );
  _nk_game_set_character_vector(
    &game_->players[1],
    &nk_character_motion[game_->players[1].character_type],
    true
    );
  game_->ball.vector.bound_min = nk_ball_motion.bound_min;
  game_->ball.vector.bound_max = nk_ball_motion.bound_max;
  nk_vector_reset(
    &game_->ball.vector,
    nk_ball_motion.acceleration,
    nk_ball_motion.deceleration,
    nk_ball_motion.maximum
    );

  game_->score[0] = 0;
  game_->score[1] = 0;
  game_->bar_size[0] = 0;
  game_->bar_size[1] = 0;
  game_->game_state = NK_GAME_STATE_WAITING;
  game_->game_over = 0;
  game_->winner = -1;
  game_->outcome_dialogue_kind = NK_GAME_DIALOGUE_NONE;
  game_->outcome_dialogue_index = -1;
  game_->ball_pain = 0;
  game_->electrocute = 0;
  game_->electrocute_duration = 0;
  game_->draw_priority = 0;
  game_->tick = 1U;
  game_->effect_clock_phase = 0U;
  game_->new_frame = 0U;
  nk_game_clear_events(game_);
}


void
nk_game_seed_match_directions(NkGame     *game_,
                              const u8    directions_[NK_GAME_PLAYER_COUNT])
{
  u32    index;

  if((game_ == NULL) || (directions_ == NULL))
    {
      return;
    }

  for(index = 0U; index < NK_GAME_PLAYER_COUNT; ++index)
    {
      if(game_->players[index].control_type == NK_CONTROL_HUMAN)
        {
          game_->players[index].input_stat =
            (u8)(directions_[index] & NK_DIR_MASK);
        }
      else
        {
          game_->players[index].input_stat = 0U;
        }
    }
}


void
nk_game_set_input(NkGame                *game_,
                  u8                     player_index_,
                  const nk_input_sample *input_)
{
  NkGamePlayer *player;

  if((game_ == NULL) || (input_ == NULL) ||
     (player_index_ >= NK_GAME_PLAYER_COUNT))
    {
      return;
    }

  player = &game_->players[player_index_];
  player->input_stat = input_->stat;
  player->input_buttons = input_->buttons;
  player->input_attack = input_->attack_code;
  player->input_commands = input_->commands;
}


static
int
_nk_game_generate_effect(NkGame *game_,
                         s32     type_,
                         s32     x_,
                         s32     y_,
                         s32     dx_,
                         s32     dy_,
                         u8      facing_,
                         u8      priority_)
{
  int index;

  index = nk_effect_generate(
    &game_->effects,
    &game_->rng,
    type_,
    x_,
    y_,
    dx_,
    dy_,
    facing_,
    priority_
    );
  if(index >= 0)
    {
      game_->effects.effects[index].last_source_tick = game_->tick;
    }

  return index;
}


/* Watcom emits these calls right-to-left; make argument RNG order explicit. */
static
void
_nk_game_random_effect(NkGame *game_,
                       s32     type_,
                       s32     x_,
                       s32     y_,
                       s32     dx_limit_,
                       s32     dx_bias_,
                       s32     dy_limit_,
                       s32     dy_bias_,
                       u8      facing_)
{
  u8    priority;
  s32    dy;
  s32    dx;

  priority = (u8)nk_rng_bounded(&game_->rng, 2);
  if(dy_limit_ > 0)
    {
      dy = nk_rng_bounded(&game_->rng, dy_limit_) + dy_bias_;
    }
  else
    {
      dy = dy_bias_;
    }

  if(dx_limit_ > 0)
    {
      dx = nk_rng_bounded(&game_->rng, dx_limit_) + dx_bias_;
    }
  else
    {
      dx = dx_bias_;
    }

  _nk_game_generate_effect(game_, type_, x_, y_, dx, dy, facing_, priority);
}


static
void
_nk_game_hit_spray(NkGame             *game_,
                   const NkGamePlayer *attacker_,
                   int                 super_move_)
{
  s32    x;
  s32    y;
  u8    reverse;
  u8    priority;
  int index;
  int count;

  x = game_->ball.x;
  y = game_->ball.y;
  reverse = (u8)(attacker_->facing ^ 1U);
  if(!super_move_)
    {
      priority = (u8)nk_rng_bounded(&game_->rng, 2);
      _nk_game_generate_effect(
        game_, 25, x, y, 0, 0, attacker_->facing, priority
        );
      count = 15;
    }
  else
    {
      count = 25;
    }

  for(index = 0; index < count; ++index)
    {
      _nk_game_random_effect(game_, 16, x, y, 20, 0, 0, 0, reverse);
    }

  count = super_move_ ? 10 : 5;
  for(index = 0; index < count; ++index)
    {
      _nk_game_random_effect(game_, 10, x, y, 20, 0, 0, 0, reverse);
      _nk_game_random_effect(game_, 18, x, y, 20, -10, 20, -10, reverse);
      if(!super_move_)
        {
          _nk_game_random_effect(game_, 25, x, y, 30, -15, 30, 0, reverse);
        }
    }

  if(super_move_)
    {
      _nk_game_random_effect(game_, 25, x, y, 30, -15, 30, 0, reverse);
      _nk_game_generate_effect(
        game_, 2, x, y, 0, 0, attacker_->facing, 1U
        );
    }
  else
    {
      _nk_game_generate_effect(
        game_, 1, x, y, 0, 0, attacker_->facing, 1U
        );
    }
}


static
void
_nk_game_tap_hit(NkGamePlayer *attacker_,
                 NkGamePlayer *ball_,
                 s32           delta_y_)
{
  s32    delta_velocity;
  s32    amount;
  int same_sign;

  if(!attacker_->control_ball)
    {
      attacker_->energy +=
        nk_character_energy[attacker_->character_type][0];
    }

  if(attacker_->energy > NK_GAME_ENERGY_LIMIT)
    {
      attacker_->energy = NK_GAME_ENERGY_LIMIT;
    }

  delta_velocity = nk_wrap_sub(
    -(delta_y_ * 0x400),
    ball_->vector.velocity.y / 16
    );
  same_sign = (delta_velocity > 0 && ball_->vector.velocity.y > 0) ||
              (delta_velocity < 0 && ball_->vector.velocity.y < 0);
  if(same_sign)
    {
      amount = _nk_game_abs(delta_velocity / 16) + 0x650;
    }
  else
    {
      amount = _nk_game_abs(delta_velocity / 8) + 0x800;
    }

  nk_vector_speed_x_acceleration(&ball_->vector, amount);
  ball_->vector.velocity.y = nk_wrap_add(
    ball_->vector.velocity.y,
    delta_velocity
    );
}


static
void
_nk_game_special_hit(NkGamePlayer *attacker_,
                     NkGamePlayer *ball_)
{
  switch(attacker_->character_type)
    {
    case NK_CHARACTER_KLUBBOR:
      nk_vector_speed_x_acceleration(&ball_->vector, 0x2500);
      ball_->vector.velocity.y = nk_wrap_add(
        ball_->vector.velocity.y, 0x2500
        ) / 2;
      break;
    case NK_CHARACTER_HENRY:
      _nk_game_request_move(attacker_, NK_MOVE_HOLD);
      if(attacker_->facing == 0U)
        {
          ball_->vector.velocity.x = 0x20000;
        }
      else
        {
          ball_->vector.velocity.x = -0x20000;
        }

      ball_->vector.velocity.y = 0;
      break;
    case NK_CHARACTER_SINAMMON:
      if(attacker_->move == NK_MOVE_SPECIAL_ONE)
        {
          if(ball_->vector.velocity.y > 0)
            {
              ball_->vector.velocity.y =
                nk_wrap_neg(ball_->vector.velocity.y);
            }

          ball_->vector.velocity.y = nk_wrap_sub(
            ball_->vector.velocity.y, 0x5000
            );
        }

      if(attacker_->move == NK_MOVE_HOLD)
        {
          if(ball_->vector.velocity.y < 0)
            {
              ball_->vector.velocity.y =
                nk_wrap_neg(ball_->vector.velocity.y);
            }

          ball_->vector.velocity.y = nk_wrap_add(
            ball_->vector.velocity.y, 0x5000
            );
        }

      break;
    default:
      break;
    }
}


static
void
_nk_game_super_hit(NkGamePlayer *attacker_,
                   NkGamePlayer *ball_)
{
  switch(attacker_->character_type)
    {
    case NK_CHARACTER_KLUBBOR:
      nk_vector_speed_x_acceleration(&ball_->vector, 0x19000);
      ball_->vector.velocity.y = nk_wrap_add(
        ball_->vector.velocity.y, 0x3000
        ) / 4;
      break;
    case NK_CHARACTER_ED:
      ball_->freeze = 10;
      _nk_game_request_move(attacker_, NK_MOVE_HOLD);
      break;
    default:
      break;
    }
}


bool
nk_game_try_ball_hit(NkGame *game_,
                     u8      player_index_)
{
  NkGamePlayer *attacker;
  NkGamePlayer *opponent;
  NkGamePlayer *ball;
  const NkAnimFrame *attacker_frame;
  const NkAnimFrame *ball_frame;
  NkCollisionContact contact;
  s32    scream;

  if((game_ == NULL) || (player_index_ >= NK_GAME_PLAYER_COUNT))
    {
      return false;
    }

  attacker = &game_->players[player_index_];
  opponent = &game_->players[player_index_ ^ 1U];
  ball = &game_->ball;
  attacker_frame = nk_game_player_frame(attacker);
  ball_frame = nk_game_player_frame(ball);
  if((attacker_frame == NULL) || (ball_frame == NULL) ||
     ((attacker->status & NK_PLAYER_DONE_ATTACK) != 0) ||
     (game_->game_state != NK_GAME_STATE_ACTIVE) ||
     (attacker->facing == ball->facing))
    {
      return false;
    }

  if(!nk_collision_first_contact(
       attacker_frame,
       attacker->x,
       attacker->y,
       attacker->facing,
       ball_frame,
       ball->x,
       ball->y,
       ball->facing,
       &contact))
    {
      return false;
    }

  ball->stuck = 0;
  if(opponent->move == NK_MOVE_HOLD)
    {
      _nk_game_request_move(opponent, NK_MOVE_RELEASE);
    }

  ball->facing ^= 1U;
  ball->vector.velocity.x = nk_wrap_neg(ball->vector.velocity.x);
  ball->vector.acceleration.x = nk_wrap_neg(ball->vector.acceleration.x);
  if(attacker->control_type == NK_CONTROL_REMOTE)
    {
      return true;
    }

  attacker->status |= NK_PLAYER_DONE_ATTACK;
  attacker->vector.velocity.x = nk_wrap_sub(
    attacker->vector.velocity.x,
    _nk_game_half(ball->vector.velocity.x)
    );
  ball->vector.position.x = nk_wrap_add(
    ball->vector.position.x,
    contact.separation_x_fixed
    );
  ball->freeze = 0;

  if((attacker->move == NK_MOVE_SPECIAL_ONE) ||
     ((attacker->character_type == NK_CHARACTER_SINAMMON) &&
         (attacker->move == NK_MOVE_HOLD)))
    {
      _nk_game_hit_spray(game_, attacker, false);
      game_->ball_pain += 3;
      _nk_game_special_hit(attacker, ball);
    }
  else if(attacker->move == NK_MOVE_SPECIAL_TWO)
    {
      _nk_game_hit_spray(game_, attacker, true);
      game_->ball_pain += 6;
      _nk_game_super_hit(attacker, ball);
    }
  else
    {
      _nk_game_generate_effect(
        game_, 0, ball->x, ball->y, 0, 0, attacker->facing, 1U
        );
      if(attacker->dizzy_duration == 0)
        {
          _nk_game_request_move(attacker, NK_MOVE_TAP_RECOVERY);
        }

      game_->ball_pain++;
      _nk_game_tap_hit(
        attacker, ball, contact.attack_center_y_relative
        );
    }

  scream = nk_rng_bounded(&game_->rng, 4) + 1;
  _nk_game_event(
    game_,
    NK_GAME_EVENT_SOUND,
    NK_GAME_ACTOR_BALL,
    (u8)((NK_ANIM_SOUND_PLAYER_FLAG | scream) << NK_ANIM_SOUND_CODE_BITS),
    1U,
    ball->x,
    ball->y
    );
  _nk_game_request_move(ball, NK_MOVE_PROJECTILE_HIT);
  ball->pain = game_->ball_pain / 8;
  game_->players[0].destination_y = -1;
  game_->players[1].destination_y = -1;
  return true;
}


bool
nk_game_try_pain_drip(NkGame       *game_,
                      NkGamePlayer *owner_,
                      int           paused_,
                      s32           x_,
                      s32           y_,
                      s32           image_width_,
                      s32           image_height_)
{
  if((game_ == NULL) || (owner_ == NULL) || (owner_->energy == 0) ||
     (owner_->pain_duration >= game_->tick))
    {
      return false;
    }

  if(((nk_rng_next(&game_->rng) & 3U) != 0U) || (paused_))
    {
      return false;
    }

  owner_->pain_duration = game_->tick + 50U;
  return _nk_game_generate_effect(
    game_,
    10,
    x_,
    y_,
    image_width_ / 2,
    image_height_ + 5,
    0U,
    0U
    ) >= 0;
}


static
bool
_nk_game_try_player_hit(NkGame *game_,
                        u8      player_index_)
{
  NkGamePlayer *attacker;
  NkGamePlayer *opponent;
  const NkAnimFrame *attacker_frame;
  const NkAnimFrame *opponent_frame;
  NkCollisionContact contact;

  attacker = &game_->players[player_index_];
  opponent = &game_->players[player_index_ ^ 1U];
  attacker_frame = nk_game_player_frame(attacker);
  opponent_frame = nk_game_player_frame(opponent);
  if((attacker_frame == NULL) || (opponent_frame == NULL) ||
     ((attacker->status & NK_PLAYER_DONE_ATTACK) != 0))
    {
      return false;
    }

  if(!nk_collision_first_contact(
       attacker_frame,
       attacker->x,
       attacker->y,
       attacker->facing,
       opponent_frame,
       opponent->x,
       opponent->y,
       opponent->facing,
       &contact))
    {
      return false;
    }

  attacker->status |= NK_PLAYER_DONE_ATTACK;
  opponent->move = NK_MOVE_STANCE;
  opponent->new_move = 0U;
  if(attacker->facing == 0U)
    {
      attacker->vector.position.x = nk_fixed_from_int(260);
      opponent->vector.position.x = nk_fixed_from_int(330);
    }
  else
    {
      attacker->vector.position.x = nk_fixed_from_int(60);
      opponent->vector.position.x = nk_fixed_from_int(-10);
    }

  _nk_game_request_move(attacker, NK_MOVE_PROJECTILE_HIT);
  return true;
}


static
bool
_nk_game_create_projectile(NkGame *game_,
                           u8      player_index_,
                           s32     x_,
                           s32     y_)
{
  NkGamePlayer *owner;
  NkProjectile *projectile;
  int index;

  owner = &game_->players[player_index_];
  for(index = 0; index < NK_GAME_PROJECTILE_COUNT; ++index)
    {
      projectile = &owner->projectiles[index];
      if(projectile->active == 0U)
        {
          if(!nk_anim_cursor_start(
               &projectile->animation,
               owner->animation_bank,
               NK_MOVE_PROJECTILE_FLY))
            {
              return false;
            }

          projectile->active = 1U;
          projectile->state = 0U;
          projectile->type = owner->character_type;
          projectile->facing = owner->facing;
          projectile->x = x_;
          projectile->y = y_;
          projectile->fraction_x = 0;
          projectile->fraction_y = 0;
          owner->projectile_previous_x[index] = x_ * 256;
          owner->projectile_previous_y[index] = y_ * 256;
          owner->projectile_interpolation_ready[index] = 0U;
          projectile->shake = 0;
          projectile->stick_x = 0;
          projectile->stick_y = 0;
          owner->projectile_exists = 1U;
          _nk_game_event(
            game_,
            NK_GAME_EVENT_PROJECTILE,
            player_index_,
            owner->character_type,
            0U,
            x_,
            y_
            );
          return true;
        }
    }

  return false;
}


static
void
_nk_game_stop_projectile(NkGamePlayer *owner_,
                         NkProjectile *projectile_)
{
  if(projectile_->state == NK_PROJECTILE_STATE_ATTACHED)
    {
      owner_->control_ball = 0;
    }

  projectile_->active = 0U;
  nk_anim_cursor_stop(&projectile_->animation);
}


static
bool
_nk_game_start_projectile_move(NkGamePlayer *owner_,
                               NkProjectile *projectile_,
                               u32           move_)
{
  if(!nk_anim_cursor_start(
       &projectile_->animation, owner_->animation_bank, move_))
    {
      _nk_game_stop_projectile(owner_, projectile_);
      return false;
    }

  return true;
}


static
void
_nk_game_projectile_hit_opponent(NkGame       *game_,
                                 u8            owner_index_,
                                 NkProjectile *projectile_,
                                 NkGamePlayer *opponent_)
{
  NkGamePlayer *owner;

  owner = &game_->players[owner_index_];
  _nk_game_start_projectile_move(
    owner, projectile_, NK_MOVE_PROJECTILE_HIT
    );
  projectile_->stick_x = projectile_->x - opponent_->x;
  projectile_->stick_y = projectile_->y - opponent_->y;
  projectile_->state = NK_PROJECTILE_STATE_HIT_PLAYER;

  switch(projectile_->type)
    {
    case NK_CHARACTER_HENRY:
      opponent_->vector.position.y = nk_wrap_add(
        opponent_->vector.position.y, 0x3a0000
        );
      if(opponent_->y > 100)
        {
          opponent_->vector.velocity.y = -0x0a000;
        }
      else
        {
          opponent_->vector.velocity.y = 0x0a000;
        }

      if(opponent_->facing == 0U)
        {
          opponent_->vector.position.x = nk_wrap_sub(
            opponent_->vector.position.x, 0x200000
            );
          opponent_->vector.velocity.x = -0x10000;
        }
      else
        {
          opponent_->vector.position.x = nk_wrap_add(
            opponent_->vector.position.x, 0x200000
            );
          opponent_->vector.velocity.x = 0x10000;
        }

      _nk_game_request_move(opponent_, NK_MOVE_STANCE);
      break;
    case NK_CHARACTER_SINAMMON:
      opponent_->vector.position.y = nk_wrap_add(
        opponent_->vector.position.y, 0x0a0000
        );
      if(projectile_->y > opponent_->y)
        {
          opponent_->vector.velocity.y = -0x60000;
        }
      else
        {
          opponent_->vector.velocity.y = 0x60000;
        }

      if(opponent_->facing == 0U)
        {
          opponent_->vector.position.x = nk_wrap_sub(
            opponent_->vector.position.x, 0x020000
            );
          opponent_->vector.velocity.x = -0x1000;
        }
      else
        {
          opponent_->vector.position.x = nk_wrap_add(
            opponent_->vector.position.x, 0x020000
            );
          opponent_->vector.velocity.x = 0x1000;
        }

      _nk_game_request_move(opponent_, NK_MOVE_STANCE);
      break;
    case NK_CHARACTER_GONZOLES:
      opponent_->energy -= 35;
      if(opponent_->energy < 0)
        {
          opponent_->energy = 0;
        }

      break;
    case NK_CHARACTER_GURDIP:
      opponent_->dizzy_duration = 500;
      if(opponent_->facing == 0U)
        {
          opponent_->vector.position.x = opponent_->vector.bound_max.x;
        }
      else
        {
          opponent_->vector.position.x = opponent_->vector.bound_min.x;
        }

      _nk_game_request_move(opponent_, NK_MOVE_DIZZY);
      break;
    case NK_CHARACTER_ED:
      if(projectile_->facing == 0U)
        {
          projectile_->stick_x += 20;
        }
      else
        {
          projectile_->stick_x -= 20;
        }

      opponent_->vector.position.y = nk_wrap_add(
        opponent_->vector.position.y, 0x010000
        );
      if(game_->ball.y > opponent_->y)
        {
          opponent_->vector.velocity.y = -0x6000;
        }
      else
        {
          opponent_->vector.velocity.y = 0x6000;
        }

      if(opponent_->facing == 0U)
        {
          opponent_->vector.position.x = nk_wrap_sub(
            opponent_->vector.position.x, 0x020000
            );
          opponent_->vector.velocity.x = -0x1000;
        }
      else
        {
          opponent_->vector.position.x = nk_wrap_add(
            opponent_->vector.position.x, 0x020000
            );
          opponent_->vector.velocity.x = 0x1000;
        }

      break;
    default:
      break;
    }
}


static
void
_nk_game_release_opponent_hold(NkGamePlayer *opponent_)
{
  if((opponent_->character_type == NK_CHARACTER_ED) &&
     (opponent_->move == NK_MOVE_HOLD))
    {
      _nk_game_request_move(opponent_, NK_MOVE_STANCE);
    }
}


static
void
_nk_game_projectile_hit_ball(NkGame                   *game_,
                             u8                        owner_index_,
                             NkProjectile             *projectile_,
                             NkGamePlayer             *opponent_,
                             const NkCollisionContact *contact_)
{
  NkGamePlayer *owner;
  NkGamePlayer *ball;

  owner = &game_->players[owner_index_];
  ball = &game_->ball;
  _nk_game_start_projectile_move(
    owner, projectile_, NK_MOVE_PROJECTILE_REPEAT
    );
  projectile_->stick_x = projectile_->x - ball->x;
  projectile_->stick_y = projectile_->y - ball->y;
  projectile_->state = NK_PROJECTILE_STATE_HIT_BALL;

  switch(projectile_->type)
    {
    case NK_CHARACTER_ED:
      if(projectile_->facing != ball->facing)
        {
          ball->facing ^= 1U;
          ball->vector.velocity.x =
            nk_wrap_neg(ball->vector.velocity.x);
          ball->vector.acceleration.x =
            nk_wrap_neg(ball->vector.acceleration.x);
        }

      nk_vector_speed_x_acceleration(&ball->vector, 0x2000);
      ball->vector.velocity.y = nk_wrap_add(
        -(contact_->attack_center_y_relative * 0x1500),
        _nk_game_half(ball->vector.velocity.y)
        );
      opponent_->destination_y = -1;
      ball->stuck = 2;
      ball->freeze = 0;
      break;
    case NK_CHARACTER_GURDIP:
      projectile_->stick_x = 0;
      projectile_->stick_y = 0;
      projectile_->state = NK_PROJECTILE_STATE_ATTACHED;
      ball->stuck = 0;
      _nk_game_release_opponent_hold(opponent_);
      ball->freeze = 0;
      break;
    case NK_CHARACTER_GONZOLES:
      if(projectile_->facing == 0U)
        {
          ball->vector.position.x = nk_wrap_add(
            ball->vector.position.x, nk_fixed_from_int(15)
            );
        }
      else
        {
          ball->vector.position.x = nk_wrap_sub(
            ball->vector.position.x, nk_fixed_from_int(15)
            );
        }

      break;
    case NK_CHARACTER_SINAMMON:
      projectile_->state = NK_PROJECTILE_STATE_ATTACHED;
      _nk_game_release_opponent_hold(opponent_);
      ball->freeze = 0;
      if(projectile_->facing != ball->facing)
        {
          ball->facing ^= 1U;
          ball->vector.velocity.x =
            nk_wrap_neg(ball->vector.velocity.x);
          ball->vector.acceleration.x =
            nk_wrap_neg(ball->vector.acceleration.x);
        }

      ball->vector.velocity.x /= 2;
      nk_vector_speed_x_velocity(&ball->vector, 0x10000);
      ball->vector.velocity.y /= 3;
      ball->freeze = 5;
      break;
    case NK_CHARACTER_BUDDY:
      nk_vector_speed_x_acceleration(&ball->vector, 0x1000);
      if(projectile_->facing != ball->facing)
        {
          ball->facing ^= 1U;
          ball->vector.velocity.x =
            nk_wrap_neg(ball->vector.velocity.x);
          ball->vector.acceleration.x =
            nk_wrap_neg(ball->vector.acceleration.x);
        }

      ball->vector.velocity.y = nk_wrap_add(
        -(contact_->attack_center_y_relative * 0x1500),
        _nk_game_half(ball->vector.velocity.y)
        );
      opponent_->destination_y = -1;
      _nk_game_release_opponent_hold(opponent_);
      break;
    default:
      break;
    }
}


static
void
_nk_game_projectile_collisions(NkGame       *game_,
                               u8            owner_index_,
                               NkProjectile *projectile_)
{
  NkGamePlayer *opponent;
  const NkAnimFrame *projectile_frame;
  const NkAnimFrame *defender_frame;
  NkCollisionContact contact;

  if((projectile_->state != NK_PROJECTILE_STATE_FLYING) ||
     (game_->game_state != NK_GAME_STATE_ACTIVE))
    {
      return;
    }

  opponent = &game_->players[owner_index_ ^ 1U];
  projectile_frame = nk_anim_cursor_frame(&projectile_->animation);
  defender_frame = nk_game_player_frame(opponent);
  if((projectile_->type != NK_CHARACTER_BUDDY) &&
     (nk_collision_first_contact(
           projectile_frame,
           projectile_->x,
           projectile_->y,
           projectile_->facing,
           defender_frame,
           opponent->x,
           opponent->y,
           opponent->facing,
           &contact)))
    {
      _nk_game_projectile_hit_opponent(
        game_, owner_index_, projectile_, opponent
        );
    }

  if((projectile_->state == NK_PROJECTILE_STATE_FLYING) &&
     (projectile_->type != NK_CHARACTER_HENRY) &&
     (projectile_->type != NK_CHARACTER_FETUS))
    {
      projectile_frame = nk_anim_cursor_frame(&projectile_->animation);
      defender_frame = nk_game_player_frame(&game_->ball);
      if(nk_collision_first_contact(
           projectile_frame,
           projectile_->x,
           projectile_->y,
           projectile_->facing,
           defender_frame,
           game_->ball.x,
           game_->ball.y,
           game_->ball.facing,
           &contact))
        {
          _nk_game_projectile_hit_ball(
            game_, owner_index_, projectile_, opponent, &contact
            );
        }
    }
}


static
void
_nk_game_projectile_blood(NkGame             *game_,
                          const NkProjectile *projectile_)
{
  u8    reverse;
  int index;

  reverse = (u8)(projectile_->facing ^ 1U);
  for(index = 0; index < 15; ++index)
    {
      _nk_game_random_effect(
        game_, 16, projectile_->x, projectile_->y,
        20, 0, 0, 0, reverse
        );
    }

  for(index = 0; index < 5; ++index)
    {
      _nk_game_random_effect(
        game_, 10, projectile_->x, projectile_->y,
        20, 0, 0, 0, reverse
        );
      _nk_game_random_effect(
        game_, 18, projectile_->x, projectile_->y,
        20, -10, 20, -10, reverse
        );
      _nk_game_random_effect(
        game_, 25, projectile_->x, projectile_->y,
        30, -15, 30, 0, reverse
        );
    }

  _nk_game_generate_effect(
    game_, 1, projectile_->x, projectile_->y,
    0, 0, projectile_->facing, 1U
    );
}


static
void
_nk_game_projectile_attachment(NkGame       *game_,
                               u8            owner_index_,
                               NkProjectile *projectile_)
{
  NkGamePlayer *owner;
  NkGamePlayer *opponent;
  const NkAnimFrame *frame;
  s32    slow_x;
  s32    slow_y;

  owner = &game_->players[owner_index_];
  opponent = &game_->players[owner_index_ ^ 1U];
  frame = nk_anim_cursor_frame(&projectile_->animation);
  if((projectile_->state == NK_PROJECTILE_STATE_HIT_PLAYER) && (frame != NULL) &&
     ((projectile_->type == NK_CHARACTER_FETUS) || (projectile_->type == NK_CHARACTER_ED)) &&
     (nk_anim_frame_delta_y(frame) == 0))
    {
      projectile_->x = opponent->x + projectile_->stick_x;
      projectile_->y = opponent->y + projectile_->stick_y;
      if(projectile_->type == NK_CHARACTER_FETUS)
        {
          slow_x = (
            nk_character_motion[opponent->character_type].acceleration.x
            - nk_character_motion[opponent->character_type].deceleration.x
            ) / 2;
          slow_y = (
            nk_character_motion[opponent->character_type].acceleration.y
            - nk_character_motion[opponent->character_type].deceleration.y
            ) / 2;
          nk_vector_slow_x_velocity(&opponent->vector, slow_x);
          nk_vector_slow_y_velocity(&opponent->vector, slow_y);
          if((opponent->vector.velocity.y < 0) &&
             (projectile_->shake >= 0))
            {
              projectile_->shake = -1;
              projectile_->animation.remaining -= 20;
            }
          else if((opponent->vector.velocity.y > 0) &&
                  (projectile_->shake <= 0))
            {
              projectile_->shake = 1;
              projectile_->animation.remaining -= 20;
            }
        }

      if((projectile_->type == NK_CHARACTER_ED) && ((game_->tick & 3U) == 0U))
        {
          _nk_game_projectile_blood(game_, projectile_);
          if(opponent->energy > 0)
            {
              opponent->energy--;
            }
        }
    }

  if(projectile_->state == NK_PROJECTILE_STATE_ATTACHED)
    {
      if(projectile_->type == NK_CHARACTER_GURDIP)
        {
          projectile_->x = game_->ball.x + projectile_->stick_x;
          projectile_->y = game_->ball.y + projectile_->stick_y;
          owner->control_ball = 1;
          if((owner->energy != 0) && (game_->ball.stuck == 0))
            {
              if((game_->tick & 3U) == 0U)
                {
                  owner->energy--;
                }
            }
          else
            {
              owner->control_ball = 0;
              _nk_game_stop_projectile(owner, projectile_);
              return;
            }

          if((owner->input_buttons & NK_BUTTON_ONE) != 0U)
            {
              opponent->destination_y = -1;
              if((owner->input_stat & NK_DIR_LEFT) != 0U)
                {
                  game_->ball.vector.position.x = nk_wrap_sub(
                    game_->ball.vector.position.x, 0x15000
                    );
                }

              if((owner->input_stat & NK_DIR_RIGHT) != 0U)
                {
                  game_->ball.vector.position.x = nk_wrap_add(
                    game_->ball.vector.position.x, 0x15000
                    );
                }

              if((owner->input_stat & NK_DIR_UP) != 0U)
                {
                  game_->ball.vector.position.y = nk_wrap_sub(
                    game_->ball.vector.position.y, 0x15000
                    );
                }

              if((owner->input_stat & NK_DIR_DOWN) != 0U)
                {
                  game_->ball.vector.position.y = nk_wrap_add(
                    game_->ball.vector.position.y, 0x15000
                    );
                }

              if(game_->ball.vector.position.y < nk_fixed_from_int(NK_GAME_ARENA_TOP))
                {
                  game_->ball.vector.position.y = nk_fixed_from_int(NK_GAME_ARENA_TOP);
                }

              if(game_->ball.vector.position.y > nk_fixed_from_int(NK_GAME_ARENA_BOTTOM))
                {
                  game_->ball.vector.position.y = nk_fixed_from_int(NK_GAME_ARENA_BOTTOM);
                }
            }
        }

      if((projectile_->type == 5U) && (game_->ball.freeze > 0))
        {
          if((game_->tick & 1U) != 0U)
            {
              if(((owner->input_stat & NK_DIR_UP) != 0U) &&
                 (projectile_->y > 30))
                {
                  projectile_->y--;
                }

              if(((owner->input_stat & NK_DIR_DOWN) != 0U) &&
                 (projectile_->y < 190))
                {
                  projectile_->y++;
                }
            }

          if(projectile_->facing == 0U)
            {
              game_->ball.vector.position.x =
                nk_fixed_from_int(projectile_->x + 10);
            }
          else
            {
              game_->ball.vector.position.x =
                nk_fixed_from_int(projectile_->x - 10);
            }

          game_->ball.vector.position.y =
            nk_fixed_from_int(projectile_->y - 10);
          game_->ball.freeze++;
          opponent->destination_y = -1;
        }
    }
}


static
void
_nk_game_move_projectile(NkGamePlayer *owner_,
                         NkProjectile *projectile_)
{
  const NkAnimFrame *frame;
  s32    delta;

  frame = nk_anim_cursor_frame(&projectile_->animation);
  if(frame == NULL)
    {
      _nk_game_stop_projectile(owner_, projectile_);
      return;
    }

  delta = nk_anim_frame_delta_x(frame);
  if(projectile_->facing == 0U)
    {
      projectile_->fraction_x = nk_wrap_add(
        projectile_->fraction_x, delta
        );
    }
  else
    {
      projectile_->fraction_x = nk_wrap_sub(
        projectile_->fraction_x, delta
        );
    }

  projectile_->fraction_y = nk_wrap_add(
    projectile_->fraction_y, nk_anim_frame_delta_y(frame)
    );
  projectile_->x += nk_floor_shift_right(projectile_->fraction_x, 8U);
  projectile_->y += nk_floor_shift_right(projectile_->fraction_y, 8U);
  projectile_->fraction_x =
    (s32)((u32)projectile_->fraction_x & 0xffU);
  projectile_->fraction_y =
    (s32)((u32)projectile_->fraction_y & 0xffU);
}


static
void
_nk_game_advance_projectile(NkGame       *game_,
                            u8            owner_index_,
                            NkProjectile *projectile_)
{
  NkGamePlayer *owner;
  const NkAnimFrame *frame;
  int step;

  owner = &game_->players[owner_index_];
  if(projectile_->active == 0U)
    {
      return;
    }

  if((projectile_->state == NK_PROJECTILE_STATE_FLYING) &&
     (projectile_->type == NK_CHARACTER_BUDDY))
    {
      if(projectile_->shake < 200)
        {
          if((owner->input_buttons & NK_BUTTON_ONE) != 0U)
            {
              if(projectile_->facing == 0U)
                {
                  projectile_->x++;
                }
              else
                {
                  projectile_->x--;
                }

              projectile_->shake++;
              owner->animation.remaining++;
            }
          else
            {
              projectile_->shake = 200;
            }
        }
      else
        {
          projectile_->shake++;
          if(projectile_->shake > 2000)
            {
              _nk_game_start_projectile_move(
                owner, projectile_, NK_MOVE_PROJECTILE_REPEAT
                );
              projectile_->state = NK_PROJECTILE_STATE_HIT_PLAYER;
            }
        }
    }

  _nk_game_projectile_collisions(game_, owner_index_, projectile_);
  _nk_game_projectile_attachment(game_, owner_index_, projectile_);
  if(projectile_->active == 0U)
    {
      return;
    }

  _nk_game_move_projectile(owner, projectile_);
  if(projectile_->active == 0U)
    {
      return;
    }

  if((projectile_->x > 380) || (projectile_->x < -60))
    {
      _nk_game_stop_projectile(owner, projectile_);
      return;
    }

  step = nk_anim_cursor_tick(&projectile_->animation);
  if(step == NK_ANIM_STEP_COMPLETE)
    {
      if(projectile_->state == NK_PROJECTILE_STATE_FLYING)
        {
          _nk_game_start_projectile_move(
            owner, projectile_, NK_MOVE_PROJECTILE_FLY
            );
        }
      else if(projectile_->state == NK_PROJECTILE_STATE_ATTACHED)
        {
          _nk_game_start_projectile_move(
            owner, projectile_, NK_MOVE_PROJECTILE_REPEAT
            );
        }
      else
        {
          _nk_game_stop_projectile(owner, projectile_);
          return;
        }
    }

  frame = nk_anim_cursor_frame(&projectile_->animation);
  if((step != NK_ANIM_STEP_NONE) && (frame != NULL) &&
     (frame->sound_effect_bits != 0U))
    {
      _nk_game_event(
        game_,
        NK_GAME_EVENT_SOUND,
        owner_index_,
        frame->sound_effect_bits,
        0U,
        projectile_->x,
        projectile_->y
        );
    }
}


static
void
_nk_game_update_projectiles(NkGame *game_,
                            u8      owner_index_)
{
  NkGamePlayer *owner;
  int index;
  int active;

  owner = &game_->players[owner_index_];
  active = 0;
  for(index = 0; index < NK_GAME_PROJECTILE_COUNT; ++index)
    {
      if(owner->projectiles[index].active != 0U)
        {
          owner->projectile_previous_x[index] =
            owner->projectiles[index].x * 256 +
            (s32)((u32)owner->projectiles[index].fraction_x & 0xffU);
          owner->projectile_previous_y[index] =
            owner->projectiles[index].y * 256 +
            (s32)((u32)owner->projectiles[index].fraction_y & 0xffU);
          owner->projectile_interpolation_ready[index] = 1U;
        }

      _nk_game_advance_projectile(
        game_, owner_index_, &owner->projectiles[index]
        );
      if(owner->projectiles[index].active != 0U)
        {
          active = 1;
        }
    }

  owner->projectile_exists = (u8)active;
}


static
void
_nk_game_new_frame(NkGame       *game_,
                   NkGamePlayer *player_,
                   u8            actor_)
{
  const NkAnimFrame *frame;
  NkAnimProjectedAttachment attachment;

  game_->new_frame = 1U;
  player_->status &= ~(s32)NK_PLAYER_DONE_ATTACK;
  frame = nk_game_player_frame(player_);
  if(frame == NULL)
    {
      return;
    }

  if((player_->control_type <= NK_CONTROL_COMPUTER) &&
     ((frame->parameter_bits & 0x0100U) != 0U) &&
     (nk_anim_project_attachment(
           frame,
           player_->x,
           player_->y,
           player_->facing,
           &attachment)) &&
     ((attachment.kind_flags >> 4) == 6U) &&
     (actor_ < NK_GAME_PLAYER_COUNT))
    {
      _nk_game_create_projectile(
        game_, actor_, attachment.x, attachment.y
        );
    }

  if((game_->game_over < 3) && (frame->sound_effect_bits != 0U))
    {
      _nk_game_event(
        game_,
        NK_GAME_EVENT_SOUND,
        actor_,
        frame->sound_effect_bits,
        0U,
        player_->x,
        player_->y
        );
    }
}


static
void
_nk_game_complete_move(NkGame       *game_,
                       NkGamePlayer *player_,
                       u8            actor_)
{
  if(player_->control_type == NK_CONTROL_CHOPPER)
    {
      player_->draw_me = 0U;
    }
  else if(player_->control_type == NK_CONTROL_BALL)
    {
      player_->move = NK_MOVE_PROJECTILE_REPEAT;
    }
  else if(player_->move >= NK_MOVE_WIN)
    {
      if((player_->move < NK_MOVE_TAP_RECOVERY) &&
         (player_->control_type != NK_CONTROL_REMOTE))
        {
          if(game_->game_state < NK_GAME_STATE_ROUND_COMPLETE)
            {
              game_->game_state++;
            }
        }

      if(player_->move == NK_MOVE_HOLD)
        {
          if(player_->character_type == NK_CHARACTER_ED)
            {
              player_->move = NK_MOVE_HOLD;
            }
          else
            {
              player_->move = NK_MOVE_RELEASE;
            }
        }
      else if(player_->dizzy_duration > 0)
        {
          player_->move = NK_MOVE_DIZZY;
        }
      else if(!game_->game_over)
        {
          player_->move = NK_MOVE_STANCE;
        }
      else
        {
          game_->game_over |= (s32)actor_ + 1;
        }
    }

  _nk_game_start_animation(player_, player_->move);
}


static
void
_nk_game_advance_animation(NkGame       *game_,
                           NkGamePlayer *player_,
                           u8            actor_)
{
  int step;

  if(player_->new_move != 0U)
    {
      _nk_game_start_animation(player_, player_->move);
      _nk_game_new_frame(game_, player_, actor_);
      return;
    }

  step = nk_anim_cursor_tick(&player_->animation);
  if(step == NK_ANIM_STEP_NONE)
    {
      return;
    }

  if(step == NK_ANIM_STEP_FRAME)
    {
      if((player_->character_type == NK_CHARACTER_GURDIP) &&
         (player_->move == NK_MOVE_SPECIAL_TWO) &&
         (player_->animation.frame_index == 9U))
        {
          player_->vector.position.y = game_->ball.vector.position.y;
        }

      _nk_game_new_frame(game_, player_, actor_);
      return;
    }

  if(step == NK_ANIM_STEP_COMPLETE)
    {
      _nk_game_complete_move(game_, player_, actor_);
      _nk_game_new_frame(game_, player_, actor_);
    }
}


static
s32
_nk_game_ai_x(s32    value_,
              u8     player_index_)
{
  if(player_index_ == 0U)
    {
      return NK_LOGICAL_WIDTH - value_;
    }

  return value_;
}


static
u8
_nk_game_ai_ball_facing(const NkGame *game_,
                        u8            player_index_)
{
  return (u8)(game_->ball.facing
                 ^ (player_index_ == 0U ? 1U : 0U));
}


static
s32
_nk_game_ai_y_miss(const NkGame *game_,
                   u8            player_index_)
{
  s32    adjustment;
  s32    player_one_adjustment;

  adjustment = 0;
  if(game_->players[player_index_].character_type == NK_CHARACTER_SINAMMON)
    {
      adjustment = 7;
    }
  else if(game_->players[player_index_].character_type == NK_CHARACTER_GURDIP)
    {
      adjustment = 10;
    }

  player_one_adjustment = 0;
  if(game_->players[1].character_type == NK_CHARACTER_SINAMMON)
    {
      player_one_adjustment = 7;
    }
  else if(game_->players[1].character_type == NK_CHARACTER_GURDIP)
    {
      player_one_adjustment = 10;
    }

  /*
   * The source global includes player 1's character adjustment because that
   * was its only CPU.  Preserve that stored value while applying the same
   * character-specific accuracy to either CPU side.
   */
  return game_->difficulty_y_miss - player_one_adjustment + adjustment;
}


static
s32
_nk_game_find_hit_y(NkGame       *game_,
                    NkGamePlayer *player_,
                    u8            player_index_)
{
  nk_vector vector;
  const NkAnimFrame *frame;
  const NkAnimRect *attack;
  s32    destination_x;
  s32    time;
  s32    right_edge;

  if(game_->game_state != NK_GAME_STATE_ACTIVE)
    {
      return 20 + nk_rng_bounded(&game_->rng, 160);
    }

  if((player_->move > NK_MOVE_WIN) && (player_->move != NK_MOVE_DIZZY))
    {
      return -1;
    }

  vector = game_->ball.vector;
  destination_x = player_->destination_x;
  if(player_index_ == 0U)
    {
      vector.position.x = nk_wrap_sub(
        nk_fixed_from_int(NK_LOGICAL_WIDTH), vector.position.x
        );
      vector.velocity.x = nk_wrap_neg(vector.velocity.x);
      destination_x = _nk_game_ai_x(destination_x, player_index_);
    }

  if((vector.velocity.y == 0) || (vector.velocity.x == 0))
    {
      return nk_fixed_floor_to_int(vector.position.y);
    }

  frame = nk_game_player_frame(player_);
  attack = nk_anim_frame_attack(frame, 0U);
  if(attack == NULL)
    {
      return nk_fixed_floor_to_int(vector.position.y);
    }

  right_edge = nk_fixed_from_int(destination_x - attack->x1);
  if(vector.position.x > right_edge)
    {
      return nk_fixed_floor_to_int(vector.position.y);
    }

  if(vector.velocity.x <= 0)
    {
      time = nk_wrap_sub(
        nk_fixed_from_int(5), vector.position.x
        ) / vector.velocity.x;
      time += nk_wrap_neg(right_edge) / vector.velocity.x;
    }
  else
    {
      time = nk_wrap_sub(right_edge, vector.position.x)
             / vector.velocity.x;
    }

  if(time <= 0)
    {
      return 101;
    }

  if(time > 1000)
    {
      return 100;
    }
  while(time > 0)
    {
      if(vector.velocity.y < 0)
        {
          time -= nk_wrap_sub(
            nk_fixed_from_int(NK_GAME_ARENA_TOP), vector.position.y
            ) / vector.velocity.y;
          vector.position.y = nk_fixed_from_int(NK_GAME_ARENA_TOP);
        }
      else
        {
          time -= nk_wrap_sub(
            nk_fixed_from_int(NK_GAME_ARENA_BOTTOM), vector.position.y
            ) / vector.velocity.y;
          vector.position.y = nk_fixed_from_int(NK_GAME_ARENA_BOTTOM);
        }

      vector.velocity.y = nk_wrap_neg(vector.velocity.y);
    }

  vector.position.y = nk_wrap_sub(
    vector.position.y,
    nk_s32_from_bits((u32)time * (u32)vector.velocity.y)
    );
  return nk_fixed_floor_to_int(vector.position.y);
}


static
bool
_nk_game_ai_special_y_ready(const NkGame       *game_,
                            const NkGamePlayer *player_)
{
  if(game_->fix_orig_bugs)
    {
      return ((player_->y > (player_->destination_y - 10)) &&
              (player_->y < (player_->destination_y + 10)));
    }

  return ((player_->y > (player_->destination_y - 10)) ||
          (player_->y < (player_->destination_y + 10)));
}


static
void
_nk_game_ai_special_one(NkGame       *game_,
                        NkGamePlayer *player_,
                        NkGamePlayer *opponent_,
                        u8            player_index_)
{
  s32    ball_x;
  u8    ball_facing;

  ball_x = _nk_game_ai_x(game_->ball.x, player_index_);
  ball_facing = _nk_game_ai_ball_facing(game_, player_index_);
  switch(player_->character_type)
    {
    case NK_CHARACTER_FETUS:
    case NK_CHARACTER_GURDIP:
    case NK_CHARACTER_ED:
    case NK_CHARACTER_GONZOLES:
      if(ball_facing == 0U)
        {
          player_->strategy = 0;
        }
      else
        {
          player_->destination_y = opponent_->y;
          if(_nk_game_ai_special_y_ready(game_, player_))
            {
              player_->input_stat |= NK_STAT_BUTTON_ONE;
            }
        }

      break;
    case NK_CHARACTER_KLUBBOR:
      if((ball_facing == 0U) && (ball_x > 260) &&
         (game_->ball.y > player_->y - 30) &&
         (game_->ball.y < player_->y + 10))
        {
          player_->input_stat |= NK_STAT_BUTTON_ONE;
        }

      break;
    case NK_CHARACTER_SINAMMON:
      if((ball_x > 240) && (ball_x < 310) &&
         (((game_->ball.y > player_->y + 20) &&
              (game_->ball.y < player_->y + 80)) ||
             ((game_->ball.y > player_->y - 80) &&
                 (game_->ball.y < player_->y - 20))))
        {
          player_->input_stat |= NK_STAT_BUTTON_ONE;
        }

      break;
    case NK_CHARACTER_BUDDY:
      if((ball_facing == 0U) && (ball_x > 120))
        {
          player_->strategy = 0;
          player_->input_buttons = 0U;
          break;
        }

      if(player_->move < NK_MOVE_WIN)
        {
          player_->input_stat |= NK_STAT_BUTTON_ONE;
        }
      else
        {
          if(game_->game_state == NK_GAME_STATE_ACTIVE)
            {
              player_->input_buttons |= NK_BUTTON_ONE;
            }
          else
            {
              break;
            }
        }

      player_->strategy_duration++;
      return;
    case NK_CHARACTER_HENRY:
      player_->input_buttons |= NK_BUTTON_ONE;
      if(player_->move < NK_MOVE_WIN)
        {
          if((ball_x > 270) &&
             (game_->ball.y > player_->y - 15) &&
             (game_->ball.y < player_->y + 15))
            {
              player_->input_stat |= NK_STAT_BUTTON_ONE;
              return;
            }
        }
      else
        {
          if(player_->y < 100)
            {
              if(opponent_->y < 100)
                {
                  player_->destination_y =
                    120 + nk_rng_bounded(&game_->rng, 60);
                }
              else
                {
                  player_->input_buttons = 0U;
                  player_->strategy = 0;
                  break;
                }
            }
          else if(opponent_->y > 100)
            {
              player_->destination_y =
                20 + nk_rng_bounded(&game_->rng, 60);
            }
          else
            {
              player_->input_buttons = 0U;
              player_->strategy = 0;
              break;
            }

          player_->strategy_duration++;
          return;
        }

      break;
    default:
      break;
    }

  if((player_->strategy == 0) ||
     ((player_->input_stat & NK_STAT_BUTTON_ONE) != 0U))
    {
      player_->strategy = 0;
      player_->strategy_duration = game_->difficulty_idle_time;
      player_->destination_y = -1;
    }
}


static
void
_nk_game_ai_special_two(NkGame       *game_,
                        NkGamePlayer *player_,
                        NkGamePlayer *opponent_,
                        u8            player_index_)
{
  s32    ball_x;
  u8    ball_facing;

  ball_x = _nk_game_ai_x(game_->ball.x, player_index_);
  ball_facing = _nk_game_ai_ball_facing(game_, player_index_);
  switch(player_->character_type)
    {
    case NK_CHARACTER_HENRY:
    case NK_CHARACTER_SINAMMON:
      if((ball_facing == 0U) && (ball_x > 150))
        {
          player_->strategy = 0;
        }
      else
        {
          player_->destination_y = opponent_->y;
          if(_nk_game_ai_special_y_ready(game_, player_))
            {
              player_->input_stat |= NK_STAT_BUTTON_TWO;
            }
        }

      break;
    case NK_CHARACTER_KLUBBOR:
      if((ball_facing == 0U) && (ball_x > 260) &&
         (game_->ball.y > player_->y - 30) &&
         (game_->ball.y < player_->y + 10))
        {
          player_->input_stat |= NK_STAT_BUTTON_TWO;
        }

      break;
    case NK_CHARACTER_FETUS:
      if((ball_x > 290) &&
         (game_->ball.y > player_->y - 40) &&
         (game_->ball.y < player_->y))
        {
          player_->input_stat |= NK_STAT_BUTTON_TWO;
        }

      break;
    case NK_CHARACTER_GURDIP:
      player_->strategy = 0;
      player_->strategy_duration = 100;
      break;
    case NK_CHARACTER_ED:
      player_->input_buttons |= NK_BUTTON_TWO;
      if(player_->move < NK_MOVE_HOLD)
        {
          if((ball_x > 260) &&
             (game_->ball.y > player_->y - 30) &&
             (game_->ball.y < player_->y))
            {
              player_->input_stat |= NK_STAT_BUTTON_TWO;
              return;
            }
        }
      else
        {
          player_->input_buttons |= NK_BUTTON_TWO;
          if(player_->y < 100)
            {
              if(opponent_->y < 100)
                {
                  player_->destination_y =
                    120 + nk_rng_bounded(&game_->rng, 60);
                }
              else
                {
                  player_->input_buttons = 0U;
                  player_->strategy = 0;
                  break;
                }
            }
          else if(opponent_->y > 100)
            {
              player_->destination_y =
                20 + nk_rng_bounded(&game_->rng, 60);
            }
          else
            {
              player_->input_buttons = 0U;
              player_->strategy = 0;
              break;
            }

          player_->strategy_duration++;
          return;
        }

      break;
    case NK_CHARACTER_BUDDY:
      if(player_->move < NK_MOVE_WIN)
        {
          if(ball_facing == 0U)
            {
              player_->strategy = 0;
            }
          else
            {
              player_->destination_y = opponent_->y;
              if(_nk_game_ai_special_y_ready(game_, player_))
                {
                  player_->input_stat |= NK_STAT_BUTTON_TWO;
                  return;
                }
            }
        }
      else
        {
          player_->destination_y = opponent_->y;
          player_->strategy_duration++;
          return;
        }

      break;
    default:
      break;
    }

  if((player_->strategy == 0) ||
     ((player_->input_stat & NK_STAT_BUTTON_TWO) != 0U))
    {
      player_->strategy = 0;
      player_->strategy_duration = game_->difficulty_idle_time;
      player_->destination_y = -1;
    }
}


static
void
_nk_game_computer_ai(NkGame       *game_,
                     NkGamePlayer *player_,
                     NkGamePlayer *opponent_,
                     u8            player_index_)
{
  s32    destination_x;
  s32    y_miss;
  s32    ball_x;
  u8    ball_facing;

  player_->input_stat = 0U;
  player_->input_buttons = 0U;
  ball_x = _nk_game_ai_x(game_->ball.x, player_index_);
  ball_facing = _nk_game_ai_ball_facing(game_, player_index_);
  y_miss = _nk_game_ai_y_miss(game_, player_index_);

  if(player_->character_type == NK_CHARACTER_GONZOLES)
    {
      if(player_->destination_y == -1)
        {
          destination_x = 180 + nk_rng_bounded(&game_->rng, 160);
          player_->destination_x = _nk_game_ai_x(
            destination_x, player_index_
            );
        }

      if(game_->game_state < NK_GAME_STATE_ACTIVE)
        {
          player_->destination_x = _nk_game_ai_x(NK_GAME_PLAYER_START_X_RIGHT, player_index_);
        }
    }
  else
    {
      player_->destination_x = _nk_game_ai_x(NK_GAME_PLAYER_START_X_RIGHT, player_index_);
    }

  if((player_->character_type == NK_CHARACTER_GURDIP) &&
     (ball_facing == 0U) && (ball_x < 150) &&
     (_nk_game_abs(game_->ball.y - player_->y) > 50))
    {
      player_->input_stat |= NK_STAT_BUTTON_TWO;
    }

  if((player_->destination_y == -1) || ((game_->tick & 31U) == 0U))
    {
      player_->destination_y = _nk_game_find_hit_y(
        game_, player_, player_index_
        );
    }

  if(player_->destination_y != -1)
    {
      if(player_->y
         < player_->destination_y - y_miss)
        {
          player_->input_stat |= NK_DIR_DOWN;
        }

      if(player_->y
         > player_->destination_y + 10 + y_miss)
        {
          player_->input_stat |= NK_DIR_UP;
        }
    }

  if(player_->destination_x < player_->x - 5)
    {
      player_->input_stat |= NK_DIR_LEFT;
    }

  if(player_->destination_x > player_->x + 5)
    {
      player_->input_stat |= NK_DIR_RIGHT;
    }

  if(game_->game_state != NK_GAME_STATE_ACTIVE)
    {
      return;
    }

  if(player_->control_ball)
    {
      if(ball_facing != 0U)
        {
          player_->input_stat = 0U;
          if(opponent_->y < 100)
            {
              player_->input_stat |= NK_DIR_DOWN;
            }
          else
            {
              player_->input_stat |= NK_DIR_UP;
            }

          player_->input_buttons |= NK_BUTTON_ONE;
        }

      return;
    }

  switch(player_->strategy)
    {
    case 0:
      if(player_->strategy_duration > 0)
        {
          player_->strategy_duration--;
        }
      else
        {
          if(player_->energy
             >= nk_character_energy[player_->character_type][1])
            {
              if(player_->energy
                 >= nk_character_energy[player_->character_type][2])
                {
                  player_->strategy =
                    nk_rng_bounded(&game_->rng, 2) + 1;
                }
              else
                {
                  player_->strategy = 1;
                }
            }
          else if(player_->energy
                  >= nk_character_energy[player_->character_type][2])
            {
              player_->strategy = 2;
            }
          else
            {
              player_->strategy = 0;
              player_->strategy_duration = game_->difficulty_idle_time;
              break;
            }

          player_->strategy_duration = game_->difficulty_special_time;
        }

      break;
    case 1:
      if(player_->strategy_duration > 0)
        {
          player_->strategy_duration--;
          _nk_game_ai_special_one(
            game_, player_, opponent_, player_index_
            );
        }
      else
        {
          player_->strategy = 0;
          player_->strategy_duration = game_->difficulty_idle_time / 2;
        }

      break;
    case 2:
      if(player_->strategy_duration > 0)
        {
          player_->strategy_duration--;
          _nk_game_ai_special_two(
            game_, player_, opponent_, player_index_
            );
        }
      else
        {
          player_->strategy = 0;
          player_->strategy_duration = game_->difficulty_idle_time / 2;
        }

      break;
    default:
      player_->strategy = 0;
      break;
    }
}


static
void
_nk_game_update_character_special(NkGame       *game_,
                                  NkGamePlayer *player_,
                                  u8            player_index_)
{
  const NkAnimFrame *frame;
  NkAnimProjectedAttachment attachment;

  if(player_->character_type == NK_CHARACTER_HENRY)
    {
      if(player_->move == NK_MOVE_HOLD)
        {
          if(player_->facing == 0U)
            {
              game_->ball.vector.position.x = nk_wrap_add(
                player_->vector.position.x, 0x390000
                );
            }
          else
            {
              game_->ball.vector.position.x = nk_wrap_sub(
                player_->vector.position.x, 0x390000
                );
            }

          game_->ball.vector.position.y = nk_wrap_add(
            player_->vector.position.y, 0x050000
            );
          if((player_->input_buttons & NK_BUTTON_ONE) == 0U)
            {
              _nk_game_request_move(player_, NK_MOVE_RELEASE);
              game_->ball.vector.velocity.y =
                _nk_game_half(player_->vector.velocity.y);
            }
        }

      if(player_->move == NK_MOVE_RELEASE)
        {
          if(((player_->facing == 0U) && (player_->x < 0)) ||
             ((player_->facing != 0U) && (player_->x > 320)))
            {
              _nk_game_request_move(player_, NK_MOVE_STANCE);
            }
        }
    }

  if(player_->character_type == NK_CHARACTER_ED)
    {
      if((player_->move == NK_MOVE_HOLD) ||
         ((player_->move == NK_MOVE_RELEASE) &&
             (player_->animation.frame_index < 3U)))
        {
          game_->ball.freeze++;
          frame = nk_game_player_frame(player_);
          if((frame != NULL) &&
             ((frame->parameter_bits & 0x0100U) != 0U) &&
             (nk_anim_project_attachment(
                   frame,
                   nk_fixed_floor_to_int(player_->vector.position.x),
                   nk_fixed_floor_to_int(player_->vector.position.y),
                   player_->facing,
                   &attachment)))
            {
              game_->ball.vector.position.x =
                nk_fixed_from_int(attachment.x);
              game_->ball.vector.position.y =
                nk_fixed_from_int(attachment.y);
            }
        }

      if(player_->move == NK_MOVE_HOLD)
        {
          if(((player_->input_buttons & NK_BUTTON_TWO) == 0U) ||
             (player_->energy == 0))
            {
              _nk_game_request_move(player_, NK_MOVE_RELEASE);
              game_->ball.freeze = 0;
              game_->ball.vector.velocity.y =
                _nk_game_half(player_->vector.velocity.y);
              game_->ball.vector.velocity.x =
                (game_->ball.vector.velocity.x * 2) / 3;
              nk_vector_speed_x_velocity(&game_->ball.vector, 0x15000);
              game_->players[player_index_ ^ 1U].destination_y = -1;
            }
          else if((game_->tick & 7U) == 0U)
            {
              player_->energy--;
            }
        }
    }
}


static
void
_nk_game_update_fighter(NkGame *game_,
                        u8      player_index_)
{
  NkGamePlayer *player;
  NkGamePlayer *opponent;
  const NkAnimFrame *frame;
  s32    delta;

  player = &game_->players[player_index_];
  opponent = &game_->players[player_index_ ^ 1U];
  if(player->control_type == NK_CONTROL_REMOTE)
    {
      return;
    }

  if(player->control_type == NK_CONTROL_COMPUTER)
    {
      _nk_game_computer_ai(game_, player, opponent, player_index_);
    }

  if(opponent->move == NK_MOVE_PROJECTILE_HIT)
    {
      if(opponent->facing == 0U)
        {
          player->x = opponent->x + 70;
        }
      else
        {
          player->x = opponent->x - 70;
        }

      player->y = opponent->y;
      player->input_stat &= NK_DIR_MASK;
      nk_game_try_ball_hit(game_, player_index_);
      return;
    }

  if(player->move == NK_MOVE_PROJECTILE_HIT)
    {
      game_->draw_priority = (s32)player_index_;
      player->vector.velocity.y /= 2;
      nk_vector_tick(
        &player->vector,
        (u8)(player->input_stat & (NK_DIR_UP | NK_DIR_DOWN)),
        true
        );
      nk_vector_tick(
        &player->vector,
        (u8)(opponent->input_stat & (NK_DIR_UP | NK_DIR_DOWN)),
        true
        );
      player->x = nk_fixed_floor_to_int(player->vector.position.x)
                  + nk_rng_bounded(&game_->rng, 5);
      player->y = nk_fixed_floor_to_int(player->vector.position.y);
      player->input_stat &= NK_DIR_MASK;
      if(player->animation.frame_index >= 2U)
        {
          player->move = NK_MOVE_STANCE;
        }
    }
  else
    {
      frame = nk_game_player_frame(player);
      if(frame == NULL)
        {
          return;
        }

      nk_vector_tick(
        &player->vector,
        player->input_stat,
        nk_anim_frame_clip_bits(frame) != 0U
        );
      delta = (s32)nk_anim_frame_delta_x(frame) * 256;
      if(player->facing == 0U)
        {
          player->vector.position.x = nk_wrap_add(
            player->vector.position.x, delta
            );
        }
      else
        {
          player->vector.position.x = nk_wrap_sub(
            player->vector.position.x, delta
            );
        }

      delta = (s32)nk_anim_frame_delta_y(frame) * 256;
      player->vector.position.y = nk_wrap_add(
        player->vector.position.y, delta
        );

      if((player->character_type == NK_CHARACTER_GONZOLES) &&
         ((player->input_buttons & NK_BUTTON_TWO) != 0U) &&
         (player->energy > 0))
        {
          nk_vector_tick(&player->vector, player->input_stat, false);
          nk_vector_tick(&player->vector, player->input_stat, false);
          player->energy--;
        }

      player->x = nk_fixed_floor_to_int(player->vector.position.x);
      player->y = nk_fixed_floor_to_int(player->vector.position.y);

      if(player->dizzy_duration == 0)
        {
          if(((player->input_stat & NK_STAT_BUTTON_ONE) != 0U) &&
             ((player->move < NK_MOVE_WIN) ||
                 (player->move == NK_MOVE_TAP_RECOVERY)) &&
             (player->energy >=
                 nk_character_energy[player->character_type][1]) &&
             (!player->control_ball))
            {
              _nk_game_request_move(player, NK_MOVE_SPECIAL_ONE);
              player->energy -=
                nk_character_energy[player->character_type][1];
              if((player->character_type == NK_CHARACTER_SINAMMON) &&
                 (game_->ball.y > player->y))
                {
                  player->move = NK_MOVE_HOLD;
                }
            }

          if((player->character_type != NK_CHARACTER_GONZOLES) &&
             ((player->input_stat & NK_STAT_BUTTON_TWO) != 0U) &&
             ((player->move < NK_MOVE_WIN) ||
                 (player->move == NK_MOVE_TAP_RECOVERY)) &&
             (player->energy >=
                 nk_character_energy[player->character_type][2]))
            {
              _nk_game_request_move(player, NK_MOVE_SPECIAL_TWO);
              player->energy -=
                nk_character_energy[player->character_type][2];
            }

          if(((player->input_stat & NK_DIR_DOWN) != 0U) &&
             (player->move != NK_MOVE_DOWN) &&
             (player->move < NK_MOVE_WIN))
            {
              _nk_game_request_move(player, NK_MOVE_DOWN);
            }
          else if(((player->input_stat & NK_DIR_UP) != 0U) &&
                  (player->move != NK_MOVE_UP) &&
                  (player->move < NK_MOVE_WIN))
            {
              _nk_game_request_move(player, NK_MOVE_UP);
            }
          else if(((player->input_stat
                    & (NK_DIR_UP | NK_DIR_DOWN)) == 0U) &&
                  (player->move != NK_MOVE_STANCE) &&
                  (player->move < NK_MOVE_WIN))
            {
              _nk_game_request_move(player, NK_MOVE_STANCE);
            }
        }
      else
        {
          player->dizzy_duration--;
        }

      player->input_stat &= NK_DIR_MASK;
      nk_game_try_ball_hit(game_, player_index_);
      if((player->character_type == NK_CHARACTER_BUDDY) &&
         (player->move == NK_MOVE_SPECIAL_TWO))
        {
          _nk_game_try_player_hit(game_, player_index_);
        }

      _nk_game_update_character_special(game_, player, player_index_);
    }
}


static
void
_nk_game_score(NkGame *game_,
               u8      player_index_)
{
  u8    loser;

  loser = player_index_ ^ 1U;
  _nk_game_request_move(&game_->players[player_index_], NK_MOVE_WIN);
  _nk_game_request_move(&game_->players[loser], NK_MOVE_LOSE);
  game_->game_state = NK_GAME_STATE_ROUND_ANIMATION;
  game_->score[player_index_]++;
  if((game_->score[0] >= game_->round_target) ||
     (game_->score[1] >= game_->round_target))
    {
      game_->game_over = 1;
    }

  game_->electrocute = (s32)loser + 1;
  game_->electrocute_duration = 150;
  game_->ball.vector.velocity.x = 0;
  game_->ball.vector.velocity.y = 0;
  if(player_index_ == 1U)
    {
      game_->ball.vector.acceleration.x = 0;
      game_->ball.vector.acceleration.y = 0;
    }

  game_->players[0].dizzy_duration = 0;
  game_->players[1].dizzy_duration = 0;
  _nk_game_event(
    game_,
    NK_GAME_EVENT_SCORE,
    player_index_,
    (u8)game_->score[player_index_],
    (u8)(game_->game_over != 0),
    game_->ball.x,
    game_->ball.y
    );
  _nk_game_event(
    game_,
    NK_GAME_EVENT_SOUND,
    NK_GAME_ACTOR_BALL,
    (u8)((NK_ANIM_SOUND_PLAYER_FLAG | 5) << NK_ANIM_SOUND_CODE_BITS),
    1U,
    game_->ball.x,
    game_->ball.y
    );
}


static
void
_nk_game_update_ball(NkGame *game_)
{
  NkGamePlayer *ball;
  s32    speed;
  s32    sound;
  int result;
  int index;

  ball = &game_->ball;
  result = 0;
  if(ball->stuck == 0)
    {
      result = nk_vector_bounce(&ball->vector);
    }
  else
    {
      ball->stuck--;
    }

  ball->x = nk_fixed_floor_to_int(ball->vector.position.x);
  ball->y = nk_fixed_floor_to_int(ball->vector.position.y);

  if(game_->game_state == NK_GAME_STATE_ACTIVE)
    {
      if((ball->x > NK_GAME_RIGHT_GOAL_X) && (!game_->multiplayer))
        {
          _nk_game_score(game_, 0U);
        }
      else if(ball->x < NK_GAME_LEFT_GOAL_X)
        {
          _nk_game_score(game_, 1U);
        }
    }

  if((result & 3) != 0)
    {
      ball->facing ^= 1U;
    }

  if(result != 0)
    {
      sound = nk_rng_bounded(&game_->rng, 4) + 1;
      _nk_game_event(
        game_,
        NK_GAME_EVENT_SOUND,
        NK_GAME_ACTOR_BALL,
        (u8)((NK_ANIM_SOUND_PLAYER_FLAG | sound) << NK_ANIM_SOUND_CODE_BITS),
        1U,
        ball->x,
        ball->y
        );
    }

  if((result & 12) != 0)
    {
      for(index = 0; index < 5; ++index)
        {
          _nk_game_random_effect(
            game_, 16, ball->x, ball->y,
            20, -10, 0, 0, ball->facing
            );
        }

      _nk_game_random_effect(
        game_, 18, ball->x, ball->y,
        0, 0, 0, 0, ball->facing
        );
    }

  if((game_->tick & 63U) == 0U)
    {
      _nk_game_generate_effect(
        game_, 10, ball->x, ball->y, 0, 0, 0U, 0U
        );
    }

  speed = nk_wrap_add(
    _nk_game_abs(ball->vector.velocity.x),
    _nk_game_abs(ball->vector.velocity.y)
    );
  if((speed > 0x20000) && ((game_->tick & 31U) == 0U))
    {
      _nk_game_generate_effect(
        game_, 18, ball->x, ball->y, 0, 0, 0U, 0U
        );
    }

  if((speed > 0x30000) && ((game_->tick & 15U) == 0U) &&
     ((game_->tick & 16U) != 0U))
    {
      _nk_game_generate_effect(
        game_, 18, ball->x, ball->y, 0, 0, 0U, 0U
        );
    }
}


static
void
_nk_game_release_ball(NkGame *game_)
{
  const NkAnimFrame *frame;
  NkAnimProjectedAttachment attachment;
  int index;

  frame = nk_game_player_frame(&game_->chopper);
  if((frame == NULL) || ((frame->parameter_bits & 0x0100U) == 0U) ||
     (game_->game_state != NK_GAME_STATE_PREGAME) ||
     (!nk_anim_project_attachment(
           frame,
           game_->chopper.x,
           game_->chopper.y,
           game_->chopper.facing,
           &attachment)))
    {
      return;
    }

  game_->players[0].destination_y = -1;
  game_->players[1].destination_y = -1;
  game_->ball.facing = (u8)(game_->chopper.facing ^ 1U);
  game_->ball_pain = 0;
  game_->ball.pain = 0;
  game_->ball.x = attachment.x;
  game_->ball.y = attachment.y;
  game_->ball.vector.position.x = nk_fixed_from_int(game_->ball.x);
  game_->ball.vector.position.y = nk_fixed_from_int(game_->ball.y);
  _nk_game_reset_player(&game_->ball);
  game_->game_state = NK_GAME_STATE_ACTIVE;
  if(game_->multiplayer_master)
    {
      if(game_->ball.facing != 0U)
        {
          game_->ball.vector.velocity.x = -0x0e000;
        }
      else
        {
          game_->ball.vector.velocity.x = 0x0e000;
        }

      game_->ball.vector.velocity.y = 0x0e000;
    }

  _nk_game_event(
    game_,
    NK_GAME_EVENT_BALL_RELEASE,
    NK_GAME_ACTOR_CHOPPER,
    game_->ball.facing,
    0U,
    game_->ball.x,
    game_->ball.y
    );

  for(index = 0; index < 25; ++index)
    {
      _nk_game_random_effect(
        game_, 16, game_->ball.x, game_->ball.y,
        20, 0, 0, 0, (u8)(game_->ball.facing ^ 1U)
        );
    }

  for(index = 0; index < 10; ++index)
    {
      _nk_game_random_effect(
        game_, 10, game_->ball.x, game_->ball.y,
        20, 0, 0, 0, (u8)(game_->ball.facing ^ 1U)
        );
      _nk_game_random_effect(
        game_, 18, game_->ball.x, game_->ball.y,
        20, -10, 20, -10, (u8)(game_->ball.facing ^ 1U)
        );
    }

  _nk_game_random_effect(
    game_, 25, game_->ball.x, game_->ball.y,
    30, -15, 30, 0, (u8)(game_->ball.facing ^ 1U)
    );
}


static
void
_nk_game_update_chopper(NkGame *game_)
{
  NkGamePlayer *chopper;

  chopper = &game_->chopper;
  if(chopper->fraction_x == 0)
    {
      if(chopper->y > 198)
        {
          chopper->y--;
        }
      else
        {
          chopper->fraction_x++;
        }
    }
  else if(chopper->fraction_x < 200)
    {
      chopper->fraction_x++;
    }
  else
    {
      chopper->y += 2;
    }

  _nk_game_release_ball(game_);
}


static
void
_nk_game_begin_pregame(NkGame *game_)
{
  _nk_game_reset_player(&game_->chopper);
  game_->ball.draw_me = 0U;
  game_->chopper.facing = (u8)nk_rng_bounded(&game_->rng, 2);
  if(game_->chopper.facing != 0U)
    {
      game_->chopper.x = NK_GAME_ARENA_CENTER_X - nk_rng_bounded(&game_->rng, 80);
    }
  else
    {
      game_->chopper.x = NK_GAME_ARENA_CENTER_X + nk_rng_bounded(&game_->rng, 80);
    }

  if(game_->multiplayer)
    {
      game_->chopper.facing = (u8)game_->multiplayer_master;
      game_->chopper.x = NK_GAME_ARENA_CENTER_X;
    }

  game_->chopper.y = 250;
  game_->ball.x = NK_GAME_BALL_START_X;
  game_->game_over = 0;
  game_->game_state = NK_GAME_STATE_PREGAME;
}


static
void
_nk_game_finish_round(NkGame *game_)
{
  s32    dialogue_index;

  if(game_->game_state == NK_GAME_STATE_ROUND_COMPLETE)
    {
      dialogue_index = -1;
      if(game_->score[0] >= game_->round_target)
        {
          game_->game_state = NK_GAME_STATE_MATCH_COMPLETE;
          game_->winner = 0;
          dialogue_index = nk_rng_bounded(&game_->rng, 4);
        }
      else if(game_->score[1] >= game_->round_target)
        {
          game_->game_state = NK_GAME_STATE_MATCH_COMPLETE;
          game_->winner = 1;
        }
      else
        {
          game_->game_state = NK_GAME_STATE_WAITING;
        }

      if(game_->game_state == NK_GAME_STATE_MATCH_COMPLETE)
        {
          if(game_->players[game_->winner].control_type
             == NK_CONTROL_COMPUTER)
            {
              game_->outcome_dialogue_kind = NK_GAME_DIALOGUE_LOSS;
              game_->outcome_dialogue_index =
                nk_rng_bounded(&game_->rng, 5);
            }
          else
            {
              game_->outcome_dialogue_kind = NK_GAME_DIALOGUE_WIN;
              game_->outcome_dialogue_index =
                nk_rng_bounded(&game_->rng, 4);
            }

          (void)dialogue_index;
        }

      game_->ball.facing = 0U;
    }
}


void
nk_game_prepare_presentation(NkGame *game_)
{
  if(game_ == NULL)
    {
      return;
    }

  /*
   * These are two source-order painter clauses, not an until-stable state
   * machine.  In particular, state 3 can become -2 only after the -2
   * clause has already been passed for this presentation.
   */
  if((game_->game_state == NK_GAME_STATE_WAITING) && (game_->ready))
    {
      _nk_game_begin_pregame(game_);
    }

  _nk_game_finish_round(game_);
}


static
void
_nk_game_tick_effects(NkGame *game_)
{
  game_->effect_clock_phase = (u8)(
    game_->effect_clock_phase + NK_EFFECT_PRESENTATION_HZ
    );
  if(game_->effect_clock_phase < NK_LOGICAL_HZ)
    {
      return;
    }

  game_->effect_clock_phase = (u8)(
    game_->effect_clock_phase - NK_LOGICAL_HZ
    );
  nk_effect_pool_advance_to_tick(
    &game_->effects,
    game_->sticky_blood,
    game_->tick
    );
}


static
void
_nk_game_update_player(NkGame       *game_,
                       NkGamePlayer *player_,
                       u8            actor_)
{
  if(player_->freeze > 0)
    {
      player_->x = nk_fixed_floor_to_int(player_->vector.position.x);
      player_->y = nk_fixed_floor_to_int(player_->vector.position.y);
      player_->freeze--;
      return;
    }

  if(actor_ < NK_GAME_PLAYER_COUNT)
    {
      _nk_game_update_fighter(game_, actor_);
    }
  else if(player_->control_type == NK_CONTROL_BALL)
    {
      _nk_game_update_ball(game_);
    }
  else if(player_->control_type == NK_CONTROL_CHOPPER)
    {
      _nk_game_update_chopper(game_);
    }

  _nk_game_advance_animation(game_, player_, actor_);
}


void
nk_game_step(NkGame *game_)
{
  int index;

  if(game_ == NULL)
    {
      return;
    }

  for(index = 0; index < NK_GAME_PLAYER_COUNT; ++index)
    {
      game_->players[index].presentation_previous_x =
        game_->players[index].x * 256 +
        (s32)((u32)game_->players[index].fraction_x & 0xffU);
      game_->players[index].presentation_previous_y =
        game_->players[index].y * 256 +
        (s32)((u32)game_->players[index].fraction_y & 0xffU);
    }

  game_->ball.presentation_previous_x = game_->ball.x * 256 +
    (s32)((u32)game_->ball.fraction_x & 0xffU);
  game_->ball.presentation_previous_y = game_->ball.y * 256 +
    (s32)((u32)game_->ball.fraction_y & 0xffU);
  game_->ball.presentation_draw_me = game_->ball.draw_me;
  game_->ball.presentation_frozen = (u8)(game_->ball.freeze != 0);
  game_->chopper.presentation_previous_x = game_->chopper.x * 256 +
    (s32)((u32)game_->chopper.fraction_x & 0xffU);
  game_->chopper.presentation_previous_y = game_->chopper.y * 256 +
    (s32)((u32)game_->chopper.fraction_y & 0xffU);
  game_->chopper.presentation_draw_me = game_->chopper.draw_me;
  nk_game_clear_events(game_);
  game_->new_frame = 0U;
  game_->tick++;
  _nk_game_tick_effects(game_);
  for(index = 0; index < NK_GAME_PLAYER_COUNT; ++index)
    {
      if(game_->players[index].energy < game_->bar_size[index])
        {
          game_->bar_size[index]--;
        }
      else if(game_->players[index].energy > game_->bar_size[index])
        {
          game_->bar_size[index]++;
        }
    }

  if(game_->electrocute_duration > 0)
    {
      game_->electrocute_duration--;
    }
  else
    {
      game_->electrocute = 0;
    }

  if(!game_->ready)
    {
      return;
    }

  /* Projectile updates precede players in the original TickUpdate. */
  _nk_game_update_projectiles(game_, 0U);
  _nk_game_update_projectiles(game_, 1U);
  _nk_game_update_player(game_, &game_->players[0], 0U);
  _nk_game_update_player(game_, &game_->players[1], 1U);
  if(game_->ball.draw_me != 0U)
    {
      _nk_game_update_player(game_, &game_->ball, 2U);
    }

  if(game_->chopper.draw_me != 0U)
    {
      _nk_game_update_player(game_, &game_->chopper, 3U);
    }
}


bool
nk_game_validate(const NkGame *game_)
{
  int index;

  if((game_ == NULL) ||
     (game_->round_target <= 0) ||
     (game_->event_count > NK_GAME_EVENT_COUNT) ||
     (game_->game_state < NK_GAME_STATE_WAITING) ||
     (game_->game_state > NK_GAME_STATE_MATCH_COMPLETE) ||
     (game_->players[0].character_type >= NK_CHARACTER_COUNT) ||
     (game_->players[1].character_type >= NK_CHARACTER_COUNT) ||
     (game_->players[0].animation_bank >= NK_CHARACTER_COUNT) ||
     (game_->players[1].animation_bank >= NK_CHARACTER_COUNT) ||
     (game_->ball.animation_bank != NK_HEAD_ANIMATION_BANK) ||
     (game_->chopper.animation_bank != NK_HEAD_ANIMATION_BANK) ||
     (game_->ball.control_type != NK_CONTROL_BALL) ||
     (game_->chopper.control_type != NK_CONTROL_CHOPPER))
    {
      return false;
    }

  for(index = 0; index < NK_GAME_PLAYER_COUNT; ++index)
    {
      if((game_->score[index] < 0) ||
         (nk_game_player_frame(&game_->players[index]) == NULL))
        {
          return false;
        }
    }

  if((game_->ball.draw_me != 0U) &&
     (nk_game_player_frame(&game_->ball) == NULL))
    {
      return false;
    }

  if((game_->chopper.draw_me != 0U) &&
     (nk_game_player_frame(&game_->chopper) == NULL))
    {
      return false;
    }

  return true;
}
