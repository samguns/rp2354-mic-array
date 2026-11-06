#include <pico/stdlib.h>
#include <pico/bootrom.h>
#include <pico/stdio_usb.h>
#include <hardware/watchdog.h>
#include <hardware/clocks.h>
#include <stdio.h>
extern "C" {
#include "noise_reduce.h"
#include "pdm_filter.h"
}
static nr_state dsp;
static pdm_filter pdm;
static int16_t samples[512],mono[128];
static uint32_t words[PDM_WORDS_PER_FRAME*128];
int main()
{
 if(watchdog_caused_reboot())reset_usb_boot(0,0);
 set_sys_clock_khz(150000,true);stdio_init_all();watchdog_enable(8000,true);
 bool connected=false;uint64_t boot_time=time_us_64();
 for(unsigned i=0;i<sizeof(words)/sizeof(words[0]);i++)words[i]=i*2654435761u;
 for(int i=0;i<512;i++)samples[i]=(i*193)%2000-1000;
 while(true){
  watchdog_update();connected=connected||stdio_usb_connected();
  if(!connected && time_us_64()-boot_time>30000000)reset_usb_boot(0,0);
  int cmd=getchar_timeout_us(100000);
  if(cmd=='b')reset_usb_boot(0,0);
  if(cmd!='r')continue;
  pdm_filter_init(&pdm);
  uint64_t start=time_us_64();
  for(int n=0;n<1000;n++)pdm_filter_process(&pdm,words,sizeof(words)/sizeof(words[0]),samples);
  printf("SDK_PDM blocks=1000 total_us=%llu\n",(unsigned long long)(time_us_64()-start));
  watchdog_update();
  for(int mode=1;mode<=2;mode++){
   nr_init(&dsp,mode,50,0);if(mode==2){nr_learn(&dsp);for(int n=0;n<250;n++)nr_process(&dsp,samples,mono);}
   start=time_us_64();
   for(int n=0;n<1000;n++)nr_process(&dsp,samples,mono);
   printf("SDK_NR mode=%d blocks=1000 total_us=%llu state_bytes=%u\n",mode,(unsigned long long)(time_us_64()-start),(unsigned)sizeof(dsp));
  }
  printf("SDK_DONE clock_hz=%u checksum=%d\n",clock_get_hz(clk_sys),mono[12]);
 }
}
