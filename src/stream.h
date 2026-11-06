#ifndef MIC_STREAM_H
#define MIC_STREAM_H
#include <stdint.h>
#include <zephyr/shell/shell.h>
void stream_push(const int16_t *pcm, uint32_t sequence);
int stream_run(const struct shell *sh, unsigned seconds);
#endif
