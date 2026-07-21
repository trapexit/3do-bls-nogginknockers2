#include "nk_scene.h"

#include "stddef.h"

const
NkSceneBank *
nk_scene_bank(u8    bank_index_)
{
  if(bank_index_ >= NK_SCENE_BANK_COUNT)
    {
      return NULL;
    }

  return &nk_scene_banks[bank_index_];
}


const
NkSceneSeries *
nk_scene_series_def(u8    bank_index_,
                    u8    series_index_)
{
  const NkSceneBank *bank;

  bank = nk_scene_bank(bank_index_);
  if((bank == NULL) || (series_index_ >= bank->series_count))
    {
      return NULL;
    }

  return &nk_scene_series[bank->first_series + series_index_];
}


const
NkSceneFrame *
nk_scene_series_frame(const NkSceneState *state_,
                      u8                  series_index_)
{
  const NkSceneSeries *series;
  const NkSceneSeriesState *series_state;

  if((state_ == NULL) || (!state_->valid))
    {
      return NULL;
    }

  series = nk_scene_series_def(state_->bank_index, series_index_);
  if((series == NULL) || (series->frame_count == 0U))
    {
      return NULL;
    }

  series_state = &state_->series[series_index_];
  if(series_state->current_frame >= series->frame_count)
    {
      return NULL;
    }

  return &nk_scene_frames[series->first_frame
                          + series_state->current_frame];
}


bool
nk_scene_begin(NkSceneState *state_,
               u8            bank_index_)
{
  const NkSceneBank *bank;
  const NkSceneSeries *series;
  const NkSceneFrame *frame;
  int index;

  if(state_ == NULL)
    {
      return false;
    }

  bank = nk_scene_bank(bank_index_);
  state_->valid = 0U;
  if((bank == NULL) || (bank->series_count > NK_SCENE_SERIES_LIMIT))
    {
      return false;
    }

  state_->bank_index = bank_index_;
  for(index = 0; index < NK_SCENE_SERIES_LIMIT; ++index)
    {
      state_->series[index].current_frame = 0U;
      state_->series[index].remaining = 0;
    }

  state_->valid = 1U;
  for(index = 0; index < bank->series_count; ++index)
    {
      series = nk_scene_series_def(bank_index_, (u8)index);
      if((series != NULL) && (series->frame_count != 0U))
        {
          frame = nk_scene_series_frame(state_, (u8)index);
          if(frame == NULL)
            {
              state_->valid = 0U;
              return false;
            }

          state_->series[index].remaining = frame->duration_100hz;
        }
    }

  return true;
}


void
nk_scene_tick(NkSceneState *state_)
{
  const NkSceneBank *bank;
  const NkSceneSeries *series;
  const NkSceneFrame *frame;
  NkSceneSeriesState *series_state;
  int index;

  if((state_ == NULL) || (!state_->valid))
    {
      return;
    }

  bank = nk_scene_bank(state_->bank_index);
  if(bank == NULL)
    {
      state_->valid = 0U;
      return;
    }

  for(index = 0; index < bank->series_count; ++index)
    {
      series = nk_scene_series_def(state_->bank_index, (u8)index);
      if((series == NULL) || (series->frame_count == 0U))
        {
          continue;
        }

      series_state = &state_->series[index];
      if(series_state->remaining > 0)
        {
          series_state->remaining--;
          continue;
        }

      series_state->current_frame++;
      if(series_state->current_frame >= series->frame_count)
        {
          series_state->current_frame = 0U;
        }

      frame = nk_scene_series_frame(state_, (u8)index);
      if(frame == NULL)
        {
          state_->valid = 0U;
          return;
        }

      series_state->remaining = frame->duration_100hz;
    }
}


bool
nk_scene_series_range_valid(const NkSceneBank   *bank_,
                            const NkSceneSeries *series_)
{
  u32    bank_end;
  u32    series_end;

  if((bank_ == NULL) || (series_ == NULL))
    {
      return false;
    }

  bank_end = (u32)bank_->first_frame + bank_->frame_count;
  series_end = (u32)series_->first_frame + series_->frame_count;
  return series_->first_frame >= bank_->first_frame
         && series_end <= bank_end;
}


bool
nk_scene_data_valid(void)
{
  const NkSceneBank *bank;
  const NkSceneSeries *series;
  u32    end;
  int bank_index;
  int series_index;

  if((nk_scene_series_count == 0U) || (nk_scene_frame_count == 0U))
    {
      return false;
    }

  for(bank_index = 0; bank_index < NK_SCENE_BANK_COUNT; ++bank_index)
    {
      bank = &nk_scene_banks[bank_index];
      end = (u32)bank->first_series + bank->series_count;
      if((bank->series_count > NK_SCENE_SERIES_LIMIT)
         || (end > nk_scene_series_count)
         || ((u32)bank->first_frame + bank->frame_count
             > nk_scene_frame_count))
        {
          return false;
        }

      for(series_index = 0;
          series_index < bank->series_count;
          ++series_index)
        {
          series = &nk_scene_series[bank->first_series + series_index];
          if(!nk_scene_series_range_valid(bank, series))
            {
              return false;
            }
        }
    }

  return true;
}
