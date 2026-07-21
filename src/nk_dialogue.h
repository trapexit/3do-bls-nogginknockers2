#pragma once

#include "nk_math.h"

#define NK_DIALOGUE_PERSON_DONE  (-1)
#define NK_DIALOGUE_PERSON_DELAY (0)
#define NK_DIALOGUE_PERSON_ICER  (1)
#define NK_DIALOGUE_PERSON_ETHAN (2)
#define NK_DIALOGUE_PERSON_STUMP (3)

#define NK_DIALOGUE_MODE_NONE       (0U)
#define NK_DIALOGUE_MODE_AMBIENT    (1U)
#define NK_DIALOGUE_MODE_WIN        (2U)
#define NK_DIALOGUE_MODE_LOSS       (3U)
#define NK_DIALOGUE_MODE_ENDING     (4U)
#define NK_DIALOGUE_MODE_POST_MATCH (5U)

#define NK_DIALOGUE_AMBIENT_COUNT (46U)
#define NK_DIALOGUE_WIN_COUNT      (4U)
#define NK_DIALOGUE_LOSS_COUNT     (5U)
#define NK_DIALOGUE_DONE_LIMIT    (50U)

typedef struct NkDialogueEntry
{
  s16    person;
  u16    delay;
  const char *text;
} NkDialogueEntry;

typedef struct NkDialogueConversation
{
  u16    first_entry;
  u16    entry_count;
} NkDialogueConversation;

typedef struct NkDialogueState
{
  u32    tick;
  u32    deadline;
  s32    conversation_index;
  s32    entry_index;
  s32    person;
  u8    mode;
  u8    current_valid;
  u8    terminal;
  u8    after_outcome;
  u8    initialized;
  u8    ambient_done[NK_DIALOGUE_DONE_LIMIT];
  u32    ambient_done_count;
} NkDialogueState;

extern const NkDialogueEntry nk_dialogue_entries[];
extern const NkDialogueConversation
  nk_dialogue_ambient[NK_DIALOGUE_AMBIENT_COUNT];
extern const NkDialogueConversation nk_dialogue_win[NK_DIALOGUE_WIN_COUNT];
extern const NkDialogueConversation nk_dialogue_loss[NK_DIALOGUE_LOSS_COUNT];
extern const NkDialogueConversation nk_dialogue_ending[1];
extern const NkDialogueConversation nk_dialogue_post_match[1];
extern const u16    nk_dialogue_entry_count;

void
nk_dialogue_init(NkDialogueState *state_);
void
nk_dialogue_begin_match(NkDialogueState *state_);
bool
nk_dialogue_begin_outcome(NkDialogueState *state_,
                          u8               mode_,
                          s32              conversation_index_);
void
nk_dialogue_finish_match(NkDialogueState *state_);
void
nk_dialogue_advance_clock(NkDialogueState *state_);
void
nk_dialogue_present(NkDialogueState *state_,
                    nk_rng          *rng_);
void
nk_dialogue_step(NkDialogueState *state_,
                 nk_rng          *rng_);
const
NkDialogueEntry *
nk_dialogue_current(const NkDialogueState *state_);
const
char *
nk_dialogue_current_text(const NkDialogueState *state_);
bool
nk_dialogue_data_valid(void);
