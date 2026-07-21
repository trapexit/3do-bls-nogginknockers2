#pragma once

typedef struct NkYm3812 NkYm3812;

#ifdef __cplusplus
extern "C" {
#endif

NkYm3812 *
nk_ym3812_create(void);
void
nk_ym3812_destroy(NkYm3812 *chip_);
void
nk_ym3812_reset(NkYm3812 *chip_);
unsigned int
nk_ym3812_sample_rate(const NkYm3812 *chip_);
void
nk_ym3812_write_register(NkYm3812    *chip_,
                         unsigned int register_,
                         unsigned int value_);
int
nk_ym3812_generate(NkYm3812 *chip_);

#ifdef __cplusplus
}
#endif
