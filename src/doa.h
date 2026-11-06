#ifndef ARRAY_DOA_H
#define ARRAY_DOA_H
#include <stdint.h>
#define DOA_RATE 48000u
#define DOA_FFT 2048u
#define DOA_WINDOWS 4u
#define DOA_FRAMES (DOA_FFT * DOA_WINDOWS)
#define DOA_FIRST_BIN 18u
#define DOA_LAST_BIN 136u
#define DOA_BINS (DOA_LAST_BIN - DOA_FIRST_BIN + 1u)
enum doa_rejection {
 DOA_LOW_LEVEL=1, DOA_CLIPPED=2, DOA_POOR_BAND=4, DOA_DELAY_BOUND=8,
 DOA_BAD_FIT=16, DOA_NEAR_NORMAL=32, DOA_BAD_VECTOR=64
};
struct doa_pair { float lag, score, coherence, band_fraction; };
struct doa_result {
 struct doa_pair pair[6];
 float angle, ux, uy, rms, fit_rms, max_residual;
 float channel_rms[4];
 uint32_t rejected;
};
struct doa_workspace {
 float window[DOA_FFT], tw_re[DOA_FFT/2], tw_im[DOA_FFT/2];
 float re[DOA_FFT], im[DOA_FFT];
 float bins_re[4][DOA_BINS], bins_im[4][DOA_BINS];
 float power[4][DOA_BINS], cross_re[6][DOA_BINS], cross_im[6][DOA_BINS];
 float scan_re[2][DOA_BINS],scan_im[2][DOA_BINS];
 float step_re[DOA_BINS],step_im[DOA_BINS];
 float scores[81];
};
void doa_init(struct doa_workspace *w);
void doa_estimate(struct doa_workspace *w,const int16_t *pcm,struct doa_result *out);
void doa_begin(struct doa_workspace *w,struct doa_result *out);
void doa_add_window(struct doa_workspace *w,const int16_t *pcm,struct doa_result *out);
void doa_finish(struct doa_workspace *w,struct doa_result *out);
void doa_feed(const int16_t *pcm,uint32_t sequence,uint32_t end_us);
/* Bare core-1 service hook; native builds have no scheduling dependency. */
void dsp_parallel_doa_poll(void);
#endif
