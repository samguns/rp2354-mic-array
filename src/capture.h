#ifndef CAPTURE_H
#define CAPTURE_H
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <stdint.h>
#include "pdm_filter.h"
#define CAPTURE_WORDS (CAPTURE_FRAMES * PDM_WORDS_PER_FRAME)
#define CAPTURE_FRAMES 128
#define CAPTURE_BUFFERS 24
#define PDM_CLOCK_HZ (PCM_SAMPLE_RATE * PDM_DECIMATION)
struct capture_block {
	uint32_t *words; /* Separate aligned 4 KiB DMA area; metadata is never in it. */
	uint32_t sequence;
 uint32_t end_us; /* DMA completion timestamp, before PCM/DSP processing. */
};
struct capture_stats {
	atomic_t blocks;
	atomic_t dropped;
	atomic_t faults;
	uint32_t sample_rate;
};
extern struct capture_stats capture_stats;
int capture_start(void);
void capture_fail(void);
int capture_read(struct capture_block **block, k_timeout_t timeout);
void capture_release(struct capture_block *block);
int capture_clock_measure(uint32_t *edges, uint32_t *elapsed_us);
int capture_phase_read(uint32_t *words, unsigned count);
int capture_clock_trace(uint32_t *words, unsigned count);
#endif
