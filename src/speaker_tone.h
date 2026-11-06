#ifndef SPEAKER_TONE_H
#define SPEAKER_TONE_H
#include <stdint.h>

/* One-second PCM16 stimulus. The caller supplies a DDS phase and validates
 * peak <= 512. Keep scaling signed: negative samples must remain negative. */
static inline int16_t speaker_tone_sample(uint32_t phase, unsigned frame, unsigned peak)
{
	static const int16_t sine[32] = {
		0,100,196,284,362,426,473,502,512,502,473,426,362,284,196,100,
		0,-100,-196,-284,-362,-426,-473,-502,-512,-502,-473,-426,-362,-284,-196,-100,
	};
	unsigned remaining = frame < 15872 ? 15872 - frame : 0;
	unsigned gain = frame < remaining ? frame : remaining;
	if (gain > 256) { gain = 256; }
	unsigned index = phase >> 27;
	int32_t fraction = (phase >> 11) & 0xffffu;
	int32_t value = sine[index] +
		((sine[(index + 1) % 32] - sine[index]) * fraction / 65536);
	return (int16_t)(value * (int32_t)gain * (int32_t)peak / (256 * 512));
}
#endif
