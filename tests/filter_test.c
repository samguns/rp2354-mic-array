#include "pdm_filter.h"
#include "pdm_coefficients.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <math.h>
#include <stdlib.h>

static uint32_t random_state = 0x12345678;
static uint32_t random_word(void)
{
	random_state ^= random_state << 13;
	random_state ^= random_state >> 17;
	random_state ^= random_state << 5;
	return random_state;
}

struct reference_filter {
 uint32_t integrator[4][4],comb[4][4];
 int16_t half[4][PDM_HALF_TAPS],final[4][PDM_FINAL_TAPS],comp[4][3];
 unsigned phase;
};
static void reference(struct reference_filter *f, const uint32_t *words, size_t count, int16_t *pcm)
{
 for(size_t n=0;n<count;n++){
  for(unsigned bit=0;bit<8;bit++)for(unsigned c=0;c<4;c++){
   uint32_t *i=f->integrator[c];i[0]+=(words[n]&(1u<<(bit*4+c)))?1u:UINT32_MAX;
   for(unsigned k=1;k<4;k++)i[k]+=i[k-1];
  }
  if(!(n&1u))continue;
  for(unsigned c=0;c<4;c++){
   uint32_t v=f->integrator[c][3];
   for(unsigned k=0;k<4;k++){uint32_t old=v;v-=f->comb[c][k];f->comb[c][k]=old;}
   int64_t sv=v;if(v&0x80000000u)sv-=0x100000000LL;
   int64_t scaled=(sv>=0?sv+1:sv-1)/2;
   if(scaled>32767)scaled=32767;
   if(scaled< -32768)scaled=-32768;
   memmove(f->half[c],f->half[c]+1,(PDM_HALF_TAPS-1)*sizeof(int16_t));
   f->half[c][PDM_HALF_TAPS-1]=scaled;
   if(!(f->phase&1u))continue;
   int64_t x=0;for(unsigned t=0;t<PDM_HALF_TAPS;t++)x+=(int32_t)pdm_halfband[t]*f->half[c][t];
   x=(x>=0?x+8192:x-8192)/16384;
   if(x>32767)x=32767;
   if(x< -32768)x=-32768;
   memmove(f->final[c],f->final[c]+1,(PDM_FINAL_TAPS-1)*sizeof(int16_t));
   f->final[c][PDM_FINAL_TAPS-1]=x;
   if((f->phase&3u)!=3u)continue;
   x=0;for(unsigned t=0;t<PDM_FINAL_TAPS;t++)x+=(int32_t)pdm_final[t]*f->final[c][t];
   x=(x>=0?x+8192:x-8192)/16384;
   if(x>32767)x=32767;
   if(x< -32768)x=-32768;
   int16_t *h=f->comp[c];h[0]=h[1];h[1]=h[2];h[2]=x;
   x=0;for(unsigned t=0;t<3;t++)x+=(int32_t)pdm_comp[t]*h[t];
   x=(x>=0?x+8192:x-8192)/16384;
   if(x>32767)x=32767;
   if(x< -32768)x=-32768;
   *pcm++=(int16_t)x;
  }
  f->phase=(f->phase+1)&3u;
 }
}

int main(void)
{
	struct pdm_filter fast;
 struct reference_filter slow = {0};
	pdm_filter_init(&fast);
	uint32_t words[PDM_WORDS_PER_FRAME * 16];
	int16_t actual[64], expected[64];
	/* Long random stream exercises lane packing, block continuity, and
	 * repeated integrator overflow against an independent bitwise model. */
	for (unsigned block = 0; block < 1000; block++) {
		for (unsigned n = 0; n < sizeof(words)/sizeof(words[0]); n++) { words[n] = random_word(); }
		pdm_filter_process(&fast, words, sizeof(words)/sizeof(words[0]), actual);
		reference(&slow, words, sizeof(words)/sizeof(words[0]), expected);
		for(unsigned k=0;k<64;k++)assert(actual[k]==expected[k]);

	}
	pdm_filter_init(&fast);
	/* c0 all ones; c1 all zero; c2 alternating; c3 alternating inverse. */
	for (unsigned n = 0; n < sizeof(words)/sizeof(words[0]); n++) { words[n] = 0x95959595u; }
	for(unsigned b=0;b<8;b++)pdm_filter_process(&fast, words, sizeof(words)/sizeof(words[0]), actual);
	assert(actual[60] == 32767);
	assert(actual[61] == -32768);
	assert(actual[62] == 0 && actual[63] == 0);
	puts("PASS: CIC4 plus FIR matches bitwise/direct-convolution reference through wraparound; channel isolation and DC gain");
	/* An independently generated first-order sigma-delta sine checks that
	 * the complete packing/filtering path preserves frequency and amplitude. */
	pdm_filter_init(&fast);
	double error = 0, re = 0, im = 0, third_re = 0, third_im = 0;
	unsigned tick = 0, frames = 0, measured = 0;
	const double pi = 3.14159265358979323846;
	for (unsigned block = 0; block < 1000; block++) {
		for (unsigned n = 0; n < sizeof(words)/sizeof(words[0]); n++) {
			words[n] = 0;
			for (unsigned bit = 0; bit < 8; bit++, tick++) {
				double input = 0.02 * sin(2 * pi * 500 * tick / ((double)PCM_SAMPLE_RATE * PDM_DECIMATION));
				/* Dither avoids idle tones in this simple first-order test
				 * modulator; the real microphone uses its own modulator. */
				input += 0.001 * ((random_word() & 65535u) / 32768.0 - 1.0);
				error += input;
				unsigned positive = error >= 0;
				error -= positive ? 1.0 : -1.0;
				unsigned nibble = positive | ((!positive) << 1) |
					((tick & 1) << 2) | (positive << 3);
				words[n] |= nibble << (bit * 4);
			}
		}
		pdm_filter_process(&fast, words, sizeof(words)/sizeof(words[0]), actual);
		for (unsigned f = 0; f < 16; f++, frames++) {
			if (frames < 128) { continue; }
			double phase = 2 * pi * 500 * frames / PCM_SAMPLE_RATE;
			re += actual[f * 4] * cos(phase);
			im += actual[f * 4] * sin(phase);
			third_re += actual[f * 4] * cos(3 * phase);
			third_im += actual[f * 4] * sin(3 * phase);
			assert(actual[f * 4 + 2] == 0);
			assert(actual[f * 4] == actual[f * 4 + 3]);
			measured++;
		}
	}
	double fundamental = 2 * hypot(re, im) / measured;
	double third = 2 * hypot(third_re, third_im) / measured;
	printf("Synthetic PDM sine: 500 Hz amplitude=%.3f, 1500 Hz amplitude=%.3f\n", fundamental, third);
	assert(fundamental > 640 && fundamental < 660);
	assert(third < 5);
	puts("PASS: synthetic sine retains its fundamental without a large third harmonic");
	return 0;
}
