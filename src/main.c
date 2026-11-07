#include "capture.h"
#include "board_pins.h"
#include "pdm_filter.h"
#include "dsp_parallel.h"
#include "speaker_tone.h"
#include "stream.h"
#include "usb_audio.h"
#include "nr_control.h"
#include "doa.h"
#include <zephyr/shell/shell.h>
#include <pico/bootrom.h>
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <hardware/clocks.h>
#include <hardware/timer.h>
#include <hardware/dma.h>
#include <hardware/pio_instructions.h>
#include <stdlib.h>
#include <string.h>

#define MAX_RECORD_FRAMES 16000
static struct pdm_filter filter;
static int16_t pcm[CAPTURE_FRAMES * MIC_CHANNELS];

#define RAW_BLOCKS 4
static union {
 int16_t recording[MAX_RECORD_FRAMES * MIC_CHANNELS];
 struct { uint32_t words[RAW_BLOCKS * CAPTURE_WORDS];
          int16_t pcm[RAW_BLOCKS * CAPTURE_FRAMES * MIC_CHANNELS]; } raw;
} diagnostic;
#define recording diagnostic.recording
#define raw_snapshot diagnostic.raw.words
#define raw_pcm diagnostic.raw.pcm
static atomic_t raw_request;
static unsigned raw_blocks;
static struct k_spinlock state_lock;
static struct {
	uint32_t target;
	uint32_t frames;
	uint32_t peak[MIC_CHANNELS];
	uint32_t max_process_us;
	uint32_t filter_blocks; uint64_t filter_total_us;
	bool ready;
	bool invalid;
} state;

static int cmd_boot(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	shell_print(sh, "Rebooting into USB BOOTSEL mode...");
	/* Allow the USB shell message to drain before disconnecting. */
	k_msleep(100);
	/* No activity GPIO; enable both mass-storage and picoboot interfaces. */
	reset_usb_boot(0, 0);
}

SHELL_CMD_ARG_REGISTER(boot, NULL, "Reboot into USB BOOTSEL mode for UF2 flashing",
		       cmd_boot, 1, 0);

static uint32_t i2s_probe[2048];
#if AUDIO_SPEAKER_DIAGNOSTICS
static uint32_t speaker_samples[32000];

/* Sample the actual output GPIOs internally, without adding jumper wires.
 * Each word packs eight chronological {LRCLK,BCLK,DIN} triples, MSB first. */
static int probe_init(PIO pio, size_t *probe_sm)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(mic_pio));
	int ret = pio_rpi_pico_allocate_sm(dev, probe_sm);
	if (ret) { return ret; }
	uint16_t instructions[] = {
		pio_encode_wait_gpio(false, SPEAKER_BCLK_PIN),
		pio_encode_wait_gpio(true, SPEAKER_BCLK_PIN),
		pio_encode_in(pio_pins, 3),
	};
	struct pio_program program = {.instructions = instructions, .length = 3, .origin = -1};
	if (!pio_can_add_program(pio, &program)) {
		pio_sm_unclaim(pio, *probe_sm);
		return -ENOMEM;
	}
	unsigned offset = pio_add_program(pio, &program);
	pio_sm_config config = pio_get_default_sm_config();
	sm_config_set_wrap(&config, offset, offset + 2);
	sm_config_set_in_pins(&config, SPEAKER_DATA_PIN);
	sm_config_set_in_shift(&config, false, true, 24);
	sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
	pio_sm_init(pio, *probe_sm, offset, &config);
	return 0;
}

static int cmd_speaker_test(const struct shell *sh, size_t argc, char **argv)
{
	bool verify = strcmp(argv[0], "verify") == 0;
	unsigned frequency = 500;
	unsigned peak = 512;
	bool left_justified = false;
	if (argc > 1) {
		char *end;
		frequency = strtoul(argv[1], &end, 10);
		if (*end || frequency < 200 || frequency > 2000) { return -EINVAL; }
	}
	if (argc > 2) {
		char *end;
		peak = strtoul(argv[2], &end, 10);
		if (*end || peak > 512) { return -EINVAL; }
	}
	if (argc > 3) {
		if (strcmp(argv[3], "left") == 0) { left_justified = true; }
		else if (strcmp(argv[3], "i2s") != 0) { return -EINVAL; }
	}
	static bool initialized;
	static size_t speaker_sm;
	static unsigned format_offset[2];
	static bool probe_initialized;
	static size_t probe_sm;
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(mic_pio));
	if (!device_is_ready(dev) || !atomic_get(&capture_stats.blocks)) {
		shell_error(sh, "Audio hardware not initialized");
		return -ENODEV;
	}
	PIO pio = pio_rpi_pico_get_pio(dev);
	if (!initialized) {
		int ret = pio_rpi_pico_allocate_sm(dev, &speaker_sm);
		if (ret) { return ret; }
		/* 32-bit stereo I2S. LR changes on the previous channel's LSB,
		 * one BCLK before the next MSB. Side-set bit0=BCLK, bit1=LRCLK. */
		uint16_t instructions[] = {
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0),
			pio_encode_jmp_x_dec(0) | pio_encode_sideset(2, 1),
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2),
			pio_encode_set(pio_x, 30) | pio_encode_sideset(2, 3),
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2),
			pio_encode_jmp_x_dec(4) | pio_encode_sideset(2, 3),
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0),
			pio_encode_set(pio_x, 30) | pio_encode_sideset(2, 1),
		};
		struct pio_program program = {
			.instructions = instructions, .length = ARRAY_SIZE(instructions), .origin = -1,
		};
		if (!pio_can_add_program(pio, &program)) {
			pio_sm_unclaim(pio, speaker_sm);
			return -ENOMEM;
		}
		unsigned offset = pio_add_program(pio, &program);
		format_offset[0] = offset;
		/* Diagnostic alternate format: align LRCLK with the MSB, as used
		 * by MAX98357B. Keep separate programs so switching is explicit. */
		uint16_t left_instructions[] = {
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0),
			pio_encode_set(pio_x, 30) | pio_encode_sideset(2, 1),
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 0),
			pio_encode_jmp_x_dec(2) | pio_encode_sideset(2, 1),
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2),
			pio_encode_set(pio_x, 30) | pio_encode_sideset(2, 3),
			pio_encode_out(pio_pins, 1) | pio_encode_sideset(2, 2),
			pio_encode_jmp_x_dec(6) | pio_encode_sideset(2, 3),
		};
		struct pio_program left_program = {
			.instructions = left_instructions, .length = 8, .origin = -1,
		};
		if (!pio_can_add_program(pio, &left_program)) {
			pio_remove_program(pio, &program, offset);
			pio_sm_unclaim(pio, speaker_sm);
			return -ENOMEM;
		}
		format_offset[1] = pio_add_program(pio, &left_program);
		pio_sm_config config = pio_get_default_sm_config();
		sm_config_set_wrap(&config, offset, offset + 7);
		sm_config_set_out_pins(&config, SPEAKER_DATA_PIN, 1);
		sm_config_set_sideset(&config, 2, false, false);
		sm_config_set_sideset_pins(&config, SPEAKER_BCLK_PIN);
		sm_config_set_out_shift(&config, false, true, 32);
		sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_TX);
		sm_config_set_clkdiv(&config, (float)clock_get_hz(clk_sys) / (16000u * 128u));
		for (unsigned pin = SPEAKER_DATA_PIN; pin <= SPEAKER_LRC_PIN; pin++) { pio_gpio_init(pio, pin); }
		pio_sm_init(pio, speaker_sm, offset, &config);
		pio_sm_set_consecutive_pindirs(pio, speaker_sm, SPEAKER_DATA_PIN, 3, true);
		initialized = true;
	}
	int channel = dma_claim_unused_channel(false);
	if (channel < 0) { return -EBUSY; }
	int probe_channel = -1;
	if (verify) {
		if (!probe_initialized) {
			int ret = probe_init(pio, &probe_sm);
			if (ret) { dma_channel_unclaim(channel); return ret; }
			probe_initialized = true;
		}
		probe_channel = dma_claim_unused_channel(false);
		if (probe_channel < 0) { dma_channel_unclaim(channel); return -EBUSY; }
		pio_sm_clear_fifos(pio, probe_sm);
		pio_sm_restart(pio, probe_sm);
		unsigned bottom = (pio->sm[probe_sm].execctrl & PIO_SM0_EXECCTRL_WRAP_BOTTOM_BITS) >>
				  PIO_SM0_EXECCTRL_WRAP_BOTTOM_LSB;
		pio_sm_exec(pio, probe_sm, pio_encode_jmp(bottom));
		dma_channel_config probe_config = dma_channel_get_default_config(probe_channel);
		channel_config_set_transfer_data_size(&probe_config, DMA_SIZE_32);
		channel_config_set_read_increment(&probe_config, false);
		channel_config_set_write_increment(&probe_config, true);
		channel_config_set_dreq(&probe_config, pio_get_dreq(pio, probe_sm, false));
		dma_channel_configure(probe_channel, &probe_config, i2s_probe,
				      &pio->rxf[probe_sm], ARRAY_SIZE(i2s_probe), true);
		pio_sm_set_enabled(pio, probe_sm, true);
	}
	/* Identical L/R preserves the module's mono mix. Fade both ends and
	 * finish with 8 ms of silence; default peak 512 is about -36 dBFS. */
	uint32_t phase = 0;
	uint32_t phase_step = ((uint64_t)frequency << 32) / 16000u;
	for (unsigned n = 0; n < 16000; n++, phase += phase_step) {
		uint16_t sample = (uint16_t)speaker_tone_sample(phase, n, peak);
		speaker_samples[2 * n] = (uint32_t)sample << 16;
		speaker_samples[2 * n + 1] = (uint32_t)sample << 16;
	}
	pio_sm_set_enabled(pio, speaker_sm, false);
	pio_sm_clear_fifos(pio, speaker_sm);
	pio_sm_restart(pio, speaker_sm);
	unsigned wrap = format_offset[left_justified];
	pio_sm_set_wrap(pio, speaker_sm, wrap, wrap + 7);
	pio_sm_exec(pio, speaker_sm, pio_encode_jmp(wrap));
	pio_sm_exec(pio, speaker_sm, pio_encode_set(pio_x, 30));
	pio_sm_set_pins_with_mask(pio, speaker_sm, 0, BIT(SPEAKER_DATA_PIN) | BIT(SPEAKER_BCLK_PIN) | BIT(SPEAKER_LRC_PIN));
	dma_channel_config dma_config = dma_channel_get_default_config(channel);
	channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
	channel_config_set_read_increment(&dma_config, true);
	channel_config_set_write_increment(&dma_config, false);
	channel_config_set_dreq(&dma_config, pio_get_dreq(pio, speaker_sm, true));
	shell_print(sh, "Playing %u Hz for 1 second; PCM16 peak=%u; format=%s...",
		    frequency, peak, left_justified ? "left" : "i2s");
	dma_channel_configure(channel, &dma_config, &pio->txf[speaker_sm],
			      speaker_samples, ARRAY_SIZE(speaker_samples), true);
	pio_sm_set_enabled(pio, speaker_sm, true);
	int64_t deadline = k_uptime_get() + 1500;
	while (dma_channel_is_busy(channel) && k_uptime_get() < deadline) { k_msleep(5); }
	bool timed_out = dma_channel_is_busy(channel);
	k_msleep(2); /* Drain the FIFO and final shift register. */
	pio_sm_set_enabled(pio, speaker_sm, false);
	dma_channel_cleanup(channel);
	dma_channel_unclaim(channel);
	pio_sm_set_pins_with_mask(pio, speaker_sm, 0, BIT(SPEAKER_DATA_PIN) | BIT(SPEAKER_BCLK_PIN) | BIT(SPEAKER_LRC_PIN));
	if (verify) {
		bool complete = !dma_channel_is_busy(probe_channel);
		pio_sm_set_enabled(pio, probe_sm, false);
		dma_channel_cleanup(probe_channel);
		dma_channel_unclaim(probe_channel);
		if (!complete) { shell_error(sh, "I2S probe did not complete"); return -EIO; }
		shell_print(sh, "I2S_BEGIN %u", (unsigned)ARRAY_SIZE(i2s_probe));
		for (unsigned i = 0; i < ARRAY_SIZE(i2s_probe); i += 8) {
			shell_print(sh, "%08x%08x%08x%08x%08x%08x%08x%08x",
				i2s_probe[i], i2s_probe[i+1], i2s_probe[i+2], i2s_probe[i+3],
				i2s_probe[i+4], i2s_probe[i+5], i2s_probe[i+6], i2s_probe[i+7]);
		}
		shell_print(sh, "I2S_END");
	}
	if (timed_out) {
		shell_error(sh, "Speaker DMA timed out");
		return -ETIMEDOUT;
	}
	shell_print(sh, "Speaker test complete");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(speaker_commands,
	SHELL_CMD_ARG(test, NULL, "Play tone [Hz=500] [peak=512; 0..512] [i2s|left]", cmd_speaker_test, 1, 3),
	SHELL_CMD_ARG(verify, NULL, "Play tone and export GPIO samples [Hz] [peak] [i2s|left]", cmd_speaker_test, 1, 3),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(speaker, &speaker_commands, "MAX98357A speaker", NULL);
#endif

static int cmd_stats(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	uint32_t peaks[MIC_CHANNELS];
	memcpy(peaks, state.peak, sizeof(peaks));
	uint32_t max_us = state.max_process_us;
	uint32_t fb = state.filter_blocks; uint64_t ft = state.filter_total_us;
	bool ready = state.ready;
	k_spin_unlock(&state_lock, key);
	shell_print(sh, "rate=%u channels=4 ready=%u blocks=%ld dropped=%ld faults=%ld max_process_us=%u",
		capture_stats.sample_rate, ready, (long)atomic_get(&capture_stats.blocks),
		(long)atomic_get(&capture_stats.dropped), (long)atomic_get(&capture_stats.faults), max_us);
	shell_print(sh, "PDM_PROFILE blocks=%u mean_us=%u", fb, fb ? (unsigned)(ft/fb) : 0);
	uint32_t local_us,remote_us,worker_status;dsp_parallel_profile(&local_us,&remote_us,&worker_status);
	shell_print(sh,"DSP_PIPELINE cores=2 fir_core0_us=%u cic_core1_us=%u worker_status=%u",local_us,remote_us,worker_status);
	shell_print(sh, "peak_pcm16=%u,%u,%u,%u", peaks[0], peaks[1], peaks[2], peaks[3]);
	return 0;
}

static int cmd_record(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	char *end;
	unsigned long ms = strtoul(argv[1], &end, 10);
	if (*end || ms < 1 || ms > MAX_RECORD_FRAMES * 1000u / PCM_SAMPLE_RATE) {
		shell_error(sh, "Duration must be 1..%u ms (RAM diagnostic limit)", MAX_RECORD_FRAMES * 1000u / PCM_SAMPLE_RATE);
		return -EINVAL;
	}
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	if (!state.ready || atomic_get(&capture_stats.faults) ||
	    (!state.invalid && state.frames < state.target)) {
		k_spin_unlock(&state_lock, key);
		shell_error(sh, "Capture warming up, faulted, or recording busy");
		return -EBUSY;
	}
	state.frames = 0;
	state.target = MIN((uint64_t)ms * capture_stats.sample_rate / 1000u, MAX_RECORD_FRAMES);
	state.invalid = false;
	k_spin_unlock(&state_lock, key);
	shell_print(sh, "RECORDING");
	return 0;
}

static int cmd_status(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	uint32_t frames = state.frames, target = state.target;
	bool invalid = state.invalid;
	k_spin_unlock(&state_lock, key);
	shell_print(sh, "RECORD %s %u %u", invalid ? "INVALID" :
		(target == 0 ? "EMPTY" : (frames == target ? "DONE" : "BUSY")), frames, target);
	return 0;
}

static int cmd_dump(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	uint32_t frames = state.frames;
	bool valid = state.target && frames == state.target && !state.invalid;
	k_spin_unlock(&state_lock, key);
	if (!valid) {
		shell_error(sh, "No complete valid recording; use mic record <ms> then mic status");
		return -EAGAIN;
	}
	/* Shell commands are serialized. A new record command cannot overwrite
	 * this completed buffer while dump is running. Capture keeps running. */
	static const char hex[] = "0123456789abcdef";
	char line[257];
	const uint8_t *bytes = (const uint8_t *)recording;
	size_t length = frames * MIC_CHANNELS * sizeof(int16_t);
	shell_print(sh, "PCM_BEGIN %u 4 %u", capture_stats.sample_rate, frames);
	for (size_t n = 0; n < length; n += 128) {
		size_t chunk = MIN(length - n, 128);
		for (size_t j = 0; j < chunk; j++) {
			line[2 * j] = hex[bytes[n + j] >> 4];
			line[2 * j + 1] = hex[bytes[n + j] & 15];
		}
		line[2 * chunk] = 0;
		shell_print(sh, "%s", line);
	}
	shell_print(sh, "PCM_END");
	return 0;
}

static int cmd_stream(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	char *end;
	unsigned long seconds = strtoul(argv[1], &end, 10);
	if (*end || seconds < 1 || seconds > 3600) { return -EINVAL; }
	k_spinlock_key_t key = k_spin_lock(&state_lock);
	bool ready = state.ready;
	k_spin_unlock(&state_lock, key);
	if (!ready || atomic_get(&capture_stats.faults)) { return -EAGAIN; }
	return stream_run(sh, seconds);
}

static int cmd_raw(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	k_spinlock_key_t state_key = k_spin_lock(&state_lock);
	if (state.frames < state.target && !state.invalid) {
		k_spin_unlock(&state_lock, state_key); return -EBUSY;
	}
	state.target = 0; state.frames = 0;
	k_spin_unlock(&state_lock, state_key);
	if (atomic_get(&capture_stats.faults) || !atomic_get(&capture_stats.blocks)) {
		return -EIO;
	}
	uint32_t edges, elapsed_us;
	int ret = capture_clock_measure(&edges, &elapsed_us);
	if (ret) { return ret; }
	shell_print(sh, "PDM_CLOCK edges=%u elapsed_us=%u", edges, elapsed_us);
	raw_blocks = 0;
	atomic_set(&raw_request, 1);
	int64_t deadline = k_uptime_get() + 500;
	while (atomic_get(&raw_request) == 1 && k_uptime_get() < deadline) { k_msleep(5); }
	if (atomic_get(&raw_request) != 0) {
		atomic_set(&raw_request, 0);
		shell_error(sh, "Raw capture incomplete or discontinuous");
		return -EIO;
	}
	shell_print(sh, "RAW_BEGIN %u %u %u", PDM_CLOCK_HZ, PDM_DECIMATION,
		    (unsigned)ARRAY_SIZE(raw_snapshot));
	for (unsigned i = 0; i < ARRAY_SIZE(raw_snapshot); i += 8) {
		shell_print(sh, "%08x%08x%08x%08x%08x%08x%08x%08x",
			raw_snapshot[i], raw_snapshot[i+1], raw_snapshot[i+2], raw_snapshot[i+3],
			raw_snapshot[i+4], raw_snapshot[i+5], raw_snapshot[i+6], raw_snapshot[i+7]);
	}
	shell_print(sh, "RAW_END");
	shell_print(sh, "RAW_PCM_BEGIN %u", (unsigned)ARRAY_SIZE(raw_pcm));
	for (unsigned i = 0; i < ARRAY_SIZE(raw_pcm); i += 8) {
		shell_print(sh, "%04x%04x%04x%04x%04x%04x%04x%04x",
			(uint16_t)raw_pcm[i], (uint16_t)raw_pcm[i+1], (uint16_t)raw_pcm[i+2], (uint16_t)raw_pcm[i+3],
			(uint16_t)raw_pcm[i+4], (uint16_t)raw_pcm[i+5], (uint16_t)raw_pcm[i+6], (uint16_t)raw_pcm[i+7]);
	}
	shell_print(sh, "RAW_PCM_END");
	ret = capture_phase_read(i2s_probe, ARRAY_SIZE(i2s_probe));
	if (ret) { return ret; }
	shell_print(sh, "PHASE_BEGIN %u", (unsigned)ARRAY_SIZE(i2s_probe));
	for (unsigned i = 0; i < ARRAY_SIZE(i2s_probe); i += 8) {
		shell_print(sh, "%08x%08x%08x%08x%08x%08x%08x%08x",
			i2s_probe[i], i2s_probe[i+1], i2s_probe[i+2], i2s_probe[i+3],
			i2s_probe[i+4], i2s_probe[i+5], i2s_probe[i+6], i2s_probe[i+7]);
	}
	shell_print(sh, "PHASE_END");
	ret = capture_clock_trace(i2s_probe, ARRAY_SIZE(i2s_probe));
	if (ret) { return ret; }
	shell_print(sh, "CLOCK_TRACE_BEGIN %u %u", (unsigned)ARRAY_SIZE(i2s_probe), clock_get_hz(clk_sys));
	for (unsigned i = 0; i < ARRAY_SIZE(i2s_probe); i += 8) {
		shell_print(sh, "%08x%08x%08x%08x%08x%08x%08x%08x",
			i2s_probe[i], i2s_probe[i+1], i2s_probe[i+2], i2s_probe[i+3],
			i2s_probe[i+4], i2s_probe[i+5], i2s_probe[i+6], i2s_probe[i+7]);
	}
	shell_print(sh, "CLOCK_TRACE_END");
	return 0;
}

SHELL_STATIC_SUBCMD_SET_CREATE(mic_commands,
	SHELL_CMD_ARG(stream, NULL, "Stream binary PCM for 1..3600 seconds (host tool)", cmd_stream, 2, 0),
	SHELL_CMD(stats, NULL, "Capture health and recent per-channel peaks", cmd_stats),
	SHELL_CMD_ARG(record, NULL, "Record 1..333 ms of four-channel PCM (RAM limit)", cmd_record, 2, 0),
	SHELL_CMD(status, NULL, "Recording status", cmd_status),
	SHELL_CMD(dump, NULL, "Export completed recording as framed PCM16 hex", cmd_dump),
	SHELL_CMD(raw, NULL, "Measure GPIO clock and export raw PDM plus matching PCM", cmd_raw),
	SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(mic, &mic_commands, "RP2354A microphone array", NULL);

__ramfunc __attribute__((optimize("O3"))) int main(void)
{
	/* Capture outranks shell work. Any continuity failure stops acquisition,
     * allowing shell recovery instead of an overloaded conversion loop. */
	k_thread_priority_set(k_current_get(), 2);
	audio_control_restore();
	pdm_filter_init(&filter);
	int ret = usb_audio_init();
	if (ret) { return ret; }
	ret = dsp_parallel_init();
	if(ret){printk("DSP core launch failed: %d\n",ret);return ret;}
	ret = capture_start();
	if (ret) {
		atomic_inc(&capture_stats.faults);
		printk("Capture initialization failed: %d; reboot required\n", ret);
		return ret;
	}
	uint32_t expected = 0;
	uint32_t warmup = 48; /* >100 ms, including microphone and CIC settling. */
	while (true) {
		struct capture_block *block;
		if (capture_read(&block, K_MSEC(100)) != 0) {
			if (atomic_get(&capture_stats.faults)) {
				k_spinlock_key_t key = k_spin_lock(&state_lock);
				state.ready = false;
				if (state.frames < state.target) { state.invalid = true; }
				k_spin_unlock(&state_lock, key);
			}
			continue;
		}
		uint32_t start = k_cycle_get_32();
		if (atomic_get(&capture_stats.faults)) {
			capture_release(block);
			k_spinlock_key_t key = k_spin_lock(&state_lock);
			state.ready = false;
			if (state.frames < state.target) { state.invalid = true; }
			k_spin_unlock(&state_lock, key);
			continue;
		}
		if (block->sequence != expected) {
            /* Lost input is fatal: never conceal it with repeated warmup silence. */
            if(atomic_get(&raw_request)==1)atomic_set(&raw_request,-1);
            capture_fail();continue;
        }
        expected=block->sequence+1;
		uint32_t filter_start = k_cycle_get_32();
		/* On timeout retain this DMA block: core 1 may still be reading it. */
		struct capture_block *completed;
        if(dsp_parallel_process(&filter,block,pcm,&completed)){capture_fail();continue;}
        if(!completed)continue;
        block=completed;
		uint32_t filter_us = k_cyc_to_us_floor32(k_cycle_get_32()-filter_start);
		if (!warmup) {
			stream_push(pcm, block->sequence);
            doa_feed(pcm,block->sequence,block->end_us);
			usb_audio_push(nr_apply(pcm), block->sequence);
		}
		if (atomic_get(&raw_request) == 1) {
			memcpy(raw_snapshot + raw_blocks * CAPTURE_WORDS, block->words,
			       CAPTURE_WORDS * sizeof(uint32_t));
			memcpy(raw_pcm + raw_blocks * CAPTURE_FRAMES * MIC_CHANNELS, pcm, sizeof(pcm));
			if (++raw_blocks == RAW_BLOCKS) { atomic_set(&raw_request, 0); }
		}
		capture_release(block);
		uint32_t peaks[MIC_CHANNELS] = {0};
		for (unsigned f = 0; f < CAPTURE_FRAMES; f++) {
			for (unsigned c = 0; c < MIC_CHANNELS; c++) {
				int32_t sample = pcm[f * MIC_CHANNELS + c];
				peaks[c] = MAX(peaks[c], (uint32_t)abs(sample));
			}
		}
		k_spinlock_key_t key = k_spin_lock(&state_lock);
		if (warmup) {
			warmup--;
		} else {
			state.ready = !atomic_get(&capture_stats.faults);
			memcpy(state.peak, peaks, sizeof(peaks));
			if (state.frames < state.target && !state.invalid) {
				uint32_t count = MIN(CAPTURE_FRAMES, state.target - state.frames);
				memcpy(recording + state.frames * MIC_CHANNELS, pcm,
				       count * MIC_CHANNELS * sizeof(int16_t));
				state.frames += count;
			}
		}
		uint32_t elapsed = k_cyc_to_us_floor32(k_cycle_get_32() - start);
		state.max_process_us = MAX(state.max_process_us, elapsed);
		state.filter_blocks++; state.filter_total_us += filter_us;
		k_spin_unlock(&state_lock, key);
		k_yield();
	}
}
