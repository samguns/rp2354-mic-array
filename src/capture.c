#include "capture.h"
#include "board_pins.h"
#include <zephyr/device.h>
#include <zephyr/drivers/misc/pio_rpi_pico/pio_rpi_pico.h>
#include <zephyr/drivers/reset.h>
#include <zephyr/irq.h>
#include <hardware/clocks.h>
#include <hardware/dma.h>
#include <hardware/timer.h>
#include <hardware/pio_instructions.h>

#define CLOCK_PIN MIC_CLOCK_PIN
#define DATA_PIN MIC_DATA1_PIN
#define DMA_NODE DT_NODELABEL(dma)
#define DMA_IRQ_INDEX 2
#define DMA_IRQ DT_IRQ_BY_IDX(DMA_NODE, DMA_IRQ_INDEX, irq)

BUILD_ASSERT(!IS_ENABLED(CONFIG_DMA), "Capture requires exclusive DMA controller ownership");
/* Ring each DMA destination within its own 4 KiB data area. If an IRQ is
 * delayed past the chaining deadline, hardware must not write beyond the
 * buffer before we can detect and stop the stream. Metadata lives outside
 * that ring. Padding costs RAM but keeps every buffer independently owned. */
static uint32_t raw_data[CAPTURE_BUFFERS][1024] __aligned(4096);
K_MEM_SLAB_DEFINE(raw_pool, sizeof(struct capture_block), CAPTURE_BUFFERS, 4);

static int allocate_raw(struct capture_block **block)
{
	int ret = k_mem_slab_alloc(&raw_pool, (void **)block, K_NO_WAIT);
	if (!ret) {
		size_t index = ((char *)*block - raw_pool.buffer) / sizeof(struct capture_block);
		(*block)->words = raw_data[index];
	}
	return ret;
}
K_MSGQ_DEFINE(raw_queue, sizeof(struct capture_block *), CAPTURE_BUFFERS, 4);
struct capture_stats capture_stats;
static PIO pio;
static size_t sm;
static int channels[2] = {-1, -1};
static struct capture_block *active[2];
static uint32_t channel_mask;
static uint32_t sequence;
static unsigned expected;
static bool counter_initialized;
static size_t counter_sm;
static unsigned counter_offset;

static void fault_stop(void)
{
	pio_sm_set_enabled(pio, sm, false);
	dma_hw->inte2 &= ~channel_mask;
	/* Abort is deliberately deferred to reboot; no waiting in the ISR. */
	pio_sm_set_pins_with_mask(pio, sm, 0, BIT(CLOCK_PIN));
	atomic_inc(&capture_stats.faults);
}

void capture_fail(void)
{
 unsigned key=irq_lock();fault_stop();irq_unlock(key);
}

static void dma_isr(const void *unused)
{
	ARG_UNUSED(unused);
	uint32_t pending = dma_hw->ints2 & channel_mask;
	if (pending != BIT(channels[expected])) {
		/* Both completed means the rearm deadline was missed. Do not silently
		 * continue with reordered blocks or a stopped microphone clock. */
		dma_hw->ints2 = pending;
		fault_stop();
		return;
	}
	unsigned slot = expected;
	unsigned ch = channels[slot];
	dma_hw->ints2 = BIT(ch);
	if ((dma_hw->ch[ch].ctrl_trig & DMA_CH0_CTRL_TRIG_AHB_ERROR_BITS) ||
	    (pio->fdebug & BIT(PIO_FDEBUG_RXSTALL_LSB + sm))) {
		fault_stop();
		return;
	}
	struct capture_block *done = active[slot];
	struct capture_block *next;
	done->sequence = sequence++;
 done->end_us=time_us_32();
	atomic_inc(&capture_stats.blocks);
	if (allocate_raw(&next) != 0) {
		/* Reuse only this finished block; never overwrite a consumer buffer. */
		next = done;
		atomic_inc(&capture_stats.dropped);
	} else if (k_msgq_put(&raw_queue, &done, K_NO_WAIT) != 0) {
		k_mem_slab_free(&raw_pool, done);
		atomic_inc(&capture_stats.dropped);
	}
	active[slot] = next;
	dma_channel_set_write_addr(ch, next->words, false);
	dma_channel_set_trans_count(ch, CAPTURE_WORDS, false);
	expected ^= 1u;
}

int capture_start(void)
{
	const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(mic_pio));
	const struct reset_dt_spec reset = RESET_DT_SPEC_GET(DMA_NODE);
	if (!device_is_ready(dev) || !device_is_ready(reset.dev)) { return -ENODEV; }
	int ret = pio_rpi_pico_allocate_sm(dev, &sm);
	if (ret) { return ret; }
	pio = pio_rpi_pico_get_pio(dev);
	/* LR=GND selects the right channel, driven during CLOCK low. Sample
	 * late in the low half, before the rising edge releases DATA. */
	uint16_t instructions[] = {
		pio_encode_nop() | pio_encode_sideset(1, 0) | pio_encode_delay(3),
		pio_encode_in(pio_pins, 4) | pio_encode_sideset(1, 0),
		pio_encode_nop() | pio_encode_sideset(1, 1) | pio_encode_delay(4),
	};
	struct pio_program program = {.instructions = instructions, .length = 3, .origin = -1};
	if (!pio_can_add_program(pio, &program)) { return -ENOMEM; }
	unsigned offset = pio_add_program(pio, &program);
	pio_sm_config config = pio_get_default_sm_config();
	sm_config_set_wrap(&config, offset, offset + 2);
	sm_config_set_in_pins(&config, DATA_PIN);
	sm_config_set_sideset(&config, 1, false, false);
	sm_config_set_sideset_pins(&config, CLOCK_PIN);
	sm_config_set_in_shift(&config, true, true, 32);
	sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
	uint32_t sys_hz = clock_get_hz(clk_sys);
	uint32_t div256 = ((uint64_t)sys_hz * 256u + PDM_CLOCK_HZ * 5u) /
			  (PDM_CLOCK_HZ * 10u);
	sm_config_set_clkdiv_int_frac8(&config, div256 / 256u, div256 % 256u);
	capture_stats.sample_rate = ((uint64_t)sys_hz * 256u + div256 * 5u * PDM_DECIMATION) /
				    (div256 * 10u * PDM_DECIMATION);
	for (unsigned pin = CLOCK_PIN; pin <= DATA_PIN + 3; pin++) {
		pio_gpio_init(pio, pin);
	}
	pio_sm_init(pio, sm, offset, &config);
	pio_sm_set_consecutive_pindirs(pio, sm, CLOCK_PIN, 1, true);
	pio_sm_set_consecutive_pindirs(pio, sm, DATA_PIN, 4, false);
	pio_sm_set_pins_with_mask(pio, sm, 0, BIT(CLOCK_PIN));
	ret = reset_line_toggle_dt(&reset);
	if (ret) { return ret; }
	for (unsigned i = 0; i < 2; i++) {
		channels[i] = dma_claim_unused_channel(false);
		if (channels[i] < 0) { return -EBUSY; }
		ret = allocate_raw(&active[i]);
		if (ret) { return ret; }
		channel_mask |= BIT(channels[i]);
	}
	for (unsigned i = 0; i < 2; i++) {
		dma_channel_config dma_config = dma_channel_get_default_config(channels[i]);
		channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
		channel_config_set_read_increment(&dma_config, false);
		channel_config_set_write_increment(&dma_config, true);
		channel_config_set_ring(&dma_config, true, 12);
		channel_config_set_dreq(&dma_config, pio_get_dreq(pio, sm, false));
		channel_config_set_chain_to(&dma_config, channels[i ^ 1]);
		dma_channel_configure(channels[i], &dma_config, active[i]->words,
				      &pio->rxf[sm], CAPTURE_WORDS, false);
	}
	IRQ_CONNECT(DMA_IRQ, 1, dma_isr, NULL, 0);
	dma_hw->ints2 = channel_mask;
	dma_hw->inte2 |= channel_mask;
	irq_enable(DMA_IRQ);
	pio->fdebug = BIT(PIO_FDEBUG_RXSTALL_LSB + sm);
	dma_start_channel_mask(BIT(channels[0]));
	pio_sm_set_enabled(pio, sm, true);
	return 0;
}

int capture_read(struct capture_block **block, k_timeout_t timeout)
{
	return k_msgq_get(&raw_queue, block, timeout);
}

void capture_release(struct capture_block *block)
{
	k_mem_slab_free(&raw_pool, block);
}

int capture_clock_measure(uint32_t *edges, uint32_t *elapsed_us)
{
	if (!counter_initialized) {
		const struct device *dev = DEVICE_DT_GET(DT_NODELABEL(mic_pio));
		int ret = pio_rpi_pico_allocate_sm(dev, &counter_sm);
		if (ret) { return ret; }
		uint16_t instructions[] = {
			pio_encode_wait_gpio(false, CLOCK_PIN),
			pio_encode_wait_gpio(true, CLOCK_PIN),
			pio_encode_jmp_x_dec(0),
		};
		struct pio_program program = {.instructions = instructions, .length = 3, .origin = -1};
		if (!pio_can_add_program(pio, &program)) {
			pio_sm_unclaim(pio, counter_sm);
			return -ENOMEM;
		}
		counter_offset = pio_add_program(pio, &program);
		counter_initialized = true;
	}
	pio_sm_config config = pio_get_default_sm_config();
	sm_config_set_wrap(&config, counter_offset, counter_offset + 2);
	pio_sm_init(pio, counter_sm, counter_offset, &config);
	pio_sm_clear_fifos(pio, counter_sm);
	pio_sm_restart(pio, counter_sm);
	pio_sm_exec(pio, counter_sm, pio_encode_jmp(counter_offset));
	pio_sm_exec(pio, counter_sm, pio_encode_mov_not(pio_x, pio_null));
	uint32_t start = k_cycle_get_32();
	pio_sm_set_enabled(pio, counter_sm, true);
	k_msleep(20);
	pio_sm_set_enabled(pio, counter_sm, false);
	*elapsed_us = k_cyc_to_us_floor32(k_cycle_get_32() - start);
	pio_sm_exec(pio, counter_sm, pio_encode_mov(pio_isr, pio_x));
	pio_sm_exec(pio, counter_sm, pio_encode_push(false, true));
	*edges = ~pio_sm_get(pio, counter_sm);
	return 0;
}

static int probe_read(const pio_sm_config *config, unsigned offset, uint32_t *words, unsigned count)
{
	pio_sm_init(pio, counter_sm, offset, config);
	int channel = dma_claim_unused_channel(false);
	if (channel < 0) { return -EBUSY; }
	dma_channel_config dma_config = dma_channel_get_default_config(channel);
	channel_config_set_transfer_data_size(&dma_config, DMA_SIZE_32);
	channel_config_set_read_increment(&dma_config, false);
	channel_config_set_write_increment(&dma_config, true);
	channel_config_set_dreq(&dma_config, pio_get_dreq(pio, counter_sm, false));
	dma_channel_configure(channel, &dma_config, words, &pio->rxf[counter_sm], count, true);
	pio_sm_set_enabled(pio, counter_sm, true);
	int64_t deadline = k_uptime_get() + 200;
	while (dma_channel_is_busy(channel) && k_uptime_get() < deadline) { k_msleep(1); }
	bool timed_out = dma_channel_is_busy(channel);
	pio_sm_set_enabled(pio, counter_sm, false);
	dma_channel_cleanup(channel);
	dma_channel_unclaim(channel);
	return timed_out ? -ETIMEDOUT : 0;
}

int capture_phase_read(uint32_t *words, unsigned count)
{
	static bool initialized;
	static unsigned offset;
	if (!counter_initialized) { return -ENODEV; }
	if (!initialized) {
		/* Sample about 100 ns after detecting each falling/rising edge, well
		 * inside the respective half-cycle at the default PDM clock. */
		uint16_t instructions[] = {
			pio_encode_wait_gpio(false, CLOCK_PIN),
			pio_encode_nop() | pio_encode_delay(13),
			pio_encode_in(pio_pins, 4),
			pio_encode_wait_gpio(true, CLOCK_PIN),
			pio_encode_nop() | pio_encode_delay(13),
			pio_encode_in(pio_pins, 4),
		};
		struct pio_program program = {.instructions = instructions, .length = 6, .origin = -1};
		if (!pio_can_add_program(pio, &program)) { return -ENOMEM; }
		offset = pio_add_program(pio, &program);
		initialized = true;
	}
	pio_sm_config config = pio_get_default_sm_config();
	sm_config_set_wrap(&config, offset, offset + 5);
	sm_config_set_in_pins(&config, DATA_PIN);
	sm_config_set_in_shift(&config, true, true, 32);
	sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
	return probe_read(&config, offset, words, count);
}

int capture_clock_trace(uint32_t *words, unsigned count)
{
	static bool initialized;
	static unsigned offset;
	if (!counter_initialized) { return -ENODEV; }
	if (!initialized) {
		uint16_t instruction = pio_encode_in(pio_pins, 1);
		struct pio_program program = {.instructions = &instruction, .length = 1, .origin = -1};
		if (!pio_can_add_program(pio, &program)) { return -ENOMEM; }
		offset = pio_add_program(pio, &program);
		initialized = true;
	}
	pio_sm_config config = pio_get_default_sm_config();
	sm_config_set_wrap(&config, offset, offset);
	sm_config_set_in_pins(&config, CLOCK_PIN);
	sm_config_set_in_shift(&config, true, true, 32);
	sm_config_set_fifo_join(&config, PIO_FIFO_JOIN_RX);
	return probe_read(&config, offset, words, count);
}
