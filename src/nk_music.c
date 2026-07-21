/*
 * Stream mono build-time renderings of the retail MIDI/CMF music through the
 * final input of the existing Portfolio sound-effect mixer.  SoundPlayer can
 * perform synchronous disc reads while servicing its buffers, so a dedicated
 * worker owns the streaming resources and keeps that work out of the render
 * task.
 */

#include "nk_music.h"

#include "nk_sound_logic.h"

#include "audio.h"
#include "debug.h"
#include "kernel.h"
#include "musicerror.h"

#include "string.h"

#define NK_MUSIC_BUFFER_COUNT (4)
#define NK_MUSIC_BUFFER_SIZE (16384)
#define NK_MUSIC_SAMPLER ("halfmonosample.dsp")
#define NK_MUSIC_SAMPLER_PRIORITY (101)
#define NK_MUSIC_AMPLITUDE (0x7fffL)
#define NK_MUSIC_THREAD_STACK_SIZE (4096)

#define NK_MUSIC_COMMAND_NONE       (0)
#define NK_MUSIC_COMMAND_PLAY       (1)
#define NK_MUSIC_COMMAND_STOP       (2)
#define NK_MUSIC_COMMAND_VOLUME     (3)
#define NK_MUSIC_COMMAND_PAUSE      (4)
#define NK_MUSIC_COMMAND_SHUTDOWN   (5)

static const char *g_MUSIC_PATHS[NK_MUSIC_TRACK_COUNT] =
{
  "nog2/music/title-midi.aiff",
  "nog2/music/title-awe32.aiff",
  "nog2/music/match-cmf.aiff"
};

/* CreateThread() has no context argument.  The worker copies this bootstrap
 * pointer before acknowledging initialization; it is not used afterwards. */
static NkMusic *g_MUSIC_THREAD_BOOTSTRAP;


static
Err
_nk_music_send_command(NkMusic *music_,
                       int32    command_,
                       s32      argument_);
static
void
_nk_music_thread(void);
static
Err
_nk_music_thread_initialize(NkMusic *music_);
static
Err
_nk_music_thread_play(NkMusic *music_,
                      u8       track_);
static
Err
_nk_music_thread_stop(NkMusic *music_);
static
Err
_nk_music_thread_set_paused(NkMusic *music_,
                            int      paused_);
static
Err
_nk_music_apply_gain(NkMusic *music_,
                     int      audible_);
static
Err
_nk_music_sync_signals(NkMusic *music_);
static
void
_nk_music_thread_cleanup(NkMusic *music_);


static
void
_nk_music_reset(NkMusic *music_)
{
  memset(music_, 0, sizeof(*music_));
  music_->sampler = -1;
  music_->mixer = -1;
  music_->left_gain = -1;
  music_->right_gain = -1;
  music_->thread = -1;
  music_->parent_task = -1;
}


bool
nk_music_initialize(NkMusic       *music_,
                    const NkAudio *audio_,
                    s32            volume_)
{
  Err result;
  int32 received;

  if(music_ == NULL)
    {
      return false;
    }

  _nk_music_reset(music_);
  if((audio_ == NULL) || (!audio_->initialized))
    {
      return false;
    }

  if(g_MUSIC_THREAD_BOOTSTRAP != NULL)
    {
      return false;
    }

  music_->mixer = audio_->mixer;
  music_->volume = volume_;
  music_->parent_task = KernelBase->kb_CurrentTask->t.n_Item;
  music_->parent_signal = AllocSignal(0);
  if(music_->parent_signal <= 0)
    {
      kprintf("NK2 music parent signal allocation failed error=%ld\n",
              (long)music_->parent_signal);
      _nk_music_reset(music_);
      return false;
    }

  g_MUSIC_THREAD_BOOTSTRAP = music_;
  music_->thread = CreateThread(
    "NK2Music",
    KernelBase->kb_CurrentTask->t.n_Priority,
    _nk_music_thread,
    NK_MUSIC_THREAD_STACK_SIZE
    );
  if(music_->thread < 0)
    {
      kprintf("NK2 music thread create failed error=%ld\n",
              (long)music_->thread);
      g_MUSIC_THREAD_BOOTSTRAP = NULL;
      (void)FreeSignal((uint32)music_->parent_signal);
      _nk_music_reset(music_);
      return false;
    }

  received = WaitSignal((uint32)music_->parent_signal);
  if(received < 0)
    {
      result = received;
    }
  else
    {
      result = music_->command_result;
    }

  g_MUSIC_THREAD_BOOTSTRAP = NULL;
  if(result < 0)
    {
      kprintf("NK2 music thread init failed error=%ld\n", (long)result);
      (void)DeleteThread(music_->thread);
      (void)FreeSignal((uint32)music_->parent_signal);
      _nk_music_reset(music_);
      return false;
    }

  music_->initialized = 1;
  return true;
}


bool
nk_music_play(NkMusic *music_,
              u8       track_)
{
  Err result;

  if((music_ == NULL) || (!music_->initialized))
    {
      return false;
    }

  if(track_ == NK_MUSIC_TRACK_NONE)
    {
      return nk_music_stop(music_);
    }

  if(track_ > NK_MUSIC_TRACK_COUNT)
    {
      return false;
    }

  if((music_->playing) && (music_->active_track == (s32)track_))
    {
      return true;
    }

  result = _nk_music_send_command(
    music_,
    NK_MUSIC_COMMAND_PLAY,
    (s32)track_
    );
  if(result < 0)
    {
      kprintf("NK2 music play failed track=%u error=%ld\n",
              (unsigned int)track_,
              (long)result);
    }

  return result < 0 ? false : true;
}


bool
nk_music_set_volume(NkMusic *music_,
                    s32      volume_)
{
  Err result;

  if((music_ == NULL) || (!music_->initialized))
    {
      return false;
    }

  result = _nk_music_send_command(
    music_,
    NK_MUSIC_COMMAND_VOLUME,
    volume_
    );
  if(result < 0)
    {
      kprintf("NK2 music volume update failed volume=%ld error=%ld\n",
              (long)volume_,
              (long)result);
    }

  return result < 0 ? false : true;
}


bool
nk_music_set_paused(NkMusic *music_,
                    int      paused_)
{
  Err result;

  if((music_ == NULL) || (!music_->initialized))
    {
      return false;
    }

  if((!music_->playing) || (music_->paused == (paused_ != 0)))
    {
      return true;
    }

  result = _nk_music_send_command(
    music_,
    NK_MUSIC_COMMAND_PAUSE,
    paused_ != 0
    );
  if(result < 0)
    {
      kprintf("NK2 music pause update failed paused=%d error=%ld\n",
              paused_ != 0,
              (long)result);
    }

  return result < 0 ? false : true;
}


bool
nk_music_service(NkMusic *music_)
{
  if((music_ == NULL) || (!music_->initialized))
    {
      return false;
    }

  return music_->service_result < 0 ? false : true;
}


bool
nk_music_stop(NkMusic *music_)
{
  Err result;

  if((music_ == NULL) || (!music_->initialized))
    {
      return false;
    }

  if(!music_->playing)
    {
      return true;
    }

  result = _nk_music_send_command(music_, NK_MUSIC_COMMAND_STOP, 0);
  if(result < 0)
    {
      kprintf("NK2 music stop failed error=%ld\n", (long)result);
    }

  return result < 0 ? false : true;
}


void
nk_music_shutdown(NkMusic *music_)
{
  Err result;

  if(music_ == NULL)
    {
      return;
    }

  if(music_->thread >= 0)
    {
      result = _nk_music_send_command(
        music_,
        NK_MUSIC_COMMAND_SHUTDOWN,
        0
        );
      if(result < 0)
        {
          kprintf("NK2 music thread shutdown failed error=%ld\n", (long)result);
        }

      result = DeleteThread(music_->thread);
      if(result < 0)
        {
          kprintf("NK2 music thread delete failed error=%ld\n", (long)result);
        }
    }

  if(g_MUSIC_THREAD_BOOTSTRAP == music_)
    {
      g_MUSIC_THREAD_BOOTSTRAP = NULL;
    }

  if(music_->parent_signal > 0)
    {
      result = FreeSignal((uint32)music_->parent_signal);
      if(result < 0)
        {
          kprintf("NK2 music signal free failed error=%ld\n", (long)result);
        }
    }

  _nk_music_reset(music_);
}


static
Err
_nk_music_send_command(NkMusic *music_,
                       int32    command_,
                       s32      argument_)
{
  Err result;
  int32 received;

  if(KernelBase->kb_CurrentTask->t.n_Item != music_->parent_task)
    {
      return ML_ERR_BAD_ARG;
    }

  if((music_->thread < 0) || (music_->command_signal <= 0))
    {
      return ML_ERR_BAD_ARG;
    }

  music_->command_argument = argument_;
  music_->command_result = 0;
  music_->command = command_;
  result = SendSignal(music_->thread, (uint32)music_->command_signal);
  if(result < 0)
    {
      return result;
    }

  received = WaitSignal((uint32)music_->parent_signal);
  if(received < 0)
    {
      return received;
    }

  return music_->command_result;
}


static
void
_nk_music_thread(void)
{
  NkMusic *music;
  Err result;
  int32 command;
  int32 received;
  int32 status;
  uint32 wait_signals;

  music = g_MUSIC_THREAD_BOOTSTRAP;
  if(music == NULL)
    {
      (void)WaitSignal(0);
      return;
    }

  result = _nk_music_thread_initialize(music);
  if(result < 0)
    {
      _nk_music_thread_cleanup(music);
    }

  music->command_result = result;
  (void)SendSignal(music->parent_task, (uint32)music->parent_signal);
  if(result < 0)
    {
      (void)WaitSignal(0);
      return;
    }

  for(;;)
    {
      wait_signals = (uint32)music->command_signal;
      if(music->playing)
        {
          wait_signals |= (uint32)music->signals_needed;
        }

      received = WaitSignal(wait_signals);
      if(received < 0)
        {
          kprintf("NK2 music service wait failed error=%ld\n", (long)received);
          music->service_result = received;
          continue;
        }

      if((music->playing)
         && ((received & music->signals_needed) != 0))
        {
          result = spService(
            music->player,
            received & music->signals_needed
            );
          if(result < 0)
            {
              kprintf("NK2 music service failed error=%ld\n", (long)result);
              music->service_result = result;
            }
          else
            {
              status = spGetPlayerStatus(music->player);
              if((status & SP_STATUS_F_BUFFER_ACTIVE) == 0)
                {
                  kprintf("NK2 music stream ended unexpectedly\n");
                  music->service_result = ML_ERR_END_OF_FILE;
                }
            }
        }

      if((received & music->command_signal) == 0)
        {
          continue;
        }

      command = music->command;
      switch(command)
        {
        case NK_MUSIC_COMMAND_PLAY:
          result = _nk_music_thread_play(
            music,
            (u8)music->command_argument
            );
          break;
        case NK_MUSIC_COMMAND_STOP:
          result = _nk_music_thread_stop(music);
          break;
        case NK_MUSIC_COMMAND_VOLUME:
          music->volume = music->command_argument;
          result = _nk_music_apply_gain(
            music,
            music->playing && !music->paused
            );
          break;
        case NK_MUSIC_COMMAND_PAUSE:
          result = _nk_music_thread_set_paused(
            music,
            music->command_argument != 0
            );
          break;
        case NK_MUSIC_COMMAND_SHUTDOWN:
          result = _nk_music_thread_stop(music);
          _nk_music_thread_cleanup(music);
          break;
        default:
          result = ML_ERR_BAD_ARG;
          break;
        }

      music->command = NK_MUSIC_COMMAND_NONE;
      music->command_result = result;
      (void)SendSignal(music->parent_task, (uint32)music->parent_signal);
      if(command == NK_MUSIC_COMMAND_SHUTDOWN)
        {
          (void)WaitSignal(0);
          return;
        }
    }
}


static
Err
_nk_music_thread_initialize(NkMusic *music_)
{
  Err result;
  u32    index;

  result = OpenAudioFolio();
  if(result < 0)
    {
      kprintf("NK2 music audio folio open failed error=%ld\n", (long)result);
      return result;
    }

  music_->thread_folio_open = 1U;
  music_->command_signal = AllocSignal(0);
  if(music_->command_signal <= 0)
    {
      kprintf("NK2 music command signal allocation failed error=%ld\n",
              (long)music_->command_signal);
      return music_->command_signal == 0
        ? AF_ERR_NOSIGNAL
        : (Err)music_->command_signal;
    }

  music_->sampler = LoadInstrument(
    NK_MUSIC_SAMPLER,
    0,
    NK_MUSIC_SAMPLER_PRIORITY
    );
  if(music_->sampler < 0)
    {
      kprintf("NK2 music load failed instrument=%s error=%ld\n",
              NK_MUSIC_SAMPLER,
              (long)music_->sampler);
      return music_->sampler;
    }

  kprintf("NK2 music loaded instrument=%s\n", NK_MUSIC_SAMPLER);
  result = ConnectInstruments(
    music_->sampler,
    "Output",
    music_->mixer,
    "Input7"
    );
  if(result < 0)
    {
      kprintf("NK2 music mixer connect failed error=%ld\n", (long)result);
      return result;
    }

  music_->left_gain = GrabKnob(music_->mixer, "LeftGain7");
  if(music_->left_gain < 0)
    {
      kprintf("NK2 music left gain grab failed error=%ld\n",
              (long)music_->left_gain);
      return music_->left_gain;
    }

  music_->right_gain = GrabKnob(music_->mixer, "RightGain7");
  if(music_->right_gain < 0)
    {
      kprintf("NK2 music right gain grab failed error=%ld\n",
              (long)music_->right_gain);
      return music_->right_gain;
    }

  result = spCreatePlayer(
    &music_->player,
    music_->sampler,
    NK_MUSIC_BUFFER_COUNT,
    NK_MUSIC_BUFFER_SIZE,
    NULL
    );
  if(result < 0)
    {
      kprintf("NK2 music player create failed error=%ld\n", (long)result);
      return result;
    }

  for(index = 0U; index < NK_MUSIC_TRACK_COUNT; ++index)
    {
      result = spAddSoundFile(
        &music_->sounds[index],
        music_->player,
        g_MUSIC_PATHS[index]
        );
      if(result < 0)
        {
          kprintf("NK2 music load failed path=%s error=%ld\n",
                  g_MUSIC_PATHS[index],
                  (long)result);
          return result;
        }

      result = spLoopSound(music_->sounds[index]);
      if(result < 0)
        {
          kprintf("NK2 music loop setup failed path=%s error=%ld\n",
                  g_MUSIC_PATHS[index],
                  (long)result);
          return result;
        }

      kprintf("NK2 music loaded path=%s\n", g_MUSIC_PATHS[index]);
    }

  music_->signals_needed = spGetPlayerSignalMask(music_->player);
  return _nk_music_apply_gain(music_, false);
}


static
Err
_nk_music_thread_play(NkMusic *music_,
                      u8       track_)
{
  Err result;
  u32    index;

  if((track_ == NK_MUSIC_TRACK_NONE)
     || (track_ > NK_MUSIC_TRACK_COUNT))
    {
      return ML_ERR_BAD_ARG;
    }

  if((music_->playing) && (music_->active_track == (s32)track_))
    {
      return 0;
    }

  if(music_->playing)
    {
      result = _nk_music_thread_stop(music_);
      if(result < 0)
        {
          return result;
        }
    }

  result = _nk_music_sync_signals(music_);
  if(result < 0)
    {
      return result;
    }

  index = (u32)track_ - 1U;
  result = spStartReading(
    music_->sounds[index],
    SP_MARKER_NAME_BEGIN
    );
  if(result < 0)
    {
      return result;
    }

  result = spStartPlayingVA(
    music_->player,
    AF_TAG_AMPLITUDE,
    NK_MUSIC_AMPLITUDE,
    TAG_END
    );
  if(result < 0)
    {
      (void)spStop(music_->player);
      return result;
    }

  music_->active_track = track_;
  music_->playing = 1;
  music_->paused = 0;
  music_->service_result = 0;
  result = _nk_music_apply_gain(music_, true);
  if(result < 0)
    {
      music_->playing = 0;
      music_->active_track = NK_MUSIC_TRACK_NONE;
      (void)spStop(music_->player);
      return result;
    }

  kprintf("NK2 music track=%u path=%s\n",
          (unsigned int)track_,
          g_MUSIC_PATHS[index]);
  return 0;
}


static
Err
_nk_music_thread_stop(NkMusic *music_)
{
  Err result;

  if(!music_->playing)
    {
      music_->active_track = NK_MUSIC_TRACK_NONE;
      music_->paused = 0;
      return 0;
    }

  result = _nk_music_sync_signals(music_);
  if(result < 0)
    {
      return result;
    }

  result = _nk_music_apply_gain(music_, false);
  if(result < 0)
    {
      return result;
    }

  result = spStop(music_->player);
  if(result < 0)
    {
      return result;
    }

  music_->playing = 0;
  music_->paused = 0;
  music_->active_track = NK_MUSIC_TRACK_NONE;
  music_->service_result = 0;
  return 0;
}


static
Err
_nk_music_thread_set_paused(NkMusic *music_,
                            int      paused_)
{
  Err result;

  if((!music_->playing) || (music_->paused == (paused_ != 0)))
    {
      return 0;
    }

  if(paused_)
    {
      result = _nk_music_apply_gain(music_, false);
      if(result < 0)
        {
          return result;
        }

      result = spPause(music_->player);
      if(result < 0)
        {
          (void)_nk_music_apply_gain(music_, true);
          return result;
        }

      music_->paused = 1;
      return 0;
    }

  result = spResume(music_->player);
  if(result < 0)
    {
      return result;
    }

  music_->paused = 0;
  result = _nk_music_apply_gain(music_, true);
  if(result < 0)
    {
      music_->paused = 1;
      (void)spPause(music_->player);
      return result;
    }

  return 0;
}


static
Err
_nk_music_apply_gain(NkMusic *music_,
                     int      audible_)
{
  Err result;
  int32 gain;

  gain = audible_ ? (int32)nk_music_gain_from_option(music_->volume) : 0;
  result = TweakKnob(music_->left_gain, gain);
  if(result < 0)
    {
      return result;
    }

  return TweakKnob(music_->right_gain, gain);
}


static
Err
_nk_music_sync_signals(NkMusic *music_)
{
  int32 pending;
  int32 received;
  int32 status;

  if(music_->player == NULL)
    {
      return 0;
    }

  pending = GetCurrentSignals() & music_->signals_needed;
  if(pending == 0)
    {
      return 0;
    }

  received = WaitSignal((uint32)pending);
  if(received < 0)
    {
      return received;
    }

  status = spService(music_->player, received);
  return status < 0 ? (Err)status : 0;
}


static
void
_nk_music_thread_cleanup(NkMusic *music_)
{
  Err result;

  music_->playing = 0;
  music_->paused = 0;
  music_->active_track = NK_MUSIC_TRACK_NONE;
  if((music_->left_gain >= 0) && (music_->right_gain >= 0))
    {
      result = _nk_music_apply_gain(music_, false);
      if(result < 0)
        {
          kprintf("NK2 music teardown mute failed error=%ld\n", (long)result);
        }
    }

  if(music_->player != NULL)
    {
      result = _nk_music_sync_signals(music_);
      if(result < 0)
        {
          kprintf("NK2 music teardown sync failed error=%ld\n", (long)result);
        }

      result = spStop(music_->player);
      if(result < 0)
        {
          kprintf("NK2 music teardown stop failed error=%ld\n", (long)result);
        }

      spDeletePlayer(music_->player);
      music_->player = NULL;
    }

  if(music_->left_gain >= 0)
    {
      result = ReleaseKnob(music_->left_gain);
      if(result < 0)
        {
          kprintf("NK2 music teardown left gain release failed error=%ld\n",
                  (long)result);
        }

      music_->left_gain = -1;
    }

  if(music_->right_gain >= 0)
    {
      result = ReleaseKnob(music_->right_gain);
      if(result < 0)
        {
          kprintf("NK2 music teardown right gain release failed error=%ld\n",
                  (long)result);
        }

      music_->right_gain = -1;
    }

  if((music_->sampler >= 0) && (music_->mixer >= 0))
    {
      result = DisconnectInstruments(
        music_->sampler,
        "Output",
        music_->mixer,
        "Input7"
        );
      if(result < 0)
        {
          kprintf("NK2 music teardown disconnect failed error=%ld\n", (long)result);
        }
    }

  if(music_->sampler >= 0)
    {
      result = UnloadInstrument(music_->sampler);
      if(result < 0)
        {
          kprintf("NK2 music teardown unload failed error=%ld\n", (long)result);
        }

      music_->sampler = -1;
    }

  music_->signals_needed = 0;
  if(music_->command_signal > 0)
    {
      result = FreeSignal((uint32)music_->command_signal);
      if(result < 0)
        {
          kprintf("NK2 music teardown command signal free failed error=%ld\n",
                  (long)result);
        }

      music_->command_signal = 0;
    }

  if(music_->thread_folio_open)
    {
      result = CloseAudioFolio();
      if(result < 0)
        {
          kprintf("NK2 music audio folio close failed error=%ld\n", (long)result);
        }

      music_->thread_folio_open = 0U;
    }
}
