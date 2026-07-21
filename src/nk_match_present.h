#pragma once

#include "nk_dialogue.h"
#include "nk_game.h"
#include "nk_sticky.h"

#define NK_MATCH_PRESENT_PHASE_ONE (0xffffU)

typedef struct NkMatchPresentationPosition
{
  s32    x;
  s32    y;
} NkMatchPresentationPosition;

typedef struct NkMatchPresentation
{
  NkStickyQueue sticky_pending;
  u8    effect_draw_modes[NK_EFFECT_POOL_COUNT];
  NkDialogueState dialogue_snapshot;
  u32    render_phase;
  u8    prepared;
  u8    outcome_began;
  u8    interpolation_initialized;
  u32    prepared_tick;
} NkMatchPresentation;

void
nk_match_present_reset(NkMatchPresentation *presentation_);

/*
 * Perform the source painter's state-changing work in exact draw order.
 * The tick entry point may skip overwrite-only presentation writes on catch-up
 * ticks which will not be rendered.  Stateful dialogue, actor pain, effect
 * migration, sticky conversion, and RNG behavior still run on every tick.
 * The renderer consumes the final tick's prepared state and the accumulated
 * sticky queue without changing logical state.  The frame entry point always
 * materializes that state.  The short wrapper resets the sticky queue for host
 * callers which prepare one presentation at a time.
 */
bool
nk_match_present_prepare(NkMatchPresentation *presentation_,
                         NkGame              *game_,
                         int                  paused_);
bool
nk_match_present_prepare_tick(NkMatchPresentation *presentation_,
                              NkGame              *game_,
                              NkDialogueState     *dialogue_,
                              int                  outcome_started_,
                              int                  talking_,
                              int                  background_hidden_,
                              int                  quit_,
                              int                  paused_,
                              int                  materialize_);
bool
nk_match_present_prepare_frame(NkMatchPresentation *presentation_,
                               NkGame              *game_,
                               NkDialogueState     *dialogue_,
                               int                  outcome_started_,
                               int                  talking_,
                               int                  background_hidden_,
                               int                  quit_,
                               int                  paused_);
void
nk_match_present_set_phase(NkMatchPresentation *presentation_,
                           u32                   phase_);
void
nk_match_present_player_position(
  const NkMatchPresentation *presentation_,
  const NkGamePlayer         *player_,
  s32                       *x_,
  s32                       *y_);
void
nk_match_present_ball_position(const NkMatchPresentation *presentation_,
                               const NkGamePlayer         *ball_,
                               s32                        *x_,
                               s32                        *y_);
void
nk_match_present_chopper_position(const NkMatchPresentation *presentation_,
                                  const NkGamePlayer         *chopper_,
                                  s32                        *x_,
                                  s32                        *y_);
void
nk_match_present_projectile_position(
  const NkMatchPresentation *presentation_,
  const NkGamePlayer         *owner_,
  u32                        projectile_index_,
  s32                       *x_,
  s32                       *y_);
