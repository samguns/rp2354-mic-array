#include "audio_settings.h"
#include "nr_control.h"
#include "dsp_parallel.h"
#include "capture.h"
#include "usb_audio.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/flash.h>
#include <zephyr/storage/flash_map.h>
#include <zephyr/sys/crc.h>
#include <zephyr/sys/reboot.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <math.h>
#define OFFSET PARTITION_OFFSET(audio_settings_partition)
#define MAGIC 0x4d494341u
BUILD_ASSERT(PARTITION_SIZE(audio_settings_partition)==8192);
BUILD_ASSERT(PARTITION_OFFSET(audio_settings_partition)+PARTITION_SIZE(audio_settings_partition)==DT_REG_SIZE(DT_CHOSEN(zephyr_flash)));
BUILD_ASSERT(sizeof(struct audio_config_record)<=768);
static const struct device *const flash=PARTITION_DEVICE(audio_settings_partition);
static struct audio_config_record stored,pending;
static bool loaded;static unsigned active_slot;
static atomic_t saving,save_error,dirty=ATOMIC_INIT(1);
void audio_settings_changed(void){atomic_set(&dirty,1);}
bool audio_settings_saving(void){return atomic_get(&saving)!=0;}
int audio_settings_error(void){return atomic_get(&save_error);}
static bool valid(const struct audio_config_record *r){
 for(unsigned i=0;i<129;i++)if(!isfinite(r->noise[i]) || r->noise[i]<0)return false;
 return r->learned<=1 && r->magic==MAGIC && r->version==1 && r->crc==crc32_ieee((const uint8_t *)r,offsetof(struct audio_config_record,crc)) &&
 r->nr_mode<=2 && (r->hum==0 || r->hum==50 || r->hum==60) && r->channel>=1 && r->channel<=4 && r->beam_mode<=2 && r->azimuth<3600 && r->elevation<=900;
}
bool audio_settings_load(struct audio_config_record *record){
 struct audio_config_record slots[2];loaded=false;
 if(!device_is_ready(flash))return false;
 for(unsigned i=0;i<2;i++){
  if(flash_read(flash,OFFSET+i*4096,&slots[i],sizeof(slots[i])) || !valid(&slots[i]))continue;
  if(!loaded || (int32_t)(slots[i].generation-stored.generation)>0){stored=slots[i];active_slot=i;loaded=true;}
 }
 atomic_set(&dirty,!loaded);
 if(loaded)*record=stored;
 return loaded;
}
static void snapshot(struct audio_config_record *r){
 struct output_status s;output_status_get(&s);memset(r,0,sizeof(*r));
 r->magic=MAGIC;r->version=1;r->generation=loaded?stored.generation+1:1;
 r->nr_mode=s.nr_mode;r->hum=s.hum;r->channel=s.channel?s.channel:1;
 r->beam_mode=s.beam_mode;r->azimuth=s.beam_mode==2?s.azimuth_tdeg:0;r->elevation=s.beam_mode==2?s.elevation_tdeg:900;
 noise_profile_copy(&r->learned,r->noise);
 r->crc=crc32_ieee((const uint8_t *)r,offsetof(struct audio_config_record,crc));
}
bool audio_settings_match(void){return loaded && !atomic_get(&dirty);}

static void save_work(struct k_work *work){
 ARG_UNUSED(work);
 /* All flash users on core 1 must park in SRAM before XIP is suspended.
  * Capture is intentionally stopped; flash changes are followed by a reboot. */
 if(usb_audio_active()){atomic_set(&save_error,-EBUSY);atomic_clear(&saving);return;}
 capture_fail();
 if(dsp_parallel_park()){sys_reboot(SYS_REBOOT_COLD);return;}
 unsigned key=irq_lock();
 static uint8_t page[768];memset(page,0xff,sizeof(page));memcpy(page,&pending,sizeof(pending));
 unsigned slot=loaded?(active_slot^1):0;
 int error=flash_erase(flash,OFFSET+slot*4096,4096);
 if(!error)error=flash_write(flash,OFFSET+slot*4096,page,sizeof(page));
 static struct audio_config_record check;
 if(!error)error=flash_read(flash,OFFSET+slot*4096,&check,sizeof(check));
 /* An interrupted/failed save leaves the older CRC-checked slot intact. */
 if(!error && valid(&check) && !memcmp(&check,&pending,sizeof(check)))stored=check;
 ARG_UNUSED(key);sys_reboot(SYS_REBOOT_COLD);
}
K_WORK_DELAYABLE_DEFINE(save_job,save_work);
int audio_settings_save_restart(void){
 if(usb_audio_active())return -EBUSY;
 struct output_status s;output_status_get(&s);if(s.pending || s.fade_left || s.learn_left || noise_control_pending())return -EAGAIN;
 if(!atomic_cas(&saving,0,1))return -EBUSY;
 atomic_clear(&save_error);snapshot(&pending);
 if(!valid(&pending)){atomic_clear(&saving);return -EINVAL;}
 k_work_reschedule(&save_job,K_MSEC(500));return 0;
}
