#include <new>

#include "ym3812_render.h"
#include "ymfm_opl.h"

#define NK_YM3812_CLOCK 3579545U

struct NkYm3812 : public ymfm::ymfm_interface
{
  NkYm3812()
    : chip(*this)
  {
    chip.reset();
  }

  ymfm::ym3812 chip;
};

extern "C" NkYm3812 *
nk_ym3812_create(void)
{
  return new(std::nothrow) NkYm3812;
}


extern "C" void
nk_ym3812_destroy(NkYm3812 *chip_)
{
  delete chip_;
}


extern "C" void
nk_ym3812_reset(NkYm3812 *chip_)
{
  if(chip_ != 0)
    {
      chip_->chip.reset();
    }
}


extern "C" unsigned int
nk_ym3812_sample_rate(const NkYm3812 *chip_)
{
  if(chip_ == 0)
    {
      return 0U;
    }
  return chip_->chip.sample_rate(NK_YM3812_CLOCK);
}


extern "C" void
nk_ym3812_write_register(NkYm3812 *chip_,
                         unsigned int register_,
                         unsigned int value_)
{
  if(chip_ != 0)
    {
      chip_->chip.write_address((uint8_t)register_);
      chip_->chip.write_data((uint8_t)value_);
    }
}


extern "C" int
nk_ym3812_generate(NkYm3812 *chip_)
{
  ymfm::ym3812::output_data output;

  if(chip_ == 0)
    {
      return 0;
    }
  chip_->chip.generate(&output);
  return (int)output.data[0];
}
