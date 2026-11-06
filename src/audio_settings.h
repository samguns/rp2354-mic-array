#ifndef AUDIO_SETTINGS_H
#define AUDIO_SETTINGS_H
#include <stdint.h>
#include <stdbool.h>
struct audio_config_record {uint32_t magic,version,generation,nr_mode,hum,channel,beam_mode,azimuth,elevation,learned;float noise[129];uint32_t crc;};
bool audio_settings_load(struct audio_config_record *record);
int audio_settings_save_restart(void);
bool audio_settings_match(void);
void audio_settings_changed(void);
bool audio_settings_saving(void);
int audio_settings_error(void);
#endif
