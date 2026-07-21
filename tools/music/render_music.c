/*
 * Offline YM3812 renderer for the preserved Noggin Knockers 2 music.
 *
 * Event parsing is shared with the source-preserving SDL reference port.  The
 * driver behavior below follows the original DOS OPL2FM.C: rhythm mode is
 * enabled, six channels are available for melody, MIDI velocity/controllers
 * and pitch bend are ignored, and TIMBRES.VOL bytes are written directly to
 * YM3812 registers.  ymfm provides the software chip implementation.
 */

#include "nk_music.h"
#include "ym3812_render.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define RENDER_EVENT_SAMPLE_RATE 44100UL
#define RENDER_MAX_SECONDS (30UL * 60UL)
#define RENDER_MELODIC_VOICES 6
#define RENDER_PHYSICAL_CHANNELS 11
#define RENDER_PERCUSSION_CHANNEL 9

typedef struct RenderPhysicalChannel
{
  unsigned long last_used;
  unsigned char ksl;
  unsigned char modlevel;
  signed char keymapping;
  unsigned char instrument_volume;
  int logical_channel;
  int note;
} RenderPhysicalChannel;

typedef struct RenderState
{
  const NkSong *song;
  NkYm3812 *chip;
  RenderPhysicalChannel physical[RENDER_PHYSICAL_CHANNELS];
  size_t event_index;
  unsigned long sample;
  unsigned long native_rate;
  unsigned long current_note;
  unsigned char percussion_control;
  int program[16];
} RenderState;

static const unsigned short g_RENDER_NOTES[128] =
{
  0x20ab, 0x20b6, 0x20c0, 0x20cc,
  0x20d8, 0x20e5, 0x20f2, 0x2101,
  0x2110, 0x2120, 0x2132, 0x2144,

  0x2157, 0x216b, 0x2181, 0x2198,
  0x21b0, 0x21ca, 0x21e5, 0x2202,
  0x2220, 0x2241, 0x2263, 0x2287,

  0x2557, 0x256b, 0x2581, 0x2598,
  0x25b0, 0x25ca, 0x25e5, 0x2602,
  0x2620, 0x2641, 0x2663, 0x2687,

  0x2957, 0x296b, 0x2981, 0x2998,
  0x29b0, 0x29ca, 0x29e5, 0x2a02,
  0x2a20, 0x2a41, 0x2a63, 0x2a87,

  0x2d57, 0x2d6b, 0x2d81, 0x2d98,
  0x2db0, 0x2dca, 0x2de5, 0x2e02,
  0x2e20, 0x2e41, 0x2e63, 0x2e87,

  0x3157, 0x316b, 0x3181, 0x3198,
  0x31b0, 0x31ca, 0x31e5, 0x3202,
  0x3220, 0x3241, 0x3263, 0x3287,

  0x3557, 0x356b, 0x3581, 0x3598,
  0x35b0, 0x35ca, 0x35e5, 0x3602,
  0x3620, 0x3641, 0x3663, 0x3687,

  0x3957, 0x396b, 0x3981, 0x3998,
  0x39b0, 0x39ca, 0x39e5, 0x3a02,
  0x3a20, 0x3a41, 0x3a63, 0x3a87,

  0x3d57, 0x3d6b, 0x3d81, 0x3d98,
  0x3db0, 0x3dca, 0x3de5, 0x3e02,
  0x3e20, 0x3e41, 0x3e63, 0x3e87,

  0x4157, 0x416b, 0x4181, 0x4198,
  0x41b0, 0x41ca, 0x41e5, 0x4202,
  0x4220, 0x4241, 0x4263, 0x4287,

  0x4557, 0x456b, 0x4581, 0x4598,
  0x0000, 0x0000, 0x0000, 0x0000
};

static
void
_write_u16_le(FILE        *file_,
              unsigned int value_);

static
void
_write_u32_le(FILE         *file_,
              unsigned long value_);

static
int
_clip_sample(int value_);

static
void
_set_register(RenderState *state_,
              unsigned int register_,
              unsigned int value_);

static
int
_operator_offset(int channel_,
                 int operator_);

static
void
_set_channel_register(RenderState *state_,
                      unsigned int register_,
                      int          channel_,
                      unsigned int value_);

static
void
_set_operator_register(RenderState *state_,
                       unsigned int register_,
                       int          channel_,
                       int          operator_,
                       unsigned int value_);

static
unsigned int
_slot_register(unsigned int register_,
               int          slot_);

static
const
NkTimbre *
_find_timbre(const RenderState *state_,
             int                id_);

static
void
_set_timbre(RenderState    *state_,
            int             physical_channel_,
            const NkTimbre *timbre_);

static
void
_set_percussion_slot(RenderState    *state_,
                     int             physical_channel_,
                     int             slot_,
                     const NkTimbre *timbre_);

static
void
_reset_synth(RenderState *state_);

static
int
_mapped_note(const RenderPhysicalChannel *physical_,
             int                          note_);

static
unsigned int
_carrier_level(const RenderPhysicalChannel *physical_);

static
int
_allocate_channel(RenderState *state_,
                  int          logical_channel_);

static
int
_deallocate_channel(RenderState *state_,
                    int          logical_channel_,
                    int          note_);

static
void
_start_note(RenderState *state_,
            int          logical_channel_,
            int          note_,
            int          velocity_);

static
void
_stop_note(RenderState *state_,
           int          logical_channel_,
           int          note_);

static
void
_change_program(RenderState *state_,
                int          logical_channel_,
                int          program_);

static
void
_apply_event(RenderState       *state_,
             const NkSongEvent *event_);

static
unsigned long
_native_sample_for_event(const RenderState *state_,
                         const NkSongEvent *event_);

static
void
_advance_events(RenderState *state_);

static
int
_load_timbres(const char *path_,
              NkTimbre   *timbres_,
              int        *count_);

static
void
_prepare_cmf_instruments(NkSong *song_);

static
int
_render_wav(const char   *path_,
            const NkSong *song_);


static
void
_write_u16_le(FILE        *file_,
              unsigned int value_)
{
  fputc((int)(value_ & 0xffU), file_);
  fputc((int)((value_ >> 8) & 0xffU), file_);
}


static
void
_write_u32_le(FILE         *file_,
              unsigned long value_)
{
  fputc((int)(value_ & 0xffUL), file_);
  fputc((int)((value_ >> 8) & 0xffUL), file_);
  fputc((int)((value_ >> 16) & 0xffUL), file_);
  fputc((int)((value_ >> 24) & 0xffUL), file_);
}


static
int
_clip_sample(int value_)
{
  if(value_ < -32768)
    {
      return -32768;
    }
  if(value_ > 32767)
    {
      return 32767;
    }
  return value_;
}


static
void
_set_register(RenderState *state_,
              unsigned int register_,
              unsigned int value_)
{
  nk_ym3812_write_register(state_->chip, register_, value_);
}


static
int
_operator_offset(int channel_,
                 int operator_)
{
  int offset;

  offset = channel_;
  if(channel_ > 2)
    {
      offset += 5;
    }
  if(channel_ > 5)
    {
      offset += 5;
    }
  if(operator_ != 0)
    {
      offset += 3;
    }
  return offset;
}


static
void
_set_channel_register(RenderState *state_,
                      unsigned int register_,
                      int          channel_,
                      unsigned int value_)
{
  _set_register(state_, register_ + (unsigned int)channel_, value_);
}


static
void
_set_operator_register(RenderState *state_,
                       unsigned int register_,
                       int          channel_,
                       int          operator_,
                       unsigned int value_)
{
  _set_register(
    state_,
    register_ + (unsigned int)_operator_offset(channel_, operator_),
    value_
    );
}


static
unsigned int
_slot_register(unsigned int register_,
               int          slot_)
{
  int offset;

  offset = slot_;
  if(slot_ > 5)
    {
      offset += 2;
    }
  if(slot_ > 11)
    {
      offset += 2;
    }
  return register_ + (unsigned int)offset;
}


static
const
NkTimbre *
_find_timbre(const RenderState *state_,
             int                id_)
{
  if((id_ < 0) || (id_ >= state_->song->instrument_count))
    {
      return NULL;
    }
  if(!state_->song->instruments[id_].present)
    {
      return NULL;
    }
  return &state_->song->instruments[id_];
}


static
void
_set_timbre(RenderState    *state_,
            int             physical_channel_,
            const NkTimbre *timbre_)
{
  RenderPhysicalChannel *physical;
  int operator_index;

  if(timbre_ == NULL)
    {
      return;
    }
  _set_operator_register(state_, 0x80U, physical_channel_, 1, 0xffU);
  _set_operator_register(state_, 0x80U, physical_channel_, 0, 0xffU);
  for(operator_index = 0; operator_index < 2; ++operator_index)
    {
      _set_operator_register(
        state_, 0x20U, physical_channel_, operator_index,
        timbre_->opl[operator_index]
        );
      _set_operator_register(
        state_, 0x40U, physical_channel_, operator_index,
        timbre_->opl[2 + operator_index]
        );
      _set_operator_register(
        state_, 0x60U, physical_channel_, operator_index,
        timbre_->opl[4 + operator_index]
        );
      _set_operator_register(
        state_, 0x80U, physical_channel_, operator_index,
        timbre_->opl[6 + operator_index]
        );
      _set_operator_register(
        state_, 0xe0U, physical_channel_, operator_index,
        timbre_->opl[8 + operator_index]
        );
    }
  _set_channel_register(
    state_, 0xc0U, physical_channel_, timbre_->opl[10]
    );
  physical = &state_->physical[physical_channel_];
  physical->ksl = (unsigned char)(timbre_->opl[3] & 0xc0U);
  physical->modlevel = timbre_->opl[2];
  physical->keymapping = (signed char)timbre_->opl[11];
  physical->instrument_volume = timbre_->opl[12];
}


static
void
_set_percussion_slot(RenderState    *state_,
                     int             physical_channel_,
                     int             slot_,
                     const NkTimbre *timbre_)
{
  RenderPhysicalChannel *physical;

  if(timbre_ == NULL)
    {
      return;
    }
  _set_register(
    state_, _slot_register(0x20U, slot_), timbre_->opl[0]
    );
  _set_register(
    state_, _slot_register(0x40U, slot_), timbre_->opl[2]
    );
  _set_register(
    state_, _slot_register(0x60U, slot_), timbre_->opl[4]
    );
  _set_register(
    state_, _slot_register(0x80U, slot_), timbre_->opl[6]
    );
  _set_register(
    state_, _slot_register(0xe0U, slot_), timbre_->opl[8]
    );
  _set_channel_register(
    state_, 0xc0U, physical_channel_, timbre_->opl[10]
    );
  physical = &state_->physical[physical_channel_];
  physical->ksl = (unsigned char)(timbre_->opl[2] & 0xc0U);
  physical->keymapping = (signed char)timbre_->opl[11];
  physical->instrument_volume = timbre_->opl[12];
}


static
void
_reset_synth(RenderState *state_)
{
  unsigned int register_index;
  int channel;

  nk_ym3812_reset(state_->chip);
  for(register_index = 0x01U; register_index <= 0xf5U; ++register_index)
    {
      _set_register(state_, register_index, 0U);
    }
  for(register_index = 0x40U; register_index <= 0x55U; ++register_index)
    {
      _set_register(state_, register_index, 0x3fU);
    }
  state_->percussion_control = 0x20U;
  _set_register(state_, 0xbdU, state_->percussion_control);
  for(channel = 0; channel < 16; ++channel)
    {
      state_->program[channel] = 0xff;
    }
  for(channel = 0; channel < RENDER_PHYSICAL_CHANNELS; ++channel)
    {
      state_->physical[channel].ksl = 0xffU;
      state_->physical[channel].modlevel = 0xffU;
      state_->physical[channel].keymapping = 0;
      state_->physical[channel].instrument_volume = 0U;
      state_->physical[channel].logical_channel = 0xff;
      state_->physical[channel].note = 0xff;
    }
}


static
int
_mapped_note(const RenderPhysicalChannel *physical_,
             int                          note_)
{
  int mapped;

  mapped = note_ + (int)physical_->keymapping;
  if(mapped < 0)
    {
      mapped = 0;
    }
  if(mapped > 127)
    {
      mapped = 127;
    }
  return mapped;
}


static
unsigned int
_carrier_level(const RenderPhysicalChannel *physical_)
{
  unsigned int attenuation;

  attenuation = 63U - (unsigned int)physical_->instrument_volume;
  if(attenuation > 63U)
    {
      attenuation = 63U;
    }
  return (unsigned int)physical_->ksl | attenuation;
}


static
int
_allocate_channel(RenderState *state_,
                  int          logical_channel_)
{
  const NkTimbre *timbre;
  unsigned long oldest;
  int chosen;
  int channel;

  chosen = -1;
  oldest = ~0UL;
  for(channel = 0; channel < RENDER_MELODIC_VOICES; ++channel)
    {
      if((state_->physical[channel].logical_channel == 0xff)
         && ((chosen == -1) || (state_->physical[channel].last_used < oldest)))
        {
          chosen = channel;
          oldest = state_->physical[channel].last_used;
        }
    }
  if(chosen == -1)
    {
      return -1;
    }
  timbre = _find_timbre(state_, state_->program[logical_channel_]);
  if(timbre == NULL)
    {
      return -1;
    }
  _set_timbre(state_, chosen, timbre);
  state_->physical[chosen].logical_channel = logical_channel_;
  return chosen;
}


static
int
_deallocate_channel(RenderState *state_,
                    int          logical_channel_,
                    int          note_)
{
  int channel;

  for(channel = 0; channel < RENDER_MELODIC_VOICES; ++channel)
    {
      if((state_->physical[channel].logical_channel == logical_channel_)
         && (state_->physical[channel].note == note_))
        {
          state_->physical[channel].logical_channel = 0xff;
          state_->physical[channel].note = 0xff;
          state_->physical[channel].last_used = ++state_->current_note;
          return channel;
        }
    }
  return -1;
}


static
void
_start_note(RenderState *state_,
            int          logical_channel_,
            int          note_,
            int          velocity_)
{
  RenderPhysicalChannel *physical;
  unsigned short frequency;
  int channel;
  int note_index;

  if(logical_channel_ == RENDER_PERCUSSION_CHANNEL)
    {
      if(note_ == 36)
        {
          physical = &state_->physical[6];
          _set_operator_register(
            state_, 0x40U, 6, 1, _carrier_level(physical)
            );
          note_index = _mapped_note(physical, 60);
          frequency = g_RENDER_NOTES[note_index];
          _set_channel_register(state_, 0xa0U, 6, frequency & 0xffU);
          _set_channel_register(
            state_, 0xb0U, 6,
            (unsigned int)((frequency >> 8) & 0xdfU)
            );
          state_->percussion_control =
            (unsigned char)(state_->percussion_control | 0x10U);
        }
      else if(note_ == 38)
        {
          physical = &state_->physical[7];
          _set_register(
            state_, _slot_register(0x40U, 16), _carrier_level(physical)
            );
          state_->percussion_control =
            (unsigned char)(state_->percussion_control | 0x08U);
        }
      else if((note_ == 39) || (note_ == 42))
        {
          physical = &state_->physical[10];
          _set_register(
            state_, _slot_register(0x40U, 13), _carrier_level(physical)
            );
          state_->percussion_control =
            (unsigned char)(state_->percussion_control | 0x01U);
        }
      _set_register(state_, 0xbdU, state_->percussion_control);
      return;
    }
  if(velocity_ == 0)
    {
      channel = _deallocate_channel(state_, logical_channel_, note_);
      if(channel != -1)
        {
          physical = &state_->physical[channel];
          note_index = _mapped_note(physical, note_);
          frequency = g_RENDER_NOTES[note_index];
          _set_channel_register(
            state_, 0xb0U, channel,
            (unsigned int)((frequency >> 8) & 0xdfU)
            );
        }
      return;
    }
  channel = _allocate_channel(state_, logical_channel_);
  if(channel == -1)
    {
      return;
    }
  physical = &state_->physical[channel];
  physical->note = note_;
  _set_operator_register(
    state_, 0x40U, channel, 1, _carrier_level(physical)
    );
  note_index = _mapped_note(physical, note_);
  frequency = g_RENDER_NOTES[note_index];
  _set_channel_register(state_, 0xa0U, channel, frequency & 0xffU);
  _set_channel_register(state_, 0xb0U, channel, frequency >> 8);
}


static
void
_stop_note(RenderState *state_,
           int          logical_channel_,
           int          note_)
{
  RenderPhysicalChannel *physical;
  unsigned short frequency;
  int channel;
  int note_index;

  if(logical_channel_ == RENDER_PERCUSSION_CHANNEL)
    {
      if(note_ == 36)
        {
          state_->percussion_control =
            (unsigned char)(state_->percussion_control & ~0x10U);
        }
      else if(note_ == 38)
        {
          state_->percussion_control =
            (unsigned char)(state_->percussion_control & ~0x08U);
        }
      else if(note_ == 39)
        {
          state_->percussion_control =
            (unsigned char)(state_->percussion_control & ~0x01U);
        }
      _set_register(state_, 0xbdU, state_->percussion_control);
      return;
    }
  channel = _deallocate_channel(state_, logical_channel_, note_);
  if(channel == -1)
    {
      return;
    }
  physical = &state_->physical[channel];
  note_index = _mapped_note(physical, note_);
  frequency = g_RENDER_NOTES[note_index];
  _set_channel_register(
    state_, 0xb0U, channel,
    (unsigned int)((frequency >> 8) & 0xdfU)
    );
}


static
void
_change_program(RenderState *state_,
                int          logical_channel_,
                int          program_)
{
  if(logical_channel_ != RENDER_PERCUSSION_CHANNEL)
    {
      if(_find_timbre(state_, program_) != NULL)
        {
          state_->program[logical_channel_] = program_;
        }
      return;
    }
  _set_timbre(state_, 6, _find_timbre(state_, 128 + 36));
  _set_percussion_slot(
    state_, 7, 16, _find_timbre(state_, 128 + 38)
    );
  _set_percussion_slot(
    state_, 10, 13, _find_timbre(state_, 128 + 42)
    );
  _set_register(state_, 0xa7U, (50U * 3U) & 0xffU);
  _set_register(state_, 0xb7U, (50U * 3U) >> 8);
  _set_register(state_, 0xa8U, 50U);
  _set_register(state_, 0xb8U, 0U);
}


static
void
_apply_event(RenderState       *state_,
             const NkSongEvent *event_)
{
  int channel;

  channel = (int)(event_->channel & 15U);
  switch(event_->type)
    {
    case NK_EVENT_NOTE_OFF:
      _stop_note(state_, channel, (int)event_->a);
      break;
    case NK_EVENT_NOTE_ON:
      _start_note(state_, channel, (int)event_->a, (int)event_->b);
      break;
    case NK_EVENT_PROGRAM:
      _change_program(state_, channel, (int)event_->a);
      break;
    default:
      break;
    }
}


static
unsigned long
_native_sample_for_event(const RenderState *state_,
                         const NkSongEvent *event_)
{
  return (event_->sample * state_->native_rate
          + RENDER_EVENT_SAMPLE_RATE / 2UL)
         / RENDER_EVENT_SAMPLE_RATE;
}


static
void
_advance_events(RenderState *state_)
{
  while((state_->event_index < state_->song->event_count)
        && (_native_sample_for_event(
          state_,
          &state_->song->events[state_->event_index]
          ) <= state_->sample))
    {
      _apply_event(state_, &state_->song->events[state_->event_index]);
      state_->event_index++;
    }
}


static
int
_load_timbres(const char *path_,
              NkTimbre   *timbres_,
              int        *count_)
{
  NkBlob bank;
  size_t position;
  int count;
  int id;
  int index;

  memset(timbres_, 0, sizeof(NkTimbre) * 256U);
  if(!nk_file_load(path_, &bank))
    {
      fprintf(stderr, "unable to read timbre bank: %s\n", path_);
      return 0;
    }
  if((bank.size < 6U) || (memcmp(bank.data, "OPL2\032", 5U) != 0))
    {
      fprintf(stderr, "invalid OPL2 timbre bank: %s\n", path_);
      nk_blob_free(&bank);
      return 0;
    }
  count = (int)bank.data[5];
  position = 6U;
  if((count <= 0) || ((size_t)count * 14U > bank.size - position))
    {
      fprintf(stderr, "truncated OPL2 timbre bank: %s\n", path_);
      nk_blob_free(&bank);
      return 0;
    }
  for(index = 0; index < count; ++index)
    {
      id = (int)bank.data[position];
      timbres_[id].id = (NkU8)id;
      memcpy(timbres_[id].opl, bank.data + position + 1U, 13U);
      timbres_[id].present = 1;
      position += 14U;
    }
  *count_ = 256;
  nk_blob_free(&bank);
  return 1;
}


static
void
_prepare_cmf_instruments(NkSong *song_)
{
  int embedded_count;
  int index;

  embedded_count = song_->instrument_count;
  for(index = 0; index < embedded_count; ++index)
    {
      if(song_->instruments[index].present)
        {
          song_->instruments[index].opl[11] = 0U;
          song_->instruments[index].opl[12] =
            (NkU8)(63U - (song_->instruments[index].opl[3] & 63U));
        }
    }
  /*
   * nk_song_load retains the fallback bank outside the embedded CMF range.
   * Expose those entries so the original rhythm-mode percussion patches at
   * IDs 164, 166, and 170 remain available.
   */
  song_->instrument_count = 256;
}


static
int
_render_wav(const char   *path_,
            const NkSong *song_)
{
  RenderState state;
  FILE *file;
  unsigned long data_size;
  unsigned long frame_count;
  unsigned long frame;
  int sample;

  memset(&state, 0, sizeof(state));
  state.song = song_;
  state.chip = nk_ym3812_create();
  if(state.chip == NULL)
    {
      fprintf(stderr, "unable to create YM3812 renderer\n");
      return 0;
    }
  state.native_rate = (unsigned long)nk_ym3812_sample_rate(state.chip);
  frame_count = (song_->length_samples * state.native_rate
                 + RENDER_EVENT_SAMPLE_RATE / 2UL)
                / RENDER_EVENT_SAMPLE_RATE;
  if((frame_count == 0UL)
     || (frame_count > RENDER_MAX_SECONDS * state.native_rate))
    {
      fprintf(stderr, "invalid rendered music duration\n");
      nk_ym3812_destroy(state.chip);
      return 0;
    }
  if(frame_count > (~0UL - 36UL) / 4UL)
    {
      fprintf(stderr, "rendered music is too large for WAV\n");
      nk_ym3812_destroy(state.chip);
      return 0;
    }
  data_size = frame_count * 4UL;
  file = fopen(path_, "wb");
  if(file == NULL)
    {
      fprintf(stderr, "unable to create WAV: %s\n", path_);
      nk_ym3812_destroy(state.chip);
      return 0;
    }
  fwrite("RIFF", 1, 4, file);
  _write_u32_le(file, 36UL + data_size);
  fwrite("WAVEfmt ", 1, 8, file);
  _write_u32_le(file, 16UL);
  _write_u16_le(file, 1U);
  _write_u16_le(file, 2U);
  _write_u32_le(file, state.native_rate);
  _write_u32_le(file, state.native_rate * 4UL);
  _write_u16_le(file, 4U);
  _write_u16_le(file, 16U);
  fwrite("data", 1, 4, file);
  _write_u32_le(file, data_size);

  _reset_synth(&state);
  for(frame = 0UL; frame < frame_count; ++frame)
    {
      _advance_events(&state);
      sample = _clip_sample(nk_ym3812_generate(state.chip));
      _write_u16_le(file, (unsigned int)((unsigned short)sample));
      _write_u16_le(file, (unsigned int)((unsigned short)sample));
      state.sample++;
    }
  nk_ym3812_destroy(state.chip);
  if(fclose(file) != 0)
    {
      fprintf(stderr, "unable to finish WAV: %s\n", path_);
      return 0;
    }
  return 1;
}


int
main(int    argc_,
     char **argv_)
{
  NkBlob music;
  NkSong song;
  NkTimbre timbres[256];
  char error[256];
  int timbre_count;
  int result;

  if(argc_ != 4)
    {
      fprintf(
        stderr,
        "usage: %s MUSIC TIMBRES.VOL OUTPUT.wav\n",
        argv_[0]
        );
      return 2;
    }
  if(!nk_file_load(argv_[1], &music))
    {
      fprintf(stderr, "unable to read music stream: %s\n", argv_[1]);
      return 1;
    }
  if(!_load_timbres(argv_[2], timbres, &timbre_count))
    {
      nk_blob_free(&music);
      return 1;
    }
  error[0] = '\0';
  if(!nk_song_load(
       &song,
       &music,
       timbres,
       timbre_count,
       error,
       (size_t)sizeof(error)))
    {
      fprintf(stderr, "unable to parse music: %s\n", error);
      nk_blob_free(&music);
      return 1;
    }
  if(memcmp(music.data, "CTMF", 4U) == 0)
    {
      _prepare_cmf_instruments(&song);
    }
  result = _render_wav(argv_[3], &song);
  printf(
    "rendered %s: %lu event-rate frames, %lu events\n",
    argv_[3],
    song.length_samples,
    (unsigned long)song.event_count
    );
  nk_song_free(&song);
  nk_blob_free(&music);
  return result ? 0 : 1;
}
