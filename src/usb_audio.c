#include "usb_audio.h"
#include "capture.h"
#include <zephyr/usb/usbd.h>
#include <zephyr/usb/class/usbd_uac2.h>
#include <zephyr/shell/shell.h>
#include <string.h>

#define RING_FRAMES 2048
/* Keep a 32-ms producer reserve: CDC/control bursts can postpone capture
 * delivery beyond the former 16-ms reserve. No additional ring RAM is used. */
#define PRIME_FRAMES 1536
#define LOW_FRAMES 1280
#define HIGH_FRAMES 1792
#define TERMINAL UAC2_ENTITY_ID(DT_NODELABEL(mic_usb))
static const struct device *const audio = DEVICE_DT_GET(DT_NODELABEL(mic_audio));
USBD_DEVICE_DEFINE(mic_usb_device, DEVICE_DT_GET(DT_NODELABEL(zephyr_udc0)), 0x2fe3, 0x0006);
USBD_DESC_LANG_DEFINE(mic_lang);
USBD_DESC_MANUFACTURER_DEFINE(mic_mfr, "Pico Array Prototype");
USBD_DESC_PRODUCT_DEFINE(mic_product, "Pico 2 Mic Array UAC2");
USBD_DESC_SERIAL_NUMBER_DEFINE(mic_serial);
USBD_DESC_CONFIG_DEFINE(mic_config_desc, "Audio, shell and telemetry");
USBD_CONFIGURATION_DEFINE(mic_config, 0, 100, &mic_config_desc);
K_MEM_SLAB_DEFINE(audio_packets, ROUND_UP(49 * 8, UDC_BUF_GRANULARITY), 8, UDC_BUF_ALIGN);
static int16_t ring[RING_FRAMES][4];
static struct k_spinlock lock;
static struct {
	bool enabled, primed, have_sequence, pattern;
	unsigned read, fill;
	uint32_t expected, starts, stops, in_frames, out_frames;
	uint32_t packets, short_packets, long_packets, startup_silence;
	uint32_t underruns, overruns, gaps, send_errors, pool_errors;
	uint32_t epoch, busy_startup, busy_active;
	unsigned min_fill, max_fill;
} state;

bool usb_audio_active(void)
{
 k_spinlock_key_t key = k_spin_lock(&lock);
 bool active = state.enabled; k_spin_unlock(&lock, key); return active;
}

__ramfunc __attribute__((optimize("O3"))) void usb_audio_push(const int16_t *pcm, uint32_t sequence)
{
	k_spinlock_key_t key = k_spin_lock(&lock);
	if (!state.enabled) { k_spin_unlock(&lock, key); return; }
	if (state.have_sequence && sequence != state.expected) {
		state.gaps++; state.epoch++; state.fill = 0; state.read = 0; state.primed = false;
	}
	state.have_sequence = true; state.expected = sequence + 1;
	if (state.fill + CAPTURE_FRAMES > RING_FRAMES) {
		state.overruns++; state.epoch++; state.fill = 0; state.read = 0; state.primed = false;
	}
	for (unsigned n = 0; n < CAPTURE_FRAMES; n++) {
		int16_t *frame = ring[(state.read + state.fill + n) % RING_FRAMES];
		if (state.pattern) {
			uint16_t value = sequence * CAPTURE_FRAMES + n;
			frame[0] = value; frame[1] = value ^ 0x1357;
			frame[2] = value ^ 0x2468; frame[3] = value ^ 0x55aa;
		} else { memcpy(frame, pcm + n*4, 8); }
	}
	state.fill += CAPTURE_FRAMES; state.in_frames += CAPTURE_FRAMES;
	state.max_fill = MAX(state.max_fill, state.fill);
	k_spin_unlock(&lock, key);
}

static void terminal_update(const struct device *dev, uint8_t terminal,
			    bool enabled, bool microframes, void *user)
{
	ARG_UNUSED(dev); ARG_UNUSED(user);
	if (terminal != TERMINAL) { return; }
	k_spinlock_key_t key = k_spin_lock(&lock);
	state.enabled = enabled && !microframes;
	state.primed = false; state.have_sequence = false;
	state.fill = 0; state.read = 0; state.epoch++;
	if (enabled) { state.min_fill = RING_FRAMES; }
	if (enabled) { state.starts++; } else { state.stops++; }
	k_spin_unlock(&lock, key);
}

static void release_packet(const struct device *dev, uint8_t terminal, void *buf, void *user)
{
	ARG_UNUSED(dev); ARG_UNUSED(terminal); ARG_UNUSED(user);
	k_mem_slab_free(&audio_packets, buf);
}

static __ramfunc __attribute__((optimize("O3"))) void sof(const struct device *dev, void *user)
{
	ARG_UNUSED(user);
	k_spinlock_key_t key = k_spin_lock(&lock);
	bool enabled = state.enabled;
	k_spin_unlock(&lock, key);
	if (!enabled) { return; }
	int16_t *packet;
	if (k_mem_slab_alloc(&audio_packets, (void **)&packet, K_NO_WAIT)) {
		key = k_spin_lock(&lock); state.pool_errors++; k_spin_unlock(&lock, key); return;
	}
	unsigned frames = PCM_SAMPLE_RATE / 1000u;
	bool staged = false;
	key = k_spin_lock(&lock);
	uint32_t epoch = state.epoch;
	if (!state.primed && state.fill >= PRIME_FRAMES) { state.primed = true; }
	if (state.primed) {
		/* 128-frame producer blocks cause a 2.67-ms sawtooth. Wide deadband
		 * absorbs that jitter; +/- one complete frame handles clock drift. */
		if (state.fill < LOW_FRAMES) { frames = 47; }
		if (state.fill > HIGH_FRAMES) { frames = 49; }
		if (state.fill < frames) {
			state.underruns++; state.primed = false;
		} else {
			for (unsigned n = 0; n < frames; n++) {
				memcpy(packet + n*4, ring[(state.read + n) % RING_FRAMES], 8);
			}
			staged = true;
		}
	}
	if (!state.primed) {
		memset(packet, 0, frames*8); state.startup_silence += frames;
	}
	k_spin_unlock(&lock, key);
	int ret = usbd_uac2_send(dev, TERMINAL, packet, frames*8);
	key = k_spin_lock(&lock);
	if (ret == -EAGAIN) {
		if (staged) { state.busy_active++; } else { state.busy_startup++; }
	} else if (ret) { state.send_errors++; }
	else {
		if (staged && state.epoch == epoch) {
			state.read = (state.read + frames) % RING_FRAMES;
			state.fill -= frames; state.out_frames += frames;
			state.min_fill = MIN(state.min_fill, state.fill);
		}
		state.packets++;
		if (frames == 47) { state.short_packets++; }
		if (frames == 49) { state.long_packets++; }
	}
	k_spin_unlock(&lock, key);
	if (ret) { k_mem_slab_free(&audio_packets, packet); }
}

static const struct uac2_ops ops = {
	.sof_cb = sof, .terminal_update_cb = terminal_update, .buf_release_cb = release_packet,
};

int usb_audio_init(void)
{
	usbd_uac2_set_ops(audio, &ops, NULL);
	int ret;
#define CHECK(call) do { ret = (call); if (ret) { return ret; } } while (0)
	CHECK(usbd_add_descriptor(&mic_usb_device, &mic_lang));
	CHECK(usbd_add_descriptor(&mic_usb_device, &mic_mfr));
	CHECK(usbd_add_descriptor(&mic_usb_device, &mic_product));
	CHECK(usbd_add_descriptor(&mic_usb_device, &mic_serial));
	CHECK(usbd_add_configuration(&mic_usb_device, USBD_SPEED_FS, &mic_config));
	CHECK(usbd_register_class(&mic_usb_device, "cdc_acm_0", USBD_SPEED_FS, 1));
	CHECK(usbd_register_class(&mic_usb_device, "cdc_acm_1", USBD_SPEED_FS, 1));
	CHECK(usbd_register_class(&mic_usb_device, "uac2_0", USBD_SPEED_FS, 1));
	CHECK(usbd_device_set_code_triple(&mic_usb_device, USBD_SPEED_FS, USB_BCC_MISCELLANEOUS, 2, 1));
	CHECK(usbd_init(&mic_usb_device));
	CHECK(usbd_enable(&mic_usb_device));
	return 0;
}

static int stats(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc); ARG_UNUSED(argv);
	k_spinlock_key_t key = k_spin_lock(&lock);
	__typeof__(state) snapshot = state;
	k_spin_unlock(&lock, key);
	shell_print(sh, "UAC enabled=%u primed=%u fill=%u starts=%u stops=%u packets=%u in=%u out=%u short=%u long=%u silence=%u underruns=%u overruns=%u gaps=%u send_errors=%u pool_errors=%u min_fill=%u max_fill=%u busy_startup=%u busy_active=%u",
		snapshot.enabled, snapshot.primed, snapshot.fill, snapshot.starts, snapshot.stops,
		snapshot.packets, snapshot.in_frames, snapshot.out_frames, snapshot.short_packets,
		snapshot.long_packets, snapshot.startup_silence, snapshot.underruns, snapshot.overruns,
		snapshot.gaps, snapshot.send_errors, snapshot.pool_errors, snapshot.min_fill, snapshot.max_fill,
		snapshot.busy_startup, snapshot.busy_active);
	return 0;
}
SHELL_CMD_REGISTER(uacstats, NULL, "USB audio health (cumulative since reboot)", stats);

static int pattern_cmd(const struct shell *sh, size_t argc, char **argv)
{
	ARG_UNUSED(argc);
	if (strcmp(argv[1], "on") && strcmp(argv[1], "off")) { return -EINVAL; }
	k_spinlock_key_t key = k_spin_lock(&lock);
	if (state.enabled) { k_spin_unlock(&lock, key); return -EBUSY; }
	state.pattern = !strcmp(argv[1], "on");
	bool enabled = state.pattern;
	k_spin_unlock(&lock, key);
	shell_print(sh, "UAC_PATTERN %s", enabled ? "ON" : "OFF");
	return 0;
}
SHELL_CMD_ARG_REGISTER(uacpattern, NULL, "Diagnostic ramp on/off; close audio first", pattern_cmd, 2, 0);
