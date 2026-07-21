#include "nk_storage.h"

#include "stdio.h"

#define NK_OPTIONS_PATH ("/NVRAM/NOG2.CFG")
#define NK_OPTIONS_SIZE (11)
#define NK_OPTIONS_READ_CAPACITY (NK_OPTIONS_SIZE + 1)

#define NK_OPTIONS_MAGIC_N_OFFSET       (0)
#define NK_OPTIONS_MAGIC_K_OFFSET       (1)
#define NK_OPTIONS_MAGIC_2_OFFSET       (2)
#define NK_OPTIONS_MAGIC_C_OFFSET       (3)
#define NK_OPTIONS_DIFFICULTY_OFFSET    (4)
#define NK_OPTIONS_SOUND_VOLUME_OFFSET  (5)
#define NK_OPTIONS_MUSIC_VOLUME_OFFSET  (6)
#define NK_OPTIONS_TALKING_OFFSET       (7)
#define NK_OPTIONS_ROUND_SETTING_OFFSET (8)
#define NK_OPTIONS_FIX_BUGS_OFFSET      (9)
#define NK_OPTIONS_CHECKSUM_OFFSET      (10)

#define NK_OPTIONS_CHECKSUM_SEED       (0x5aU)
#define NK_OPTIONS_CHECKSUM_MULTIPLIER (33U)


static
bool
_nk_storage_options_valid(const nk_options *options_)
{
  return ((options_ != NULL) &&
          (options_->difficulty >= NK_DIFFICULTY_MIN) &&
          (options_->difficulty <= NK_DIFFICULTY_MAX) &&
          (options_->sound_volume >= NK_VOLUME_MIN) &&
          (options_->sound_volume <= NK_VOLUME_MAX) &&
          (options_->music_volume >= NK_VOLUME_MIN) &&
          (options_->music_volume <= NK_VOLUME_MAX) &&
          (options_->talking >= NK_TOGGLE_OFF) &&
          (options_->talking <= NK_TOGGLE_ON) &&
          (options_->round_setting >= NK_ROUND_WIN_MIN) &&
          (options_->round_setting <= NK_ROUND_WIN_MAX) &&
          (options_->fix_orig_bugs >= NK_TOGGLE_OFF) &&
          (options_->fix_orig_bugs <= NK_TOGGLE_ON));
}


static
unsigned char
_nk_storage_checksum(const unsigned char *data_,
                     int                  size_)
{
  unsigned char checksum;
  int index;

  checksum = NK_OPTIONS_CHECKSUM_SEED;
  for(index = 0; index < (size_ - 1); ++index)
    {
      checksum = (unsigned char)(
        ((checksum * NK_OPTIONS_CHECKSUM_MULTIPLIER) + data_[index])
        );
    }

  return checksum;
}


bool
nk_storage_load_options(nk_options *options_)
{
  unsigned char data[NK_OPTIONS_READ_CAPACITY];
  nk_options loaded;
  FILE *file;
  int count;

  if(options_ == NULL)
    return false;

  file = fopen(NK_OPTIONS_PATH, "r");
  if(file == NULL)
    return false;

  count = (int)fread(data, 1, sizeof(data), file);
  fclose(file);
  if(count != NK_OPTIONS_SIZE)
    return false;

  if((data[NK_OPTIONS_MAGIC_N_OFFSET] != 'N') ||
     (data[NK_OPTIONS_MAGIC_K_OFFSET] != 'K') ||
     (data[NK_OPTIONS_MAGIC_2_OFFSET] != '2') ||
     (data[NK_OPTIONS_MAGIC_C_OFFSET] != 'C') ||
     (data[NK_OPTIONS_CHECKSUM_OFFSET] !=
      _nk_storage_checksum(data, NK_OPTIONS_SIZE)))
    return false;

  loaded.difficulty = data[NK_OPTIONS_DIFFICULTY_OFFSET];
  loaded.sound_volume = data[NK_OPTIONS_SOUND_VOLUME_OFFSET];
  loaded.music_volume = data[NK_OPTIONS_MUSIC_VOLUME_OFFSET];
  loaded.talking = data[NK_OPTIONS_TALKING_OFFSET];
  loaded.round_setting = data[NK_OPTIONS_ROUND_SETTING_OFFSET];
  loaded.fix_orig_bugs = data[NK_OPTIONS_FIX_BUGS_OFFSET];

  if(!_nk_storage_options_valid(&loaded))
    return false;

  *options_ = loaded;

  return true;
}


bool
nk_storage_save_options(const nk_options *options_)
{
  unsigned char data[NK_OPTIONS_SIZE];
  FILE *file;
  int count;

  if(!_nk_storage_options_valid(options_))
    return false;

  data[NK_OPTIONS_MAGIC_N_OFFSET] = 'N';
  data[NK_OPTIONS_MAGIC_K_OFFSET] = 'K';
  data[NK_OPTIONS_MAGIC_2_OFFSET] = '2';
  data[NK_OPTIONS_MAGIC_C_OFFSET] = 'C';
  data[NK_OPTIONS_DIFFICULTY_OFFSET] = (unsigned char)options_->difficulty;
  data[NK_OPTIONS_SOUND_VOLUME_OFFSET] = (unsigned char)options_->sound_volume;
  data[NK_OPTIONS_MUSIC_VOLUME_OFFSET] = (unsigned char)options_->music_volume;
  data[NK_OPTIONS_TALKING_OFFSET] = (unsigned char)options_->talking;
  data[NK_OPTIONS_ROUND_SETTING_OFFSET] = (unsigned char)options_->round_setting;
  data[NK_OPTIONS_FIX_BUGS_OFFSET] = (unsigned char)options_->fix_orig_bugs;
  data[NK_OPTIONS_CHECKSUM_OFFSET] = _nk_storage_checksum(data, NK_OPTIONS_SIZE);

  file = fopen(NK_OPTIONS_PATH, "w");
  if(file == NULL)
    return false;

  count = (int)fwrite(data, 1, NK_OPTIONS_SIZE, file);
  if((count != NK_OPTIONS_SIZE) || (fflush(file) < 0))
    {
      fclose(file);
      return false;
    }

  return fclose(file) == 0;
}
