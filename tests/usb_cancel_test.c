#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#define BIT(n) (1u << (n))
#define USB_EP_DIR_IS_IN(ep) (((ep) & 0x80) != 0)
#define USB_EP_DIR_IS_OUT(ep) (!USB_EP_DIR_IS_IN(ep))
#define USB_EP_GET_IDX(ep) ((ep) & 15)
#define USB_BUF_CTRL_AVAIL BIT(10)
#define DIV_ROUND_UP(n, d) (((n) + (d) - 1) / (d))
#define LOG_INF(...) ((void)0)
#define LOG_DBG(...) ((void)0)
#define LOG_ERR(...) ((void)0)
typedef uintptr_t mm_reg_t;
typedef struct { uint32_t abort, abort_done, buf_status; } usb_hw_t;
struct udc_ep_config { unsigned addr, mps; struct { bool enabled; } stat; bool busy; };
struct rpi_pico_ep_data { void *buf; uint8_t next_pid; };
struct rpi_pico_data { uint32_t xfer_finished, xfer_new; };
struct rpi_pico_config { usb_hw_t *base; void *mem_block; };
struct device { const struct rpi_pico_config *config; };
static struct udc_ep_config cfg;
static struct rpi_pico_ep_data ep_data;
static struct rpi_pico_data priv;
static usb_hw_t hw;
static uint32_t buffer_control, endpoint_control;
static unsigned irq_depth, frees, handshake_count;
static bool disabling, fail_free;
static char allocation;

static unsigned irq_lock(void) { return irq_depth++; }
static void irq_unlock(unsigned key)
{
    assert(irq_depth == key + 1);
    irq_depth = key;
    /* No interrupt window between cancellation and disabling the endpoint. */
    assert(!disabling || irq_depth || !cfg.stat.enabled || fail_free);
}
static unsigned rp2040_chip_version(void) { return 2; }
static struct udc_ep_config *udc_get_ep_cfg(const struct device *d, uint8_t ep)
{ (void)d; assert(ep == cfg.addr); return &cfg; }
static struct rpi_pico_ep_data *get_ep_data(const struct device *d, uint8_t ep)
{ (void)d; assert(ep == cfg.addr); return &ep_data; }
static struct rpi_pico_data *udc_get_private(const struct device *d)
{ (void)d; return &priv; }
static void write_buf_ctrl_reg(const struct device *d, uint8_t ep, uint32_t value)
{ (void)d; (void)ep; assert(irq_depth); buffer_control = value; }
static void write_ep_ctrl_reg(const struct device *d, uint8_t ep, uint32_t value)
{ (void)d; (void)ep; assert(irq_depth); endpoint_control = value; }
static uint32_t sys_read32(mm_reg_t address) { return *(uint32_t *)address; }
static void sys_write32(uint32_t value, mm_reg_t address)
{
    assert(address == (mm_reg_t)&hw.abort_done);
    hw.abort_done &= ~value; /* Hardware write-one-to-clear semantics. */
}
static void rpi_pico_bit_set(mm_reg_t address, uint32_t bits)
{
    assert(irq_depth && address == (mm_reg_t)&hw.abort);
    assert(!(hw.abort_done & bits)); /* Must not accept a stale ACK. */
    hw.abort |= bits;
    /* Simulate a transfer finishing between cancel entry and the abort ACK. */
    buffer_control &= ~USB_BUF_CTRL_AVAIL;
    hw.buf_status |= bits;
    hw.abort_done |= bits;
    handshake_count++;
}
static void rpi_pico_bit_clr(mm_reg_t address, uint32_t bits)
{ assert(irq_depth); *(uint32_t *)address &= ~bits; }
static void atomic_clear_bit(uint32_t *value, unsigned bit) { *value &= ~BIT(bit); }
static void udc_ep_set_busy(struct udc_ep_config *c, bool value) { c->busy = value; }
static int sys_mem_blocks_free_contiguous(void *pool, void *buffer, size_t count)
{
    (void)pool;
    assert(irq_depth && endpoint_control == 0);
    assert(buffer == &allocation && count == DIV_ROUND_UP(cfg.mps, 64u));
    if (fail_free) return -1;
    frees++;
    return 0;
}

/* PRODUCTION_FUNCTIONS */

static void prepare(uint8_t ep, bool available)
{
    memset(&hw, 0, sizeof(hw));
    memset(&priv, 0, sizeof(priv));
    cfg = (struct udc_ep_config){.addr=ep, .mps=392, .stat={.enabled=true}, .busy=true};
    ep_data.buf = &allocation;
    buffer_control = available ? USB_BUF_CTRL_AVAIL : 0;
    endpoint_control = 1;
    irq_depth = frees = handshake_count = 0;
    disabling = fail_free = false;
    /* Include unrelated endpoints; cancellation must preserve their state. */
    hw.abort_done = get_ep_mask(ep) | BIT(31);
    hw.buf_status = get_ep_mask(ep) | BIT(31);
    priv.xfer_finished = BIT(udc_ep_to_bnum(ep)) | BIT(31);
    priv.xfer_new = priv.xfer_finished;
}

int main(void)
{
    const uint8_t endpoints[] = {0, 0x80, 3, 0x84};
    struct rpi_pico_config config = {.base=&hw};
    struct device dev = {.config=&config};
    for (unsigned n=0; n<sizeof(endpoints); n++) {
        uint8_t ep=endpoints[n];
        for (unsigned available=0; available<2; available++) {
            prepare(ep, available);
            rpi_pico_ep_cancel(&dev, ep);
            assert(!irq_depth && !cfg.busy && !buffer_control && !hw.abort);
            assert(handshake_count == 1);
            assert(hw.abort_done == BIT(31) && hw.buf_status == BIT(31));
            assert(priv.xfer_finished == BIT(31) && priv.xfer_new == BIT(31));
            prepare(ep, available);
            disabling = true;
            assert(udc_rpi_pico_ep_disable(&dev, &cfg) == 0);
            disabling = false;
            assert(!irq_depth && !cfg.stat.enabled);
            assert(frees == (USB_EP_GET_IDX(ep) != 0));
            if (USB_EP_GET_IDX(ep)) assert(!ep_data.buf);
        }
    }
    prepare(3, true);
    fail_free = disabling = true;
    assert(udc_rpi_pico_ep_disable(&dev, &cfg) == -1);
    assert(!irq_depth && cfg.stat.enabled && ep_data.buf == &allocation && !frees);
    return 0;
}
