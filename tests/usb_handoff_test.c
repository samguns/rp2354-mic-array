#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#define USB_BUF_CTRL_AVAIL (1u << 10)
#define USB_BUF_CTRL_DATA1_PID (1u << 13)
#define USB_BUF_CTRL_DATA0_PID 0u
#define USB_BUF_CTRL_FULL (1u << 15)
#define MIN(a,b) ((a)<(b)?(a):(b))
#define LOG_DBG(...) fake_log(__VA_ARGS__)
#define LOG_ERR(...) ((void)0)
static void fake_log(const char *fmt, ...) { (void)fmt; }
/* One CPU cycle at 150 MHz, expressed in units of 1/150 MHz. */
#define arch_nop() (++elapsed_cycles)
struct device { int unused; };
struct net_buf { unsigned char *data; size_t len; };
struct udc_ep_config { uint8_t addr; uint16_t mps; };
struct rpi_pico_ep_data { void *buf; uint8_t next_pid; };
static unsigned char dpram[64];
static struct rpi_pico_ep_data ep = {.buf=dpram};
static uint32_t control;
static unsigned elapsed_cycles, handoffs, irq_depth;
static unsigned irq_lock(void) { return irq_depth++; }
static void irq_unlock(unsigned key) { assert(irq_depth == key+1); irq_depth=key; }
static struct rpi_pico_ep_data *get_ep_data(const struct device *d, uint8_t addr)
{ (void)d; (void)addr; return &ep; }
static uint32_t read_buf_ctrl_reg(const struct device *d, uint8_t addr)
{ (void)d; (void)addr; return control; }
static void write_buf_ctrl_reg(const struct device *d, uint8_t addr, uint32_t value)
{
 (void)d; (void)addr; assert(irq_depth);
 if (value & USB_BUF_CTRL_AVAIL) {
  /* ceil(150/48) = 4 cycles minimum before USB owns the metadata. */
  assert(elapsed_cycles >= 4);
  assert(value == (control | USB_BUF_CTRL_AVAIL));
  handoffs++;
 } else { elapsed_cycles = 0; }
 control = value;
}
static void k_busy_wait(unsigned us) { elapsed_cycles += us*150; }
static void dpram_memcpy(void *dst, const void *src, size_t n) { memcpy(dst,src,n); }
static size_t net_buf_tailroom(struct net_buf *buf) { (void)buf; return 64; }
/* PRODUCTION_FUNCTIONS */
int main(void) {
 struct device dev={0}; struct udc_ep_config cfg={.addr=0x80,.mps=64};
 unsigned char payload[8]={1,2,3,4,5,6,7,8};
 struct net_buf buf={.data=payload,.len=8};
 assert(rpi_pico_prep_tx(&dev,&buf,&cfg)==0);
 assert(!memcmp(payload,dpram,8) && (control & 0x3ff)==8);
 assert(control & USB_BUF_CTRL_FULL);
 assert(ep.next_pid==1 && handoffs==1 && !irq_depth);
 assert(rpi_pico_prep_tx(&dev,&buf,&cfg)==-EBUSY);
 assert(handoffs==1 && ep.next_pid==1);
 control=0; cfg.addr=0;
 assert(rpi_pico_prep_rx(&dev,&buf,&cfg)==0);
 assert((control & 0x3ff)==64 && (control & USB_BUF_CTRL_DATA1_PID));
 assert(!(control & USB_BUF_CTRL_FULL));
 assert(handoffs==2 && ep.next_pid==0 && !irq_depth);
 assert(rpi_pico_prep_rx(&dev,&buf,&cfg)==-EBUSY);
 assert(handoffs==2 && !irq_depth);
 return 0;
}
