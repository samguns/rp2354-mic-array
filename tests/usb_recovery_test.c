#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#define USB_SIE_CTRL_PULLUP_EN_BITS 1u
typedef int k_spinlock_key_t;
static int lock, completed_packets;
static int usb_recoveries;
static struct { bool enabled; } state;
static struct { uint32_t sie_ctrl; } hw, *usb_hw = &hw;
static uint32_t now;
static uint32_t k_uptime_get_32(void) { return now; }
static int k_spin_lock(int *p) { (void)p; return 0; }
static void k_spin_unlock(int *p, int key) { (void)p; (void)key; }
static int atomic_get(int *p) { return *p; }
static void atomic_inc(int *p) { ++*p; }
static void hw_set_bits(uint32_t *p, uint32_t bits) { *p |= bits; }
static void hw_clear_bits(uint32_t *p, uint32_t bits) { *p &= ~bits; }
/* PRODUCTION_FUNCTIONS */
int main(void) {
 hw.sie_ctrl = 3; now = 2000; usb_audio_service();
 assert(hw.sie_ctrl == 3 && usb_recoveries == 0);
 state.enabled = true;
 now = 3000; completed_packets++; usb_audio_service();
 now = 4000; usb_audio_service(); /* Exactly one second is allowed. */
 assert(hw.sie_ctrl == 3 && usb_recoveries == 0);
 now++; usb_audio_service();
 assert(hw.sie_ctrl == 2 && usb_recoveries == 1 && state.enabled);
 now += 99; usb_audio_service();
 assert(hw.sie_ctrl == 2 && usb_recoveries == 1);
 now++; usb_audio_service();
 assert(hw.sie_ctrl == 3 && usb_recoveries == 1);
 now += 900; completed_packets++; usb_audio_service();
 now += 900; completed_packets++; usb_audio_service();
 assert(hw.sie_ctrl == 3 && usb_recoveries == 1);
 state.enabled = false; now = UINT32_MAX - 50; usb_audio_service();
 state.enabled = true; now = 949; usb_audio_service();
 assert(hw.sie_ctrl == 3 && usb_recoveries == 1); /* Wrap: 1000 ms. */
 now++; usb_audio_service();
 assert(hw.sie_ctrl == 2 && usb_recoveries == 2);
 now += 100; usb_audio_service();
 state.enabled = false; now += 5000; usb_audio_service();
 assert(hw.sie_ctrl == 3 && usb_recoveries == 2);
 return 0;
}
