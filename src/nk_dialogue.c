#include "nk_dialogue.h"

#include "stddef.h"
#include "string.h"

#define NK_DIALOGUE_AMBIENT_RETRY_LIMIT (20)
#define NK_DIALOGUE_CATALOG_COUNT        (5U)


static
const
NkDialogueConversation *
_nk_dialogue_conversation(u8     mode_,
                          s32    index_)
{
  switch(mode_)
    {
    case NK_DIALOGUE_MODE_AMBIENT:
      if((index_ >= 0) && (index_ < (s32)NK_DIALOGUE_AMBIENT_COUNT))
        {
          return &nk_dialogue_ambient[index_];
        }

      break;
    case NK_DIALOGUE_MODE_WIN:
      if((index_ >= 0) && (index_ < (s32)NK_DIALOGUE_WIN_COUNT))
        {
          return &nk_dialogue_win[index_];
        }

      break;
    case NK_DIALOGUE_MODE_LOSS:
      if((index_ >= 0) && (index_ < (s32)NK_DIALOGUE_LOSS_COUNT))
        {
          return &nk_dialogue_loss[index_];
        }

      break;
    case NK_DIALOGUE_MODE_ENDING:
      if(index_ == 0)
        {
          return &nk_dialogue_ending[0];
        }

      break;
    case NK_DIALOGUE_MODE_POST_MATCH:
      if(index_ == 0)
        {
          return &nk_dialogue_post_match[0];
        }

      break;
    default:
      break;
    }

  return NULL;
}


static
u32
_nk_dialogue_automatic_delay(const char *text_)
{
  u32    spaces;
  int was_space;
  unsigned char character;

  spaces = 0U;
  was_space = 1;
  while((text_ != NULL) && (*text_ != '\0'))
    {
      character = (unsigned char)*text_++;
      if((character == ' ') || (character == '\t') ||
         (character == '\r') || (character == '\n'))
        {
          if(!was_space)
            {
              spaces++;
            }

          was_space = 1;
        }
      else
        {
          was_space = 0;
        }
    }

  if(spaces * 55U < 400U)
    {
      return 300U;
    }

  return spaces * 55U;
}


static
void
_nk_dialogue_start(NkDialogueState *state_,
                   u8               mode_,
                   s32              conversation_index_)
{
  state_->mode = mode_;
  state_->conversation_index = conversation_index_;
  state_->entry_index = -1;
  state_->person = NK_DIALOGUE_PERSON_DELAY;
  state_->current_valid = 0U;
  state_->terminal = 0U;
  state_->deadline = 0U;
}


void
nk_dialogue_init(NkDialogueState *state_)
{
  if(state_ == NULL)
    {
      return;
    }

  memset(state_, 0, sizeof(*state_));
  state_->conversation_index = -1;
  state_->entry_index = -1;
}


void
nk_dialogue_begin_match(NkDialogueState *state_)
{
  if(state_ == NULL)
    {
      return;
    }

  state_->tick = 1U;
  state_->deadline = 0U;
  state_->terminal = 0U;
  if(!state_->initialized)
    {
      memset(state_->ambient_done, 0, sizeof(state_->ambient_done));
      state_->ambient_done_count = 0U;
      state_->initialized = 1U;
      _nk_dialogue_start(state_, NK_DIALOGUE_MODE_AMBIENT, 0);
    }
  else if(state_->after_outcome)
    {
      state_->after_outcome = 0U;
      _nk_dialogue_start(state_, NK_DIALOGUE_MODE_POST_MATCH, 0);
    }
  else if(state_->mode == NK_DIALOGUE_MODE_AMBIENT)
    {
      /* PlayGame resets texttimeout but otherwise preserves the dialogue. */
      state_->deadline = 0U;
    }
  else
    {
      _nk_dialogue_start(state_, NK_DIALOGUE_MODE_AMBIENT, 0);
    }
}


bool
nk_dialogue_begin_outcome(NkDialogueState *state_,
                          u8               mode_,
                          s32              conversation_index_)
{
  s32    previous_person;

  if((state_ == NULL) ||
     ((mode_ != NK_DIALOGUE_MODE_WIN) &&
         (mode_ != NK_DIALOGUE_MODE_LOSS) &&
         (mode_ != NK_DIALOGUE_MODE_ENDING)) ||
     (_nk_dialogue_conversation(mode_, conversation_index_) == NULL))
    {
      return false;
    }

  /*
   * PlayGame swaps convptr/textptr before DrawText but leaves the current
   * speaker global untouched.  Background head animation therefore sees
   * the preceding speaker on this presentation, while DrawText parses the
   * first outcome entry for the following presentation.
   */
  previous_person = state_->person;
  _nk_dialogue_start(state_, mode_, conversation_index_);
  state_->person = previous_person;
  return true;
}


void
nk_dialogue_finish_match(NkDialogueState *state_)
{
  if(state_ == NULL)
    {
      return;
    }

  state_->after_outcome = 1U;
  state_->conversation_index = 0;
  state_->entry_index = -1;
  state_->person = NK_DIALOGUE_PERSON_DONE;
  state_->current_valid = 0U;
  state_->terminal = 0U;
}


static
void
_nk_dialogue_choose_ambient(NkDialogueState *state_,
                            nk_rng          *rng_)
{
  s32    selected;
  int attempts;

  state_->ambient_done_count++;
  if((state_->conversation_index >= 0) &&
     (state_->conversation_index < (s32)NK_DIALOGUE_AMBIENT_COUNT))
    {
      state_->ambient_done[state_->conversation_index]++;
    }

  if(state_->ambient_done_count >= NK_DIALOGUE_AMBIENT_COUNT)
    {
      memset(state_->ambient_done, 0, sizeof(state_->ambient_done));
      state_->ambient_done_count = 0U;
      selected = 0;
    }
  else
    {
      attempts = 0;
      do
        {
          selected = nk_rng_bounded(rng_, NK_DIALOGUE_AMBIENT_COUNT);
          attempts++;
        }
      while((state_->ambient_done[selected]) && (attempts < NK_DIALOGUE_AMBIENT_RETRY_LIMIT));
    }

  _nk_dialogue_start(state_, NK_DIALOGUE_MODE_AMBIENT, selected);
}


static
void
_nk_dialogue_reach_done(NkDialogueState *state_,
                        nk_rng          *rng_)
{
  if((state_->mode == NK_DIALOGUE_MODE_AMBIENT) ||
     (state_->mode == NK_DIALOGUE_MODE_POST_MATCH))
    {
      _nk_dialogue_choose_ambient(state_, rng_);
      return;
    }

  state_->person = NK_DIALOGUE_PERSON_DELAY;
  state_->current_valid = 0U;
  state_->terminal = 1U;
  state_->deadline = NK_U32_MAX;
}


void
nk_dialogue_advance_clock(NkDialogueState *state_)
{
  if((state_ == NULL) || (state_->mode == NK_DIALOGUE_MODE_NONE) ||
     (state_->terminal))
    {
      return;
    }

  state_->tick++;
}


void
nk_dialogue_present(NkDialogueState *state_,
                    nk_rng          *rng_)
{
  const NkDialogueConversation *conversation;
  const NkDialogueEntry *entry;
  u32    duration;

  if((state_ == NULL) || (rng_ == NULL) || (state_->mode == NK_DIALOGUE_MODE_NONE) ||
     (state_->terminal))
    {
      return;
    }

  if((state_->current_valid) && (state_->tick <= state_->deadline))
    {
      return;
    }

  conversation = _nk_dialogue_conversation(
    state_->mode, state_->conversation_index
    );
  if(conversation == NULL)
    {
      state_->terminal = 1U;
      return;
    }

  state_->entry_index++;
  if((state_->entry_index < 0) ||
     (state_->entry_index >= (s32)conversation->entry_count))
    {
      state_->terminal = 1U;
      return;
    }

  entry = &nk_dialogue_entries[
    (u32)conversation->first_entry +
    (u32)state_->entry_index
          ];
  if(entry->person == NK_DIALOGUE_PERSON_DONE)
    {
      _nk_dialogue_reach_done(state_, rng_);
      return;
    }

  state_->person = entry->person;
  state_->current_valid = 1U;
  duration = entry->delay;
  if((entry->person != NK_DIALOGUE_PERSON_DELAY) && (duration == 0U))
    {
      duration = _nk_dialogue_automatic_delay(entry->text);
    }

  state_->deadline = state_->tick + duration;
}


void
nk_dialogue_step(NkDialogueState *state_,
                 nk_rng          *rng_)
{
  nk_dialogue_advance_clock(state_);
  nk_dialogue_present(state_, rng_);
}


const
NkDialogueEntry *
nk_dialogue_current(const NkDialogueState *state_)
{
  const NkDialogueConversation *conversation;

  if((state_ == NULL) || (!state_->current_valid) || (state_->entry_index < 0))
    {
      return NULL;
    }

  conversation = _nk_dialogue_conversation(
    state_->mode, state_->conversation_index
    );
  if((conversation == NULL) ||
     (state_->entry_index >= (s32)conversation->entry_count))
    {
      return NULL;
    }

  return &nk_dialogue_entries[
    (u32)conversation->first_entry +
    (u32)state_->entry_index
  ];
}


const
char *
nk_dialogue_current_text(const NkDialogueState *state_)
{
  const NkDialogueEntry *entry;

  entry = nk_dialogue_current(state_);
  if((entry == NULL) || (entry->person <= NK_DIALOGUE_PERSON_DELAY))
    {
      return NULL;
    }

  return entry->text;
}


bool
nk_dialogue_data_valid(void)
{
  const NkDialogueConversation *catalogs[NK_DIALOGUE_CATALOG_COUNT];
  u32    counts[NK_DIALOGUE_CATALOG_COUNT];
  u32    index;
  u32    conversation;

  catalogs[0] = nk_dialogue_ambient;
  catalogs[1] = nk_dialogue_win;
  catalogs[2] = nk_dialogue_loss;
  catalogs[3] = nk_dialogue_ending;
  catalogs[4] = nk_dialogue_post_match;
  counts[0] = NK_DIALOGUE_AMBIENT_COUNT;
  counts[1] = NK_DIALOGUE_WIN_COUNT;
  counts[2] = NK_DIALOGUE_LOSS_COUNT;
  counts[3] = 1U;
  counts[4] = 1U;
  for(index = 0U; index < NK_DIALOGUE_CATALOG_COUNT; ++index)
    {
      for(conversation = 0U;
          conversation < counts[index];
          ++conversation)
        {
          const NkDialogueConversation *item;
          const NkDialogueEntry *last;

          item = &catalogs[index][conversation];
          if((item->entry_count == 0U) ||
             (item->first_entry >= nk_dialogue_entry_count) ||
             (item->entry_count
                 > nk_dialogue_entry_count - item->first_entry))
            {
              return false;
            }

          last = &nk_dialogue_entries[
            item->first_entry + item->entry_count - 1U
                 ];
          if(last->person != NK_DIALOGUE_PERSON_DONE)
            {
              return false;
            }
        }
    }

  return true;
}
