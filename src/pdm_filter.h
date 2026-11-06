#ifndef PDM_FILTER_H
#define PDM_FILTER_H
#include <stdint.h>
#include <stddef.h>
#include "audio_config.h"
#define MIC_CHANNELS 4
#define PDM_DECIMATION 64
#define PDM_CIC_DECIMATION 16
#define PDM_CIC_ORDER 4
#define PDM_HALF_TAPS 32
#define PDM_FINAL_TAPS 64
#define PDM_WORDS_PER_FRAME (PDM_DECIMATION / 8)
struct pdm_filter {
	uint8_t pdm_history[MIC_CHANNELS][16];
	unsigned pdm_pos, valid_bytes;
	int16_t half[MIC_CHANNELS][2*PDM_HALF_TAPS];
	int16_t final[MIC_CHANNELS][2*PDM_FINAL_TAPS];
	unsigned half_pos, final_pos, phase;
	uint32_t cic_cycles, fir_cycles;
	int16_t intermediate[128*4][MIC_CHANNELS];
	int16_t half_coeff[PDM_HALF_TAPS], final_coeff[PDM_FINAL_TAPS], comp_coeff[3];
	int16_t comp[MIC_CHANNELS][3];
};
void pdm_filter_init(struct pdm_filter *filter);
/* Each word contains eight chronological nibbles, earliest in bits 0..3.
 * word_count must be a multiple of PDM_WORDS_PER_FRAME. */
void pdm_filter_cic(struct pdm_filter *filter,const uint32_t *words,size_t count);
void pdm_filter_fir(struct pdm_filter *filter,const int16_t *intermediate,size_t frames,int16_t *pcm);
void pdm_filter_process_channels(struct pdm_filter *filter, const uint32_t *words,
 size_t count, int16_t *packed_pcm, unsigned first, unsigned channels);
void pdm_filter_process(struct pdm_filter *filter, const uint32_t *words,
			size_t word_count, int16_t *interleaved_pcm);
#endif
