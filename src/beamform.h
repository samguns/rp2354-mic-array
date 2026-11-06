#ifndef BEAMFORM_H
#define BEAMFORM_H
#include <stddef.h>
#include <stdint.h>
#define BF_TAPS 21
#define BF_HISTORY 32
#define BF_COMMON_DELAY 14
#define BF_FADE_FRAMES 960
enum beam_mode { BF_OFF, BF_BROADSIDE, BF_MANUAL };
struct beam_bank {
 int mode;unsigned azimuth_tdeg,elevation_tdeg;
 unsigned integral[4];float delay[4]; int16_t coefficient[4][BF_TAPS+1];
};
struct beamformer {
 int16_t history[4][BF_HISTORY*2];unsigned position,fade_left;
 struct beam_bank current,target;
};
int beamform_prepare(struct beam_bank *bank,int mode,unsigned azimuth_tdeg,unsigned elevation_tdeg);
void beamform_init(struct beamformer *state);
void beamform_target(struct beamformer *state,const struct beam_bank *bank);
void beamform_process(struct beamformer *state,const int16_t *input,int16_t *output,size_t frames);
/* Equal-radius planar array: broadside needs zero relative sample delays.
 * Output is duplicated into four UAC slots, with unity coherent gain. */
void beamform_broadside(const int16_t *input, int16_t *output, size_t frames);
#endif
