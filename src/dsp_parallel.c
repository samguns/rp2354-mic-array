#include "dsp_parallel.h"
#include "capture.h"
#include <zephyr/kernel.h>
#include <hardware/structs/sio.h>
#include <hardware/structs/psm.h>
#include <hardware/timer.h>
#include <hardware/sync.h>
#include <errno.h>
#include <string.h>

/* Core 1 never calls Zephyr, allocates memory, or accesses DMA/USB. It owns
 * the CIC state for all channels. Release/acquire publication protects the input
 * pointer and intermediate output; DMA blocks stay owned until both cores finish. */
static struct {
 struct pdm_filter filter;
 const uint32_t *input;
 uint32_t last_us;
 uint32_t status; /* 0 idle, 1 run, 2 complete, 3 fault, 4 booting */
} remote;
static uint32_t local_last_us;
static struct {
 struct doa_workspace *workspace;
 const int16_t *pcm;
 struct doa_result *result;
 uint32_t state,active_us,cic_us,max_wait_us,window;
} direction_job;
static struct {struct beamformer *beam;const int16_t *input;int16_t *output;uint32_t status,elapsed_us;} beam_job;
int dsp_parallel_beam_wait(uint32_t *elapsed_us)
{
 uint32_t deadline=time_us_32()+20000;
 while(__atomic_load_n(&beam_job.status,__ATOMIC_ACQUIRE)==1){
  if((int32_t)(time_us_32()-deadline)>=0)return -ETIMEDOUT;
  k_yield();
 }
 *elapsed_us=beam_job.elapsed_us;
 return __atomic_load_n(&beam_job.status,__ATOMIC_ACQUIRE)==3?-EIO:0;
}
void dsp_parallel_beam_submit(struct beamformer *state,const int16_t *input,int16_t *output)
{
 beam_job.beam=state;beam_job.input=input;beam_job.output=output;
 __atomic_store_n(&beam_job.status,1,__ATOMIC_RELEASE);__asm__ volatile("sev");
}
static uint32_t park_requested,parked;
int dsp_parallel_park(void){
 __atomic_store_n(&park_requested,1,__ATOMIC_RELEASE);__asm__ volatile("sev");
 uint32_t end=time_us_32()+100000;
 while(!__atomic_load_n(&parked,__ATOMIC_ACQUIRE)){if((int32_t)(time_us_32()-end)>=0)return -ETIMEDOUT;k_yield();}
 return 0;
}
static uint32_t submitted_us;
static struct capture_block *pending;
static uint32_t stack[512] __aligned(8);
static uint32_t vectors[16] __aligned(128);

static void worker_fault(void)
{
 __atomic_store_n(&beam_job.status,3,__ATOMIC_RELEASE);
 __atomic_store_n(&remote.status,3,__ATOMIC_RELEASE);
 __atomic_store_n(&direction_job.state,3,__ATOMIC_RELEASE);
 __asm__ volatile("sev");
 for(;;)__asm__ volatile("wfe");
}
__ramfunc void dsp_parallel_doa_poll(void)
{
 if(__atomic_load_n(&park_requested,__ATOMIC_ACQUIRE)){
  __atomic_store_n(&parked,1,__ATOMIC_RELEASE);for(;;)__asm__ volatile("wfe");
 }
 if(__atomic_load_n(&remote.status,__ATOMIC_ACQUIRE)==1){
  uint32_t start=time_us_32();
  uint32_t wait=start-submitted_us;
  if(wait>__atomic_load_n(&direction_job.max_wait_us,__ATOMIC_RELAXED))
   __atomic_store_n(&direction_job.max_wait_us,wait,__ATOMIC_RELEASE);
  pdm_filter_cic(&remote.filter,remote.input,CAPTURE_WORDS);
  uint32_t elapsed=time_us_32()-start;
  __atomic_store_n(&remote.last_us,elapsed,__ATOMIC_RELEASE);
  direction_job.cic_us+=elapsed;
  __atomic_store_n(&remote.status,2,__ATOMIC_RELEASE);
  __asm__ volatile("sev");
 }
 if(__atomic_load_n(&beam_job.status,__ATOMIC_ACQUIRE)==1){
  uint32_t start=time_us_32();beamform_process(beam_job.beam,beam_job.input,beam_job.output,128);
  beam_job.elapsed_us=time_us_32()-start;direction_job.cic_us+=beam_job.elapsed_us;
  __atomic_store_n(&beam_job.status,2,__ATOMIC_RELEASE);__asm__ volatile("sev");
 }
}
static __ramfunc void worker(void)
{
 __asm__ volatile("cpsid i");
 /* Core 1 runs bare scalar DSP: enable CP10/CP11 before any float work. */
 *(volatile uint32_t *)0xe000ed88u|=0xfu<<20;
 __asm__ volatile("dsb\n\tisb" ::: "memory");
 __atomic_store_n(&remote.status,0,__ATOMIC_RELEASE);
 for(;;){
  dsp_parallel_doa_poll();
  if(__atomic_load_n(&direction_job.state,__ATOMIC_ACQUIRE)==1){
   direction_job.cic_us=0;uint32_t start=time_us_32();
   if(direction_job.window==DOA_WINDOWS)doa_finish(direction_job.workspace,direction_job.result);
   else {
    if(!direction_job.window)doa_begin(direction_job.workspace,direction_job.result);
    doa_add_window(direction_job.workspace,direction_job.pcm,direction_job.result);
   }
   __atomic_store_n(&direction_job.active_us,time_us_32()-start-direction_job.cic_us,__ATOMIC_RELEASE);
   __atomic_store_n(&direction_job.state,2,__ATOMIC_RELEASE);
   __asm__ volatile("sev");
  } else __asm__ volatile("wfe");
 }
}
int dsp_parallel_doa_window(struct doa_workspace *workspace,const int16_t *pcm,struct doa_result *result,unsigned window)
{
 if(__atomic_load_n(&remote.status,__ATOMIC_ACQUIRE)==3)return -EIO;
 direction_job.workspace=workspace;direction_job.pcm=pcm;direction_job.result=result;
 direction_job.window=window;
 __atomic_store_n(&direction_job.state,1,__ATOMIC_RELEASE);__asm__ volatile("sev");
 while(__atomic_load_n(&direction_job.state,__ATOMIC_ACQUIRE)==1)k_sleep(K_MSEC(2));
 return __atomic_load_n(&direction_job.state,__ATOMIC_ACQUIRE)==2?0:-EIO;
}
void dsp_parallel_doa_profile(uint32_t *active_us,uint32_t *max_cic_wait_us)
{
 *active_us=__atomic_load_n(&direction_job.active_us,__ATOMIC_ACQUIRE);
 *max_cic_wait_us=__atomic_load_n(&direction_job.max_wait_us,__ATOMIC_ACQUIRE);
}
static bool wait_fifo(uint32_t mask,uint32_t deadline)
{
 while(!(sio_hw->fifo_st&mask))if((int32_t)(time_us_32()-deadline)>=0)return false;
 return true;
}
int dsp_parallel_init(void)
{
 pdm_filter_init(&remote.filter);
 for(unsigned n=0;n<16;n++)vectors[n]=(uintptr_t)worker_fault;
 vectors[0]=(uintptr_t)(stack+ARRAY_SIZE(stack));vectors[1]=(uintptr_t)worker;
 remote.status=4;
 __atomic_thread_fence(__ATOMIC_RELEASE);
 uint32_t deadline=time_us_32()+500000;
 hw_set_bits(&psm_hw->frce_off,PSM_FRCE_OFF_PROC1_BITS);
 while(!(psm_hw->frce_off&PSM_FRCE_OFF_PROC1_BITS)){
  if((int32_t)(time_us_32()-deadline)>=0)return -ETIMEDOUT;
 }
 hw_clear_bits(&psm_hw->frce_off,PSM_FRCE_OFF_PROC1_BITS);
 if(!wait_fifo(SIO_FIFO_ST_VLD_BITS,deadline))return -ETIMEDOUT;
 (void)sio_hw->fifo_rd;
 /* RP2350 ROM core-launch handshake, as in Pico SDK multicore_launch_core1_raw. */
 uint32_t commands[]={0,0,1,(uintptr_t)vectors,(uintptr_t)(stack+ARRAY_SIZE(stack)),(uintptr_t)worker};
 unsigned n=0;
 while(n<ARRAY_SIZE(commands)){
  uint32_t cmd=commands[n];
  if(!cmd){while(sio_hw->fifo_st&SIO_FIFO_ST_VLD_BITS)(void)sio_hw->fifo_rd;__asm__ volatile("sev");}
  if(!wait_fifo(SIO_FIFO_ST_RDY_BITS,deadline))return -ETIMEDOUT;
  sio_hw->fifo_wr=cmd;__asm__ volatile("sev");
  if(!wait_fifo(SIO_FIFO_ST_VLD_BITS,deadline))return -ETIMEDOUT;
  n=sio_hw->fifo_rd==cmd?n+1:0;
 }
 while(__atomic_load_n(&remote.status,__ATOMIC_ACQUIRE)==4){
  if((int32_t)(time_us_32()-deadline)>=0)return -ETIMEDOUT;
 }
 return remote.status==3?-EIO:0;
}
static void submit(struct capture_block *block)
{
 remote.input=block->words;
 submitted_us=time_us_32();
 __atomic_store_n(&remote.status,1,__ATOMIC_RELEASE);__asm__ volatile("sev");
}
__ramfunc __attribute__((optimize("O3"))) int dsp_parallel_process(struct pdm_filter *local,struct capture_block *incoming,int16_t *pcm,struct capture_block **completed)
{
 *completed=NULL;
 if(__atomic_load_n(&remote.status,__ATOMIC_ACQUIRE)==3)return -EIO;
 if(!pending){pending=incoming;submit(incoming);return 0;}
 uint32_t deadline=time_us_32()+20000;
 while(__atomic_load_n(&remote.status,__ATOMIC_ACQUIRE)==1){
  if((int32_t)(time_us_32()-deadline)>=0)return -ETIMEDOUT;
  k_yield();
 }
 if(__atomic_load_n(&remote.status,__ATOMIC_ACQUIRE)!=2)return -EIO;
 /* Copy the completed CIC buffer before publishing the next job. */
 memcpy(local->intermediate,remote.filter.intermediate,sizeof(local->intermediate));
 *completed=pending;pending=incoming;submit(incoming);
 uint32_t start=time_us_32();
 pdm_filter_fir(local,&local->intermediate[0][0],CAPTURE_WORDS/2,pcm);
 __atomic_store_n(&local_last_us,time_us_32()-start,__ATOMIC_RELEASE);
 return 0;
}

void dsp_parallel_profile(uint32_t *local_us,uint32_t *remote_us,uint32_t *status)
{
 *local_us=__atomic_load_n(&local_last_us,__ATOMIC_ACQUIRE);
 *remote_us=__atomic_load_n(&remote.last_us,__ATOMIC_ACQUIRE);
 *status=__atomic_load_n(&remote.status,__ATOMIC_ACQUIRE);
}
