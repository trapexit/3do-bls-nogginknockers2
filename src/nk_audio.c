/*
 * Portfolio sound adapter extending the original six-voice DOS software
 * mixer with the spare input recovered by streaming background music as mono.
 *
 * All converted samples are byte-identical copies of the exact signed
 * 8-bit/16 kHz source PCM AIFFs.  A rate-variable 8-bit player preserves
 * their original pitch and duration on the 44.1 kHz Portfolio DSP.  The
 * source mixer selected the first free voice and dropped sounds when every
 * voice was occupied; this adapter retains that policy across seven voices.
 */

#include "nk_audio.h"

#include "nk_select.h"

#include "blockfile.h"
#include "debug.h"
#include "mem.h"

#include "stdio.h"
#include "string.h"

#define NK_AUDIO_MIXER_NAME ("mixer8x2.dsp")
#define NK_AUDIO_PLAYER_NAME ("varmono8.dsp")
#define NK_AUDIO_SOURCE_RATE (16000L)
#define NK_AUDIO_OUTPUT_RATE (44100L)
#define NK_AUDIO_PLAYBACK_RATE \
  ((NK_AUDIO_SOURCE_RATE * 0x8000L + NK_AUDIO_OUTPUT_RATE / 2L) \
   / NK_AUDIO_OUTPUT_RATE)
/* DOS SMIX.C scales an 8-bit sample by 75 into its signed 16-bit mix. */
#define NK_AUDIO_CHANNEL_GAIN (9600)

#define NK_AUDIO_PACKED_MAGIC (0x4e4b4142UL)
#define NK_AUDIO_PACKED_VERSION (1U)
#define NK_AUDIO_PACKED_HEADER_SIZE (24U)
#define NK_AUDIO_PACKED_ENTRY_SIZE (12U)
#define NK_AUDIO_PACKED_LOGICAL_LIMIT (NK_AUDIO_SCREAM_LOGICAL_COUNT)
#define NK_AUDIO_HEAD_BANK_PATH ("nog2/audio/head.nkab")
#define NK_AUDIO_SCREAM_BANK_PATH ("nog2/audio/select_screams.nkab")

#define NK_AUDIO_PACKED_VERSION_OFFSET     (4U)
#define NK_AUDIO_PACKED_SOURCE_RATE_OFFSET (8U)
#define NK_AUDIO_PACKED_COUNT_OFFSET       (12U)
#define NK_AUDIO_PACKED_DATA_OFFSET        (16U)
#define NK_AUDIO_PACKED_FILE_SIZE_OFFSET   (20U)

#define NK_AUDIO_PACKED_ENTRY_MEMBER_OFFSET     (4U)
#define NK_AUDIO_PACKED_ENTRY_MEMBER_SIZE       (8U)
#define NK_AUDIO_PACKED_ALIGNMENT               (4U)
#define NK_AUDIO_BANK_SAMPLE_INDEX_BASE         (5)

static const char *g_AUDIO_CHARACTER_DIRECTORIES[NK_CHARACTER_COUNT] =
{
  "klubbor",
  "fetus",
  "henry",
  "gurdip",
  "ed",
  "sinammon",
  "buddy",
  "gonzoles"
};

static const char *g_AUDIO_INPUT_NAMES[NK_AUDIO_VOICE_COUNT] =
{
  "Input0",
  "Input1",
  "Input2",
  "Input3",
  "Input4",
  "Input5",
  "Input6"
};

static const char *g_AUDIO_LEFT_GAIN_NAMES[NK_AUDIO_VOICE_COUNT] =
{
  "LeftGain0",
  "LeftGain1",
  "LeftGain2",
  "LeftGain3",
  "LeftGain4",
  "LeftGain5",
  "LeftGain6"
};

static const char *g_AUDIO_RIGHT_GAIN_NAMES[NK_AUDIO_VOICE_COUNT] =
{
  "RightGain0",
  "RightGain1",
  "RightGain2",
  "RightGain3",
  "RightGain4",
  "RightGain5",
  "RightGain6"
};


static
u32
_nk_audio_read_be32(const u8    *data_)
{
  return ((u32)data_[0] << 24)
         | ((u32)data_[1] << 16)
         | ((u32)data_[2] << 8)
         | (u32)data_[3];
}


static
u32
_nk_audio_align_four(u32    value_)
{
  return (value_ + NK_AUDIO_PACKED_ALIGNMENT - 1U) & ~(NK_AUDIO_PACKED_ALIGNMENT - 1U);
}


static
bool
_nk_audio_parse_packed_bank(const void *image_,
                            s32         image_size_,
                            u32         offsets_[NK_AUDIO_PACKED_LOGICAL_LIMIT],
                            u32         sizes_[NK_AUDIO_PACKED_LOGICAL_LIMIT],
                            u32        *available_mask_,
                            u32        *maximum_size_)
{
  const u8    *bytes;
  u32    count;
  u32    data_offset;
  u32    entry_offset;
  u32    file_size;
  u32    index;
  u32    logical_id;
  u32    member_offset;
  u32    member_size;
  u32    previous_end;

  if((image_ == NULL) || (image_size_ < (s32)NK_AUDIO_PACKED_HEADER_SIZE) ||
     (offsets_ == NULL) || (sizes_ == NULL) ||
     (available_mask_ == NULL) || (maximum_size_ == NULL))
    {
      return false;
    }

  bytes = (const u8 *)image_;
  if((_nk_audio_read_be32(bytes) != NK_AUDIO_PACKED_MAGIC) ||
     (_nk_audio_read_be32(bytes + NK_AUDIO_PACKED_VERSION_OFFSET) != NK_AUDIO_PACKED_VERSION) ||
     (_nk_audio_read_be32(bytes + NK_AUDIO_PACKED_SOURCE_RATE_OFFSET) != NK_AUDIO_SOURCE_RATE))
    {
      return false;
    }

  count = _nk_audio_read_be32(bytes + NK_AUDIO_PACKED_COUNT_OFFSET);
  data_offset = _nk_audio_read_be32(bytes + NK_AUDIO_PACKED_DATA_OFFSET);
  file_size = _nk_audio_read_be32(bytes + NK_AUDIO_PACKED_FILE_SIZE_OFFSET);
  if((count == 0U) || (count > NK_AUDIO_PACKED_LOGICAL_LIMIT) ||
     (file_size != (u32)image_size_) ||
     (data_offset < NK_AUDIO_PACKED_HEADER_SIZE
         + count * NK_AUDIO_PACKED_ENTRY_SIZE) ||
     (data_offset > file_size) ||
     ((data_offset & (NK_AUDIO_PACKED_ALIGNMENT - 1U)) != 0U))
    {
      return false;
    }

  memset(offsets_, 0, sizeof(u32) * NK_AUDIO_PACKED_LOGICAL_LIMIT);
  memset(sizes_, 0, sizeof(u32) * NK_AUDIO_PACKED_LOGICAL_LIMIT);
  *available_mask_ = 0U;
  *maximum_size_ = 0U;
  previous_end = data_offset;
  for(index = 0U; index < count; ++index)
    {
      entry_offset = NK_AUDIO_PACKED_HEADER_SIZE
                     + index * NK_AUDIO_PACKED_ENTRY_SIZE;
      logical_id = _nk_audio_read_be32(bytes + entry_offset);
      member_offset = _nk_audio_read_be32(
        bytes + entry_offset + NK_AUDIO_PACKED_ENTRY_MEMBER_OFFSET
        );
      member_size = _nk_audio_read_be32(bytes + entry_offset + NK_AUDIO_PACKED_ENTRY_MEMBER_SIZE);
      if((logical_id >= NK_AUDIO_PACKED_LOGICAL_LIMIT) ||
         ((*available_mask_ & (1UL << logical_id)) != 0U) ||
         (member_size == 0U) ||
         (member_offset != _nk_audio_align_four(previous_end)) ||
         (member_offset > file_size) ||
         (member_size > file_size - member_offset))
        {
          return false;
        }

      offsets_[logical_id] = member_offset;
      sizes_[logical_id] = member_size;
      *available_mask_ |= (u32)1U << logical_id;
      if(member_size > *maximum_size_)
        {
          *maximum_size_ = member_size;
        }

      previous_end = member_offset + member_size;
    }

  return previous_end == file_size;
}


static
Item
_nk_audio_create_memory_sample(void  *address_,
                               u32    size_)
{
  return CreateSampleVA(
    AF_TAG_ADDRESS,
    address_,
    AF_TAG_WIDTH,
    1,
    AF_TAG_CHANNELS,
    1,
    AF_TAG_FRAMES,
    size_,
    TAG_END
    );
}


static
void
_nk_audio_reset(NkAudio *audio_)
{
  int bank_index;
  int sample_index;
  int select_index;
  int voice_index;

  memset(audio_, 0, sizeof(*audio_));
  audio_->mixer = -1;
  audio_->clock_owner = -1;
  audio_->scream_sample = -1;
  for(voice_index = 0;
      voice_index < NK_AUDIO_VOICE_COUNT;
      ++voice_index)
    {
      audio_->voices[voice_index].player = -1;
      audio_->voices[voice_index].attachment = -1;
      audio_->voices[voice_index].left_gain = -1;
      audio_->voices[voice_index].right_gain = -1;
    }

  for(bank_index = 0;
      bank_index < NK_AUDIO_BANK_COUNT;
      ++bank_index)
    {
      audio_->banks[bank_index].available_mask = 0U;
      audio_->banks[bank_index].owner = NK_AUDIO_BANK_OWNER_NONE;
      audio_->banks[bank_index].storage = NULL;
      audio_->banks[bank_index].storage_size = 0;
      for(sample_index = 0;
          sample_index < NK_AUDIO_COMBAT_SAMPLE_COUNT;
          ++sample_index)
        {
          audio_->banks[bank_index].samples[sample_index] = -1;
        }
    }

  for(select_index = 0;
      select_index < NK_AUDIO_SELECT_SAMPLE_COUNT;
      ++select_index)
    {
      audio_->select_samples[select_index] = -1;
    }
}


static
void
_nk_audio_unload_bank(NkAudioBank *bank_)
{
  Err error;
  int sample_index;

  for(sample_index = 0;
      sample_index < NK_AUDIO_COMBAT_SAMPLE_COUNT;
      ++sample_index)
    {
      if(bank_->samples[sample_index] >= 0)
        {
          error = UnloadSample(bank_->samples[sample_index]);
          if(error < 0)
            {
              kprintf("NK2 audio unload sample failed item=%ld error=%ld\n",
                      (long)bank_->samples[sample_index],
                      (long)error);
            }

          bank_->samples[sample_index] = -1;
        }
    }

  if(bank_->storage != NULL)
    {
      UnloadFile(bank_->storage);
      bank_->storage = NULL;
      bank_->storage_size = 0;
    }

  bank_->available_mask = 0U;
  bank_->owner = NK_AUDIO_BANK_OWNER_NONE;
}


static
bool
_nk_audio_load_packed_combat_bank(NkAudioBank *bank_,
                                  const char  *path_,
                                  u32          available_mask_,
                                  u8           owner_)
{
  u32    bank_mask;
  u32    expected_mask;
  u32    maximum_size;
  u32    offsets[NK_AUDIO_PACKED_LOGICAL_LIMIT];
  u32    sizes[NK_AUDIO_PACKED_LOGICAL_LIMIT];
  u8    *image;
  int32 loaded_size;
  int sample_index;

  loaded_size = 0;
  bank_->storage = LoadFile(path_, &loaded_size, MEMTYPE_AUDIO);
  if(bank_->storage == NULL)
    {
      kprintf("NK2 audio bank load failed: %s\n", path_);
      return false;
    }

  bank_->storage_size = (s32)loaded_size;
  if(!_nk_audio_parse_packed_bank(
       bank_->storage,
       bank_->storage_size,
       offsets,
       sizes,
       &bank_mask,
       &maximum_size))
    {
      kprintf("NK2 audio bank invalid: %s\n", path_);
      _nk_audio_unload_bank(bank_);
      return false;
    }

  (void)maximum_size;
  expected_mask = 0U;
  for(sample_index = 0;
      sample_index < NK_AUDIO_COMBAT_SAMPLE_COUNT;
      ++sample_index)
    {
      if((available_mask_ & (1UL << sample_index)) != 0U)
        {
          expected_mask |= (u32)1U << (sample_index + NK_AUDIO_BANK_SAMPLE_INDEX_BASE);
        }
    }

  if(bank_mask != expected_mask)
    {
      kprintf("NK2 audio bank mask mismatch: %s\n", path_);
      _nk_audio_unload_bank(bank_);
      return false;
    }

  image = (u8 *)bank_->storage;
  for(sample_index = 0;
      sample_index < NK_AUDIO_COMBAT_SAMPLE_COUNT;
      ++sample_index)
    {
      if((available_mask_ & (1UL << sample_index)) == 0U)
        {
          continue;
        }

      bank_->samples[sample_index] = _nk_audio_create_memory_sample(
        image + offsets[sample_index + NK_AUDIO_BANK_SAMPLE_INDEX_BASE],
        sizes[sample_index + NK_AUDIO_BANK_SAMPLE_INDEX_BASE]
        );
      if(bank_->samples[sample_index] < 0)
        {
          kprintf("NK2 audio sample create failed bank=%s sample=%d\n",
                  path_,
                  sample_index + NK_AUDIO_BANK_SAMPLE_INDEX_BASE);
          _nk_audio_unload_bank(bank_);
          return false;
        }
    }

  bank_->available_mask = available_mask_;
  bank_->owner = owner_;
  kprintf("NK2 audio bank loaded path=%s bytes=%ld samples=%u\n",
          path_,
          (long)bank_->storage_size,
          (unsigned int)_nk_audio_read_be32(image + NK_AUDIO_PACKED_COUNT_OFFSET));
  return true;
}


static
bool
_nk_audio_ensure_bank(NkAudio    *audio_,
                      int         bank_index_,
                      u8          owner_,
                      const char *directory_,
                      u32         available_mask_)
{
  NkAudioBank *bank;
  char path[96];

  bank = &audio_->banks[bank_index_];
  if((bank->owner == owner_) &&
     (bank->available_mask == available_mask_))
    {
      kprintf("NK2 audio cache hit bank=%d owner=%u path=%s\n",
              bank_index_,
              (unsigned int)owner_,
              directory_);
      return true;
    }

  _nk_audio_unload_bank(bank);
  kprintf("NK2 audio cache miss bank=%d owner=%u path=%s\n",
          bank_index_,
          (unsigned int)owner_,
          directory_);
  if(owner_ == NK_AUDIO_BANK_OWNER_HEAD)
    {
      strcpy(path, NK_AUDIO_HEAD_BANK_PATH);
    }
  else
    {
      sprintf(path, "nog2/audio/%s.nkab", directory_);
    }

  return _nk_audio_load_packed_combat_bank(
    bank,
    path,
    available_mask_,
    owner_
    );
}


static
bool
_nk_audio_initialize_voice(NkAudio      *audio_,
                           NkAudioVoice *voice_,
                           int           voice_index_)
{
  Err error;

  voice_->player = LoadInstrument(NK_AUDIO_PLAYER_NAME, 0, 100);
  if(voice_->player < 0)
    {
      kprintf("NK2 audio load failed instrument=%s voice=%d error=%ld\n",
              NK_AUDIO_PLAYER_NAME,
              voice_index_,
              (long)voice_->player);
      return false;
    }

  kprintf("NK2 audio loaded instrument=%s voice=%d\n",
          NK_AUDIO_PLAYER_NAME,
          voice_index_);
  error = ConnectInstruments(
    voice_->player,
    "Output",
    audio_->mixer,
    (char *)g_AUDIO_INPUT_NAMES[voice_index_]
    );
  if(error < 0)
    {
      kprintf("NK2 audio voice connect failed voice=%d error=%ld\n",
              voice_index_,
              (long)error);
      return false;
    }

  voice_->left_gain = GrabKnob(
    audio_->mixer,
    (char *)g_AUDIO_LEFT_GAIN_NAMES[voice_index_]
    );
  if(voice_->left_gain < 0)
    {
      kprintf("NK2 audio left gain grab failed voice=%d error=%ld\n",
              voice_index_,
              (long)voice_->left_gain);
      return false;
    }

  voice_->right_gain = GrabKnob(
    audio_->mixer,
    (char *)g_AUDIO_RIGHT_GAIN_NAMES[voice_index_]
    );
  if(voice_->right_gain < 0)
    {
      kprintf("NK2 audio right gain grab failed voice=%d error=%ld\n",
              voice_index_,
              (long)voice_->right_gain);
      return false;
    }

  error = TweakKnob(voice_->left_gain, NK_AUDIO_CHANNEL_GAIN);
  if(error < 0)
    {
      kprintf("NK2 audio left gain update failed voice=%d error=%ld\n",
              voice_index_,
              (long)error);
      return false;
    }

  error = TweakKnob(voice_->right_gain, NK_AUDIO_CHANNEL_GAIN);
  if(error < 0)
    {
      kprintf("NK2 audio right gain update failed voice=%d error=%ld\n",
              voice_index_,
              (long)error);
    }

  return error >= 0;
}


void
nk_audio_stop_all(NkAudio *audio_)
{
  Err error;
  NkAudioVoice *voice;
  int voice_index;

  if(audio_ == NULL)
    {
      return;
    }

  if((audio_->initialized == 0U) && (audio_->mixer == 0))
    {
      return;
    }

  for(voice_index = 0;
      voice_index < NK_AUDIO_VOICE_COUNT;
      ++voice_index)
    {
      voice = &audio_->voices[voice_index];
      if(voice->player >= 0)
        {
          error = StopInstrument(voice->player, NULL);
          if(error < 0)
            {
              kprintf("NK2 audio stop failed voice=%d error=%ld\n",
                      voice_index,
                      (long)error);
            }
        }

      if(voice->attachment >= 0)
        {
          error = DetachSample(voice->attachment);
          if(error < 0)
            {
              kprintf("NK2 audio detach failed voice=%d attachment=%ld error=%ld\n",
                      voice_index,
                      (long)voice->attachment,
                      (long)error);
            }

          voice->attachment = -1;
        }
    }
}


void
nk_audio_shutdown(NkAudio *audio_)
{
  Err error;
  NkAudioVoice *voice;
  int voice_index;

  if(audio_ == NULL)
    {
      return;
    }

  /* A zero-filled runtime has not entered nk_audio_initialize() yet. */
  if((audio_->initialized == 0U) && (audio_->mixer == 0))
    {
      return;
    }

  for(voice_index = 0;
      voice_index < NK_AUDIO_VOICE_COUNT;
      ++voice_index)
    {
      voice = &audio_->voices[voice_index];
      if(voice->player >= 0)
        {
          error = StopInstrument(voice->player, NULL);
          if(error < 0)
            {
              kprintf("NK2 audio teardown stop failed voice=%d error=%ld\n",
                      voice_index,
                      (long)error);
            }
        }

      if(voice->attachment >= 0)
        {
          error = DetachSample(voice->attachment);
          if(error < 0)
            {
              kprintf("NK2 audio teardown detach failed voice=%d error=%ld\n",
                      voice_index,
                      (long)error);
            }

          voice->attachment = -1;
        }

      if(voice->left_gain >= 0)
        {
          error = ReleaseKnob(voice->left_gain);
          if(error < 0)
            {
              kprintf("NK2 audio teardown left gain release failed "
                      "voice=%d error=%ld\n",
                      voice_index,
                      (long)error);
            }

          voice->left_gain = -1;
        }

      if(voice->right_gain >= 0)
        {
          error = ReleaseKnob(voice->right_gain);
          if(error < 0)
            {
              kprintf("NK2 audio teardown right gain release failed "
                      "voice=%d error=%ld\n",
                      voice_index,
                      (long)error);
            }

          voice->right_gain = -1;
        }

      if(voice->player >= 0)
        {
          error = UnloadInstrument(voice->player);
          if(error < 0)
            {
              kprintf("NK2 audio teardown instrument unload failed "
                      "voice=%d error=%ld\n",
                      voice_index,
                      (long)error);
            }

          voice->player = -1;
        }
    }

  nk_audio_unload_combat(audio_);
  nk_audio_unload_select(audio_);
  nk_audio_unload_screams(audio_);
  if(audio_->mixer >= 0)
    {
      error = StopInstrument(audio_->mixer, NULL);
      if(error < 0)
        {
          kprintf("NK2 audio mixer stop failed error=%ld\n", (long)error);
        }

      error = UnloadInstrument(audio_->mixer);
      if(error < 0)
        {
          kprintf("NK2 audio mixer unload failed error=%ld\n", (long)error);
        }

      audio_->mixer = -1;
    }

  if(audio_->clock_owner >= 0)
    {
      (void)DisownAudioClock(audio_->clock_owner);
      audio_->clock_owner = -1;
    }

  audio_->clock_rate_f16 = 0U;
  audio_->initialized = 0U;
}


bool
nk_audio_initialize(NkAudio *audio_)
{
  Err error;
  int voice_index;

  if(audio_ == NULL)
    {
      return false;
    }

  _nk_audio_reset(audio_);
  audio_->clock_owner = OwnAudioClock();
  if(audio_->clock_owner < 0)
    {
      kprintf("NK2 audio clock ownership failed error=%ld\n",
              (long)audio_->clock_owner);
      nk_audio_shutdown(audio_);
      return false;
    }

  error = GetAudioRate();
  if(error <= 0)
    {
      kprintf("NK2 audio clock rate query failed error=%ld\n", (long)error);
      nk_audio_shutdown(audio_);
      return false;
    }

  /*
   * Portfolio's nominal 240 Hz request is realized with whole DSP sample
   * frames.  At 44,100 Hz, 184 frames per tick produces 239.6739... Hz.
   * Preserve the folio's 16.16 rate so the 100 Hz game clock does not drift.
   */
  audio_->clock_rate_f16 = (u32)error;
  if((audio_->clock_rate_f16 < (60U << 16)) ||
     (audio_->clock_rate_f16 > (1000U << 16)))
    {
      kprintf("NK2 audio clock rate invalid value=%lu\n",
              (unsigned long)audio_->clock_rate_f16);
      nk_audio_shutdown(audio_);
      return false;
    }

  kprintf("NK2 audio clock=%lu.%04lu Hz\n",
          ((unsigned long)audio_->clock_rate_f16) >> 16,
          ((((unsigned long)audio_->clock_rate_f16 & 0xffffUL) * 10000UL
            + 0x8000UL) >> 16));
  audio_->mixer = LoadInstrument(NK_AUDIO_MIXER_NAME, 0, 100);
  if(audio_->mixer < 0)
    {
      kprintf("NK2 audio load failed instrument=%s error=%ld\n",
              NK_AUDIO_MIXER_NAME,
              (long)audio_->mixer);
      nk_audio_shutdown(audio_);
      return false;
    }

  kprintf("NK2 audio loaded instrument=%s\n", NK_AUDIO_MIXER_NAME);
  error = StartInstrument(audio_->mixer, NULL);
  if(error < 0)
    {
      kprintf("NK2 audio mixer start failed error=%ld\n", (long)error);
      nk_audio_shutdown(audio_);
      return false;
    }

  for(voice_index = 0;
      voice_index < NK_AUDIO_VOICE_COUNT;
      ++voice_index)
    {
      if(!_nk_audio_initialize_voice(
           audio_,
           &audio_->voices[voice_index],
           voice_index))
        {
          nk_audio_shutdown(audio_);
          return false;
        }
    }

  audio_->initialized = 1U;
  return true;
}


u32
nk_audio_clock_rate_f16(const NkAudio *audio_)
{
  if(audio_ == NULL)
    {
      return 0U;
    }

  return audio_->clock_rate_f16;
}


void
nk_audio_unload_combat(NkAudio *audio_)
{
  int bank_index;

  if(audio_ == NULL)
    {
      return;
    }

  if((audio_->initialized == 0U) && (audio_->mixer == 0))
    {
      return;
    }

  nk_audio_stop_all(audio_);
  for(bank_index = 0;
      bank_index < NK_AUDIO_BANK_COUNT;
      ++bank_index)
    {
      _nk_audio_unload_bank(&audio_->banks[bank_index]);
    }
}


void
nk_audio_unload_opponent(NkAudio *audio_)
{
  if(audio_ == NULL)
    {
      return;
    }

  if((audio_->initialized == 0U) && (audio_->mixer == 0))
    {
      return;
    }

  nk_audio_stop_all(audio_);
  _nk_audio_unload_bank(&audio_->banks[1]);
}


bool
nk_audio_load_combat(NkAudio *audio_,
                     u8       player_zero_type_,
                     u8       player_one_type_)
{
  if((audio_ == NULL) || (!audio_->initialized) ||
     (player_zero_type_ >= NK_CHARACTER_COUNT) ||
     (player_one_type_ >= NK_CHARACTER_COUNT))
    {
      return false;
    }

  nk_audio_stop_all(audio_);
  if((!_nk_audio_ensure_bank(
        audio_,
        2,
        NK_AUDIO_BANK_OWNER_HEAD,
        "head",
        /* Frame sound 5 plus direct hit/score sound types 1 through 5. */
        nk_anim_bank_sound_mask(8U) | (u32)0x0f80U)) ||
     (!_nk_audio_ensure_bank(
           audio_,
           0,
           player_zero_type_,
           g_AUDIO_CHARACTER_DIRECTORIES[player_zero_type_],
           nk_anim_bank_sound_mask(player_zero_type_))) ||
     (!_nk_audio_ensure_bank(
           audio_,
           1,
           player_one_type_,
           g_AUDIO_CHARACTER_DIRECTORIES[player_one_type_],
           nk_anim_bank_sound_mask(player_one_type_))))
    {
      return false;
    }

  return true;
}


bool
nk_audio_load_select(NkAudio *audio_)
{
  char path[96];
  int select_index;

  if((audio_ == NULL) || (!audio_->initialized))
    {
      return false;
    }

  if((audio_->select_samples[0] >= 0) &&
     (audio_->select_samples[1] >= 0))
    {
      kprintf("NK2 audio cache hit bank=select3\n");
      return true;
    }

  nk_audio_unload_select(audio_);
  kprintf("NK2 audio cache miss bank=select3\n");
  for(select_index = 0;
      select_index < NK_AUDIO_SELECT_SAMPLE_COUNT;
      ++select_index)
    {
      sprintf(
        path,
        "nog2/select3/audio/s%03d.aiff",
        select_index + 1
        );
      audio_->select_samples[select_index] = LoadSample(path);
      if(audio_->select_samples[select_index] < 0)
        {
          kprintf("NK2 audio load failed sample=%s\n", path);
          nk_audio_unload_select(audio_);
          return false;
        }

      kprintf("NK2 audio loaded sample=%s\n", path);
    }

  return true;
}


void
nk_audio_unload_select(NkAudio *audio_)
{
  int select_index;

  if(audio_ == NULL)
    {
      return;
    }

  if((audio_->initialized == 0U) && (audio_->mixer == 0))
    {
      return;
    }

  nk_audio_stop_all(audio_);
  for(select_index = 0;
      select_index < NK_AUDIO_SELECT_SAMPLE_COUNT;
      ++select_index)
    {
      if(audio_->select_samples[select_index] >= 0)
        {
          UnloadSample(audio_->select_samples[select_index]);
          audio_->select_samples[select_index] = -1;
        }
    }
}


static
u32
_nk_audio_expected_scream_mask(void)
{
  u32    mask;
  int character;
  int scream_index;

  mask = 0U;
  for(character = 0; character < NK_CHARACTER_COUNT; ++character)
    {
      for(scream_index = 0;
          scream_index < NK_AUDIO_SCREAM_VARIANT_COUNT;
          ++scream_index)
        {
          if((nk_select_scream_masks[character]
              & (1U << scream_index)) != 0U)
            {
              mask |= (u32)1U << (
                character * NK_AUDIO_SCREAM_VARIANT_COUNT
                + scream_index
                  );
            }
        }
    }

  return mask;
}


static
bool
_nk_audio_allocate_scream_buffer(NkAudio *audio_,
                                 u32      maximum_size_)
{
  audio_->scream_buffer = AllocMem(
    (int32)maximum_size_,
    MEMTYPE_AUDIO
    );
  if(audio_->scream_buffer != NULL)
    {
      audio_->scream_buffer_size = (s32)maximum_size_;
      return true;
    }

  if(audio_->banks[1].owner == NK_AUDIO_BANK_OWNER_NONE)
    {
      return false;
    }

  kprintf("NK2 audio cache evict bank=opponent reason=scream_buffer_retry\n");
  nk_audio_unload_opponent(audio_);
  audio_->scream_buffer = AllocMem(
    (int32)maximum_size_,
    MEMTYPE_AUDIO
    );
  if(audio_->scream_buffer == NULL)
    {
      return false;
    }

  audio_->scream_buffer_size = (s32)maximum_size_;
  return true;
}


bool
nk_audio_load_screams(NkAudio *audio_)
{
  u32    maximum_size;
  int32 loaded_size;

  if((audio_ == NULL) || (!audio_->initialized))
    {
      return false;
    }

  if(audio_->scream_bank == NULL)
    {
      loaded_size = 0;
      audio_->scream_bank = LoadFile(
        NK_AUDIO_SCREAM_BANK_PATH,
        &loaded_size,
        MEMTYPE_VRAM
        );
      if(audio_->scream_bank == NULL)
        {
          kprintf("NK2 audio bank load failed: %s\n",
                  NK_AUDIO_SCREAM_BANK_PATH);
          return false;
        }

      audio_->scream_bank_size = (s32)loaded_size;
      if((!_nk_audio_parse_packed_bank(
            audio_->scream_bank,
            audio_->scream_bank_size,
            audio_->scream_offsets,
            audio_->scream_sizes,
            &audio_->scream_available_mask,
            &maximum_size)) ||
         (audio_->scream_available_mask
             != _nk_audio_expected_scream_mask()))
        {
          kprintf("NK2 audio bank invalid: %s\n",
                  NK_AUDIO_SCREAM_BANK_PATH);
          nk_audio_unload_screams(audio_);
          return false;
        }

      kprintf("NK2 audio bank loaded path=%s bytes=%ld samples=%u memory=vram\n",
              NK_AUDIO_SCREAM_BANK_PATH,
              (long)audio_->scream_bank_size,
              (unsigned int)_nk_audio_read_be32(
                (const u8 *)audio_->scream_bank + 12
                ));
    }
  else
    {
      maximum_size = 0U;
      if(!_nk_audio_parse_packed_bank(
           audio_->scream_bank,
           audio_->scream_bank_size,
           audio_->scream_offsets,
           audio_->scream_sizes,
           &audio_->scream_available_mask,
           &maximum_size))
        {
          kprintf("NK2 audio cached bank invalid: %s\n",
                  NK_AUDIO_SCREAM_BANK_PATH);
          nk_audio_unload_screams(audio_);
          return false;
        }

      kprintf("NK2 audio cache hit bank=select_screams\n");
    }

  if(audio_->scream_buffer != NULL)
    {
      return true;
    }

  if(!_nk_audio_allocate_scream_buffer(audio_, maximum_size))
    {
      kprintf("NK2 audio buffer allocation failed bank=select_screams bytes=%lu\n",
              (unsigned long)maximum_size);
      nk_audio_unload_screams(audio_);
      return false;
    }

  return true;
}


Item
nk_audio_prepare_scream(NkAudio *audio_,
                        s32      character_,
                        s32      scream_index_)
{
  u32    logical_id;
  u32    size;

  if((audio_ == NULL) || (character_ < 0) ||
     (character_ >= NK_CHARACTER_COUNT) ||
     (scream_index_ < 0) ||
     (scream_index_ >= NK_AUDIO_SCREAM_VARIANT_COUNT) ||
     (audio_->scream_bank == NULL) ||
     (audio_->scream_buffer == NULL))
    {
      return -1;
    }

  logical_id = (u32)(
    character_ * NK_AUDIO_SCREAM_VARIANT_COUNT + scream_index_
    );
  if((audio_->scream_available_mask & (1UL << logical_id)) == 0U)
    {
      return -1;
    }

  size = audio_->scream_sizes[logical_id];
  if((size == 0U) || (size > (u32)audio_->scream_buffer_size))
    {
      return -1;
    }

  nk_audio_release_scream_sample(audio_);
  memcpy(
    audio_->scream_buffer,
    (const u8 *)audio_->scream_bank
    + audio_->scream_offsets[logical_id],
    size
    );
  audio_->scream_sample = _nk_audio_create_memory_sample(
    audio_->scream_buffer,
    size
    );
  if(audio_->scream_sample < 0)
    {
      kprintf("NK2 audio sample create failed bank=select_screams "
              "character=%ld (%s) sample=%ld\n",
              (long)character_,
              g_AUDIO_CHARACTER_DIRECTORIES[character_],
              (long)scream_index_);
    }
  else
    {
      kprintf("NK2 audio sample prepared bank=select_screams "
              "character=%ld (%s) sample=%ld\n",
              (long)character_,
              g_AUDIO_CHARACTER_DIRECTORIES[character_],
              (long)scream_index_);
      kprintf("NK2 audio sample prepared bytes=%lu\n",
              (unsigned long)size);
    }

  return audio_->scream_sample;
}


void
nk_audio_release_scream_sample(NkAudio *audio_)
{
  if(audio_ == NULL)
    {
      return;
    }

  if(audio_->scream_sample >= 0)
    {
      UnloadSample(audio_->scream_sample);
      audio_->scream_sample = -1;
    }
}


void
nk_audio_leave_select(NkAudio *audio_)
{
  if(audio_ == NULL)
    {
      return;
    }

  nk_audio_release_scream_sample(audio_);
  if(audio_->scream_buffer != NULL)
    {
      FreeMem(audio_->scream_buffer, audio_->scream_buffer_size);
      audio_->scream_buffer = NULL;
      audio_->scream_buffer_size = 0;
    }
}


void
nk_audio_unload_screams(NkAudio *audio_)
{
  if(audio_ == NULL)
    {
      return;
    }

  nk_audio_leave_select(audio_);
  if(audio_->scream_bank != NULL)
    {
      UnloadFile(audio_->scream_bank);
      audio_->scream_bank = NULL;
      audio_->scream_bank_size = 0;
    }

  memset(audio_->scream_offsets, 0, sizeof(audio_->scream_offsets));
  memset(audio_->scream_sizes, 0, sizeof(audio_->scream_sizes));
  audio_->scream_available_mask = 0U;
}


u32
nk_audio_loaded_sample_count(const NkAudio *audio_)
{
  u32    count;
  int bank_index;
  int sample_index;
  int select_index;

  if(audio_ == NULL)
    {
      return 0U;
    }

  count = 0U;
  for(bank_index = 0;
      bank_index < NK_AUDIO_BANK_COUNT;
      ++bank_index)
    {
      for(sample_index = 0;
          sample_index < NK_AUDIO_COMBAT_SAMPLE_COUNT;
          ++sample_index)
        {
          if(audio_->banks[bank_index].samples[sample_index] >= 0)
            {
              count++;
            }
        }
    }

  for(select_index = 0;
      select_index < NK_AUDIO_SELECT_SAMPLE_COUNT;
      ++select_index)
    {
      if(audio_->select_samples[select_index] >= 0)
        {
          count++;
        }
    }

  return count;
}


static
bool
_nk_audio_voice_is_free(const NkAudioVoice *voice_)
{
  TagArg tags[2];
  Err error;

  tags[0].ta_Tag = AF_TAG_STATUS;
  tags[0].ta_Arg = 0;
  tags[1].ta_Tag = TAG_END;
  tags[1].ta_Arg = 0;
  error = GetAudioItemInfo(voice_->player, tags);
  if(error < 0)
    {
      return false;
    }

  return (int32)tags[0].ta_Arg <= AF_STOPPED;
}


int
nk_audio_play_item(NkAudio *audio_,
                   Item     sample_)
{
  NkAudioVoice *voice;
  TagArg start_tags[2];
  Err error;
  int voice_index;

  if((audio_ == NULL) || (!audio_->initialized) || (sample_ < 0))
    {
      return -1;
    }

  for(voice_index = 0;
      voice_index < NK_AUDIO_VOICE_COUNT;
      ++voice_index)
    {
      voice = &audio_->voices[voice_index];
      if(!_nk_audio_voice_is_free(voice))
        {
          continue;
        }

      if(voice->attachment >= 0)
        {
          error = DetachSample(voice->attachment);
          if(error < 0)
            {
              kprintf("NK2 audio stale sample detach failed voice=%d error=%ld\n",
                      voice_index,
                      (long)error);
            }

          voice->attachment = -1;
        }

      voice->attachment = AttachSample(voice->player, sample_, NULL);
      if(voice->attachment < 0)
        {
          kprintf("NK2 audio sample attach failed voice=%d sample=%ld error=%ld\n",
                  voice_index,
                  (long)sample_,
                  (long)voice->attachment);
          return -1;
        }

      error = SetAudioItemInfoVA(
        voice->attachment,
        AF_TAG_SET_FLAGS,
        AF_ATTF_FATLADYSINGS,
        TAG_END
        );
      if(error < 0)
        {
          kprintf("NK2 audio attachment update failed voice=%d error=%ld\n",
                  voice_index,
                  (long)error);
          (void)DetachSample(voice->attachment);
          voice->attachment = -1;
          return -1;
        }

      start_tags[0].ta_Tag = AF_TAG_RATE;
      start_tags[0].ta_Arg = (void *)NK_AUDIO_PLAYBACK_RATE;
      start_tags[1].ta_Tag = TAG_END;
      start_tags[1].ta_Arg = 0;
      error = StartInstrument(voice->player, start_tags);
      if(error < 0)
        {
          kprintf("NK2 audio start failed voice=%d sample=%ld error=%ld\n",
                  voice_index,
                  (long)sample_,
                  (long)error);
          (void)DetachSample(voice->attachment);
          voice->attachment = -1;
        }

      return (error < 0) ? -1 : voice_index;
    }

  return -1;
}


bool
nk_audio_voice_playing(const NkAudio *audio_,
                       int            voice_index_)
{
  if((audio_ == NULL) || (!audio_->initialized) || (voice_index_ < 0) ||
     (voice_index_ >= NK_AUDIO_VOICE_COUNT))
    {
      return false;
    }

  return !_nk_audio_voice_is_free(&audio_->voices[voice_index_]);
}


void
nk_audio_release_voice(NkAudio *audio_,
                       int      voice_index_)
{
  Err error;
  NkAudioVoice *voice;

  if((audio_ == NULL) || (!audio_->initialized) || (voice_index_ < 0) ||
     (voice_index_ >= NK_AUDIO_VOICE_COUNT))
    {
      return;
    }

  voice = &audio_->voices[voice_index_];
  error = StopInstrument(voice->player, NULL);
  if(error < 0)
    {
      kprintf("NK2 audio voice release stop failed voice=%d error=%ld\n",
              voice_index_,
              (long)error);
    }

  if(voice->attachment >= 0)
    {
      error = DetachSample(voice->attachment);
      if(error < 0)
        {
          kprintf("NK2 audio voice release detach failed voice=%d error=%ld\n",
                  voice_index_,
                  (long)error);
        }

      voice->attachment = -1;
    }
}


bool
nk_audio_set_volume(NkAudio *audio_,
                    s32      level_)
{
  Err error;
  int32 gain;
  int voice_index;
  bool valid;

  if((audio_ == NULL) || (!audio_->initialized))
    {
      return false;
    }

  gain = (int32)nk_sound_gain_from_option(level_);
  valid = true;
  for(voice_index = 0;
      voice_index < NK_AUDIO_VOICE_COUNT;
      ++voice_index)
    {
      error = TweakKnob(
        audio_->voices[voice_index].left_gain,
        gain
        );
      if(error < 0)
        {
          kprintf("NK2 audio left gain update failed voice=%d error=%ld\n",
                  voice_index,
                  (long)error);
          valid = false;
        }

      error = TweakKnob(
        audio_->voices[voice_index].right_gain,
        gain
        );
      if(error < 0)
        {
          kprintf("NK2 audio right gain update failed voice=%d error=%ld\n",
                  voice_index,
                  (long)error);
          valid = false;
        }
    }

  return valid;
}


void
nk_audio_play_event(NkAudio           *audio_,
                    const NkGameEvent *event_)
{
  NkSoundRequest request;
  NkAudioBank *bank;

  if((audio_ == NULL) || (!audio_->initialized) ||
     (!nk_sound_request_from_game_event(event_, &request)) ||
     (request.operation != NK_SOUND_REQUEST_PLAY))
    {
      return;
    }

  bank = &audio_->banks[request.bank_index];
  nk_audio_play_item(audio_, bank->samples[request.sample_index]);
}


void
nk_audio_play_events(NkAudio      *audio_,
                     const NkGame *game_)
{
  u32    event_index;

  if((audio_ == NULL) || (game_ == NULL))
    {
      return;
    }

  for(event_index = 0U;
      event_index < game_->event_count;
      ++event_index)
    {
      nk_audio_play_event(audio_, &game_->events[event_index]);
    }
}
