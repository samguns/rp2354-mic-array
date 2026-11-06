#include "doa.h"
#include "capture.h"
#include "usb_audio.h"
#include "dsp_parallel.h"
#include "doa_buffer.h"
#include "telemetry.h"
#include "nr_control.h"
#include <zephyr/sys/printk.h>
#include <hardware/timer.h>
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/shell/shell.h>
#include <string.h>
BUILD_ASSERT(PCM_SAMPLE_RATE==DOA_RATE,"DOA requires 48 kHz capture");
BUILD_ASSERT(DOA_FRAMES%CAPTURE_FRAMES==0,"DOA snapshots must contain complete capture blocks");

static struct doa_workspace workspace;
/* Full frame is retained only for one-shot numerical diagnostics. */
static int16_t snapshot[DOA_FRAMES*4];
static struct doa_window_queue windows;
static struct k_spinlock queue_lock,result_lock;
static atomic_t enabled,busy,generation,valid,frame_resets;
static unsigned feed_generation,oneshot_frames;
static uint32_t oneshot_first,analysis_sequence;
static unsigned cadence_frames,skip_frames;
K_SEM_DEFINE(doa_ready,0,1);
static struct {
 struct doa_result result;
 uint32_t updates,accepted,last_us,max_us,last_ms,epoch,interval_ms,max_interval_ms,active_us,max_active_us,cic_wait_us;
 uint32_t end_latency_us,max_end_latency_us;
} info;

__ramfunc __attribute__((optimize("O3"))) void doa_feed(const int16_t *pcm,uint32_t sequence,uint32_t end_us)
{
 int mode=atomic_get(&enabled);
 if(mode!=1 && mode!=2)return;
 unsigned epoch=atomic_get(&generation);
 if(epoch!=feed_generation){feed_generation=epoch;oneshot_frames=0;cadence_frames=skip_frames=0;analysis_sequence=sequence;}
 /* Manual FIR steering shares core 1. Skip whole analysis frames, never
  * individual FFT windows, to reserve compute without mixing discontinuous PCM.
  * Queue sequence counts analyzed blocks; capture still checks every raw block. */
 if(mode==1){
  if(skip_frames){skip_frames-=CAPTURE_FRAMES;return;}
  cadence_frames+=CAPTURE_FRAMES;
  if(cadence_frames==DOA_FRAMES){cadence_frames=0;if(beam_manual_active())skip_frames=DOA_FRAMES;}
  sequence=analysis_sequence++;
 }
 if(mode==2){
  if(!oneshot_frames)oneshot_first=sequence;
  memcpy(snapshot+oneshot_frames*4,pcm,CAPTURE_FRAMES*4*sizeof(int16_t));
  oneshot_frames+=CAPTURE_FRAMES;
 }
 unsigned offset;
 k_spinlock_key_t key=k_spin_lock(&queue_lock);
 int slot=doa_queue_reserve(&windows,epoch,sequence,&offset);
 k_spin_unlock(&queue_lock,key);
 if(slot>=0){
  memcpy(windows.slot[slot].pcm+offset*4,pcm,CAPTURE_FRAMES*4*sizeof(int16_t));
  key=k_spin_lock(&queue_lock);
  int ready=doa_queue_commit(&windows,sequence,end_us,CAPTURE_FRAMES);
  /* Publish the one-shot stop before a higher-priority consumer can wake. */
  if(mode==2 && oneshot_frames==DOA_FRAMES)atomic_set(&enabled,3);
  k_spin_unlock(&queue_lock,key);
  if(ready)k_sem_give(&doa_ready);
 }
 if(slot<0 && mode==2 && oneshot_frames==DOA_FRAMES)atomic_set(&enabled,3);
}
static void worker(void *a,void *b,void *c)
{
 ARG_UNUSED(a);ARG_UNUSED(b);ARG_UNUSED(c);
 doa_init(&workspace);
 unsigned index=0,epoch=0;uint32_t expected=0,first=0,elapsed_us=0,active_total=0;
 struct doa_result result;
 for(;;){
  k_sem_take(&doa_ready,K_FOREVER);
  if(!atomic_get(&enabled)){index=0;atomic_set(&busy,0);continue;}
  for(;;){
   k_spinlock_key_t key=k_spin_lock(&queue_lock);
   int slot=doa_queue_take_latest(&windows);
   unsigned window_epoch=slot>=0?windows.slot[slot].epoch:0;
   uint32_t first_seq=slot>=0?windows.slot[slot].first_sequence:0;
   uint32_t last_seq=slot>=0?windows.slot[slot].last_sequence:0;
   uint32_t end_us=slot>=0?windows.slot[slot].end_us:0;
   k_spin_unlock(&queue_lock,key);
   if(slot<0)break;
   if(window_epoch!=(unsigned)atomic_get(&generation) || !atomic_get(&enabled)){
    key=k_spin_lock(&queue_lock);doa_queue_release(&windows,slot);k_spin_unlock(&queue_lock,key);
    index=0;atomic_set(&busy,0);continue;
   }
   if(!index || epoch!=window_epoch || first_seq!=expected){
    if(index)atomic_inc(&frame_resets);
    index=0;epoch=window_epoch;first=first_seq;elapsed_us=0;active_total=0;
   }
   atomic_set(&busy,1);
   uint32_t start=k_uptime_get_32();
   int error=dsp_parallel_doa_window(&workspace,windows.slot[slot].pcm,&result,index);
   elapsed_us+=(k_uptime_get_32()-start)*1000;
   uint32_t active_us,cic_wait_us;dsp_parallel_doa_profile(&active_us,&cic_wait_us);active_total+=active_us;
   key=k_spin_lock(&queue_lock);doa_queue_release(&windows,slot);k_spin_unlock(&queue_lock,key);
   /* The fit uses spectra only. Return PCM ownership before starting it. */
   if(!error && index+1==DOA_WINDOWS){
    start=k_uptime_get_32();
    error=dsp_parallel_doa_window(&workspace,NULL,&result,DOA_WINDOWS);
    elapsed_us+=(k_uptime_get_32()-start)*1000;
    dsp_parallel_doa_profile(&active_us,&cic_wait_us);active_total+=active_us;
   }
   if(error){atomic_set(&enabled,0);atomic_set(&valid,0);atomic_set(&busy,0);index=0;break;}
   if(epoch!=(unsigned)atomic_get(&generation) || !atomic_get(&enabled)){index=0;atomic_set(&busy,0);continue;}
   expected=last_seq+1;
   if(++index!=DOA_WINDOWS)continue;
   uint32_t now=k_uptime_get_32(),latency=time_us_32()-end_us;
   key=k_spin_lock(&result_lock);
   if(info.updates && info.epoch==epoch){
    info.interval_ms=now-info.last_ms;info.max_interval_ms=MAX(info.max_interval_ms,info.interval_ms);
   }
   info.result=result;info.updates++;if(!result.rejected)info.accepted++;
   info.active_us=active_total;info.max_active_us=MAX(info.max_active_us,active_total);info.cic_wait_us=cic_wait_us;
   info.last_us=elapsed_us;info.max_us=MAX(info.max_us,elapsed_us);
   info.end_latency_us=latency;info.max_end_latency_us=MAX(info.max_end_latency_us,latency);
   info.last_ms=now;info.epoch=epoch;
   k_spin_unlock(&result_lock,key);
   telemetry_notify();
   if(atomic_get(&enabled)==3 && first==oneshot_first && oneshot_frames==DOA_FRAMES){
    atomic_set(&valid,1);atomic_set(&enabled,0);
   }
   index=0;atomic_set(&busy,0);
  }
 }
}
/* Only bounded ownership/handoff work runs here; bare core 1 does the DSP.
 * A brief priority boost prevents window starvation under continuous capture.
 * DMA interrupts and the USB workqueue still outrank this controller. */
K_THREAD_DEFINE(doa_worker,3072,worker,NULL,NULL,NULL,1,0,0);

static int stats(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);ARG_UNUSED(argv);
 k_spinlock_key_t key=k_spin_lock(&result_lock);__typeof__(info) s=info;k_spin_unlock(&result_lock,key);
 uint32_t age_us=s.updates?(k_uptime_get_32()-s.last_ms)*1000:0;
 shell_print(sh,"DOA enabled=%d busy=%d updates=%u accepted=%u age_ms=%u wall_us=%u max_wall_us=%u memory_bytes=%u",
  (int)atomic_get(&enabled),(int)atomic_get(&busy),s.updates,s.accepted,age_us/1000,
  s.last_us,s.max_us,(unsigned)(sizeof(workspace)+sizeof(snapshot)+sizeof(windows)+3072));
 shell_print(sh,"DOA_TIMING core=1 active_us=%u max_active_us=%u interval_ms=%u max_interval_ms=%u max_cic_wait_us=%u",
  s.active_us,s.max_active_us,s.interval_ms,s.max_interval_ms,s.cic_wait_us);
 shell_print(sh,"DOA_LATENCY window_us=%u end_age_us=%u midpoint_age_us=%u publish_end_us=%u max_publish_end_us=%u",
  (unsigned)(DOA_FRAMES*1000000ull/DOA_RATE),s.updates?s.end_latency_us+age_us:0,
  s.updates?s.end_latency_us+age_us+(unsigned)(DOA_FRAMES*500000ull/DOA_RATE):0,s.end_latency_us,s.max_end_latency_us);
 key=k_spin_lock(&queue_lock);uint32_t dropped=windows.dropped,gaps=windows.gaps;k_spin_unlock(&queue_lock,key);
 shell_print(sh,"DOA_PIPELINE window_buffers=2 buffer_bytes=%u overwritten_windows=%u sequence_resets=%u frame_resets=%d",
  (unsigned)sizeof(windows),dropped,gaps,(int)atomic_get(&frame_resets));
 shell_print(sh,"DOA_RESULT status=%s angle_mdeg=%d reject_mask=%u rms_milli=%u fit_milli_samples=%u max_residual_milli_samples=%u",
  !s.updates?"pending":s.result.rejected?"rejected":"provisional",
  s.updates && !s.result.rejected?(int)(s.result.angle*1000):-1,s.result.rejected,
  (unsigned)(s.result.rms*1000),(unsigned)(s.result.fit_rms*1000),(unsigned)(s.result.max_residual*1000));
 for(unsigned p=0;p<6;p++){
  static const uint8_t pair[6][2]={{1,2},{1,3},{1,4},{2,3},{2,4},{3,4}};
  shell_print(sh,"DOA_PAIR %u %u lag_milli_samples=%d score_milli=%d coherence_milli=%u band_milli=%u",
   pair[p][0],pair[p][1],(int)(s.result.pair[p].lag*1000),(int)(s.result.pair[p].score*1000),
   (unsigned)(s.result.pair[p].coherence*1000),(unsigned)(s.result.pair[p].band_fraction*1000));
 }
 shell_print(sh,"DOA_REJECT bits: 1=low_level 2=clipped 4=poor_band 8=delay_bound 16=bad_fit 32=near_normal 64=bad_vector");
 return 0;
}
int doa_snapshot_json(char *buffer,size_t capacity,unsigned version,uint32_t session)
{
 k_spinlock_key_t key=k_spin_lock(&result_lock);__typeof__(info) s=info;k_spin_unlock(&result_lock,key);
 uint32_t now=k_uptime_get_32(),age=s.updates?now-s.last_ms:0;
 key=k_spin_lock(&queue_lock);uint32_t dropped=windows.dropped,gaps=windows.gaps;k_spin_unlock(&queue_lock,key);
 return snprintk(buffer,capacity,"{\"v\":%u,\"type\":\"telemetry\",\"session\":%u,\"epoch\":%u,\"seq\":%u,\"uptime_ms\":%u,\"enabled\":%d,\"current\":%d,\"age_ms\":%u,\"end_age_us\":%u,\"interval_ms\":%u,\"publish_end_us\":%u,\"angle_mdeg\":%d,\"reject_mask\":%u,\"rms_milli\":[%u,%u,%u,%u],\"lag_milli\":[%d,%d,%d,%d,%d,%d],\"coherence_milli\":[%u,%u,%u,%u,%u,%u],\"fit_milli\":%u,\"dropped\":%u,\"faults\":%u,\"overwritten\":%u,\"resets\":%u}\n",version,session,
  (unsigned)atomic_get(&generation),s.updates,now,(int)atomic_get(&enabled),
  s.updates && s.epoch==(unsigned)atomic_get(&generation),age,s.updates?s.end_latency_us+age*1000:0,
  s.interval_ms,s.end_latency_us,s.updates && !s.result.rejected?(int)(s.result.angle*1000):-1,s.result.rejected,
  (unsigned)(s.result.channel_rms[0]*1000),(unsigned)(s.result.channel_rms[1]*1000),(unsigned)(s.result.channel_rms[2]*1000),(unsigned)(s.result.channel_rms[3]*1000),
  (int)(s.result.pair[0].lag*1000),(int)(s.result.pair[1].lag*1000),(int)(s.result.pair[2].lag*1000),(int)(s.result.pair[3].lag*1000),(int)(s.result.pair[4].lag*1000),(int)(s.result.pair[5].lag*1000),
  (unsigned)(s.result.pair[0].coherence*1000),(unsigned)(s.result.pair[1].coherence*1000),(unsigned)(s.result.pair[2].coherence*1000),(unsigned)(s.result.pair[3].coherence*1000),(unsigned)(s.result.pair[4].coherence*1000),(unsigned)(s.result.pair[5].coherence*1000),
  (unsigned)(s.result.fit_rms*1000),(unsigned)atomic_get(&capture_stats.dropped),(unsigned)atomic_get(&capture_stats.faults),dropped,gaps+(unsigned)atomic_get(&frame_resets));
}
static int telemetry(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);ARG_UNUSED(argv);char buffer[768];
 int length=doa_snapshot_json(buffer,sizeof(buffer),1,0);
 if(length>0 && length<(int)sizeof(buffer)){buffer[length-1]=0;shell_print(sh,"MIC_APP %s",buffer);}
 return 0;
}
void doa_set_enabled(bool on)
{
 atomic_set(&enabled,0);atomic_inc(&generation);atomic_set(&valid,0);atomic_set(&enabled,on?1:0);k_sem_give(&doa_ready);telemetry_notify();
}
static int config(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);
 int mode=!strcmp(argv[1],"on")?1:!strcmp(argv[1],"off")?0:-1;if(mode<0)return -EINVAL;
 doa_set_enabled(mode==1);
 shell_print(sh,"DOA %s: raw pre-DSP channels, provisional angles; overlapped windows, latest complete work, no stale queue",argv[1]);
 return 0;
}
static int capture(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);ARG_UNUSED(argv);
 atomic_set(&enabled,0);atomic_inc(&generation);unsigned epoch=atomic_get(&generation);
 atomic_set(&valid,0);atomic_set(&enabled,2);k_sem_give(&doa_ready);
 uint32_t start=k_uptime_get_32();
 while(k_uptime_get_32()-start<5000){
  k_sleep(K_MSEC(20));
  if(atomic_get(&valid) && !atomic_get(&busy) && !atomic_get(&enabled))return stats(sh,0,NULL);
  if((unsigned)atomic_get(&generation)!=epoch)return -ECANCELED;
 }
 atomic_set(&enabled,0);atomic_inc(&generation);k_sem_give(&doa_ready);shell_error(sh,"DOA capture timed out; check mic stats");return -ETIMEDOUT;
}
static int dump(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);ARG_UNUSED(argv);
 if(usb_audio_active() || atomic_get(&enabled) || atomic_get(&busy) || !atomic_get(&valid)){
  shell_error(sh,"Run doa capture first, with UAC closed, then doa dump");return -EBUSY;
 }
 shell_print(sh,"DOA_PCM_BEGIN rate=48000 channels=4 frames=%u",DOA_FRAMES);
 for(unsigned i=0;i<DOA_FRAMES*4;i+=8){
  char line[33];static const char hex[]="0123456789abcdef";
  for(unsigned j=0;j<8;j++){uint16_t v=snapshot[i+j];for(unsigned k=0;k<4;k++)line[4*j+k]=hex[(v>>(12-4*k))&15];}
  line[32]=0;shell_print(sh,"%s",line);
 }
 shell_print(sh,"DOA_PCM_END");return 0;
}
SHELL_STATIC_SUBCMD_SET_CREATE(commands,
 SHELL_CMD_ARG(config,NULL,"on|off; off at boot",config,2,0),
 SHELL_CMD(stats,NULL,"Provisional direction, rejection flags and elapsed cost",stats),
 SHELL_CMD(telemetry,NULL,"Diagnostic JSON snapshot; app uses dedicated CDC",telemetry),
 SHELL_CMD(capture,NULL,"Estimate one raw frame and stop; frame retained for dump",capture),
 SHELL_CMD(dump,NULL,"Export retained raw frame as PCM hex",dump),SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(doa,&commands,"Optional raw-channel direction estimation",NULL);
