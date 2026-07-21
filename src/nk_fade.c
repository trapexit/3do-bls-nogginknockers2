#include "nk_fade.h"

#include "stddef.h"

#define NK_FADE_PPMPC_MF_SHIFT (10U)
#define NK_FADE_PPMPC_2S_PDC (0x000000c0U)
#define NK_FADE_PPMPC_AV_SF2_2 (0x00000010U)

#define NK_FADE_LOW_RANGE_MAX       (8U)
#define NK_FADE_HIGH_RANGE_BASE     (9U)


static
u32
_nk_fade_duplicate_half(u32    half_)
{
  return half_ | (half_ << 16);
}


void
nk_fade_configure(u8               dos_level_,
                  NkFadeCelConfig *config_)
{
  u32    half;
  u8    hardware_level;

  if(config_ == NULL)
    {
      return;
    }

  if(dos_level_ > NK_FADE_DOS_LEVEL_MAX)
    {
      dos_level_ = NK_FADE_DOS_LEVEL_MAX;
    }

  hardware_level = (u8)(((u32)dos_level_ + 1U) / 2U);

  config_->pixc = 0U;
  config_->required_ccb_flags = 0U;
  config_->hardware_level = hardware_level;
  config_->draw = hardware_level != 0U ? true : false;
  config_->reserved_zero = 0U;
  config_->reserved_one = 0U;
  if(hardware_level == 0U)
    {
      return;
    }

  if(hardware_level <= NK_FADE_LOW_RANGE_MAX)
    {
      /*
       * PDC * MF / 16 + 0.  MF is encoded as multiplier minus one.
       * Transparent source pixels remain skipped by the CEL decoder.
       */
      half = ((u32)hardware_level - 1U)
             << NK_FADE_PPMPC_MF_SHIFT;
      config_->pixc = _nk_fade_duplicate_half(half);
      return;
    }

  config_->required_ccb_flags = NK_FADE_CCB_USEAV;
  if(hardware_level == NK_FADE_HARDWARE_LEVEL_MAX)
    {
      config_->pixc = NK_FADE_PIXC_OPAQUE;
      return;
    }

  /*
   * Portfolio 2.5 CrossFadeCels high-intensity formula:
   * PDC * (level - 8) / 16 + PDC / 2.
   */
  half = (((u32)hardware_level - NK_FADE_HIGH_RANGE_BASE)
          << NK_FADE_PPMPC_MF_SHIFT)
         | NK_FADE_PPMPC_2S_PDC
         | NK_FADE_PPMPC_AV_SF2_2;
  config_->pixc = _nk_fade_duplicate_half(half);
}
