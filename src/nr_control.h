#ifndef NR_CONTROL_H
#define NR_CONTROL_H
#include <stdint.h>
#include <stdbool.h>
bool beam_manual_active(void);
bool noise_control_pending(void);
struct output_status {
 int beam_mode,azimuth_tdeg,elevation_tdeg,pending,fade_left,nr_mode,learned,learn_left,hum,channel;
 uint32_t revision;int delay_milli[4];
};
int beam_control_set(int mode,unsigned azimuth,unsigned elevation,uint32_t *revision);
int noise_control_learn(void);
int noise_control_set(unsigned mode,unsigned hum,unsigned channel);
void audio_control_restore(void);
void noise_profile_copy(unsigned *learned,float *noise);
void output_status_get(struct output_status *status);
const int16_t *nr_apply(const int16_t *pcm);
#endif
