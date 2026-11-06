#ifndef NOISE_REDUCE_H
#define NOISE_REDUCE_H
#include <stdint.h>
#ifdef __ZEPHYR__
#include "audio_config.h"
#define NR_SAMPLE_RATE PCM_SAMPLE_RATE
#elif !defined(NR_SAMPLE_RATE)
#define NR_SAMPLE_RATE 48000u
#endif
#define NR_HOP 128
#define NR_FFT 256
#define NR_LEARN_BLOCKS ((2u * NR_SAMPLE_RATE + NR_HOP - 1u) / NR_HOP)
struct nr_state {
 int mode, hum, channel, learn_left, learned, rate, learn_blocks;
 float hp_x, hp_y, hp_alpha, nx1, nx2, ny1, ny2, nb0, nb1, nb2, na1, na2;
 float history[128], overlap[128], window[256], re[128], im[128];
 float spectrum_re[129], spectrum_im[129];
 float noise[129], gain[129], tw_re[128], tw_im[128];
};
void nr_init(struct nr_state *s, int mode, int hum, int channel);
void nr_init_rate(struct nr_state *s, int mode, int hum, int channel, unsigned rate);
void nr_learn(struct nr_state *s);
void nr_process(struct nr_state *s, const int16_t *four, int16_t *mono);
#endif
