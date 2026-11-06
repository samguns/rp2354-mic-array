#ifndef MIC_USB_AUDIO_H
#define MIC_USB_AUDIO_H
#include <stdint.h>
#include <stdbool.h>
int usb_audio_init(void);
void usb_audio_push(const int16_t *pcm, uint32_t sequence);
bool usb_audio_active(void);
#endif
