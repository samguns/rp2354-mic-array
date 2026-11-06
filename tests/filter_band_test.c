#include "pdm_filter.h"
#include <math.h>
#include <assert.h>
#include <stdio.h>
#undef assert
#define assert(x) do { if(!(x)) { fprintf(stderr,"FAIL: %s\n",#x); return 1; } } while(0)
static struct pdm_filter f;
static uint32_t words[PDM_WORDS_PER_FRAME*128];
static int16_t pcm[128*4];
int main(void)
{
 setbuf(stdout,NULL);
 const unsigned frequencies[]={1000,10000,20000,28000,44000,76000,172000};
 for(unsigned trial=0;trial<sizeof(frequencies)/sizeof(frequencies[0]);trial++){
  unsigned frequency=frequencies[trial],tick=0,frames=0,count=0;
  double error=0,re=0,im=0;uint32_t random=42;unsigned alias=frequency%48000;if(alias>24000)alias=48000-alias;
  pdm_filter_init(&f);
  for(unsigned block=0;block<100;block++){
   for(unsigned n=0;n<sizeof(words)/sizeof(words[0]);n++){
    words[n]=0;
    for(unsigned bit=0;bit<8;bit++,tick++){
     random^=random<<13;random^=random>>17;random^=random<<5;
     error+=.3*sin(6.283185307179586*frequency*tick/3072000.0)+.1*((random&65535)/32768.0-1);
     unsigned high=error>=0;error-=high?1:-1;
     unsigned nibble=high|(high<<1)|((tick&1)<<2)|((!high)<<3);
     words[n]|=nibble<<(bit*4);
    }
   }
   pdm_filter_process(&f,words,sizeof(words)/sizeof(words[0]),pcm);
   for(unsigned n=0;n<128;n++,frames++){
    assert(pcm[4*n]==pcm[4*n+1]);
    if(frames<256)continue;
    assert(pcm[4*n+2]==0);
    double phase=6.283185307179586*alias*frames/48000.0;
    re+=pcm[4*n]*cos(phase);im+=pcm[4*n]*sin(phase);count++;
   }
  }
  double amplitude=2*hypot(re,im)/count;
  printf("%u Hz input, %u Hz output bin: %.3f peak PCM\n",frequency,alias,amplitude);
  if(frequency<=20000)assert(amplitude>9000 && amplitude<10500);
  else assert(amplitude<12);
 }
 puts("PASS: full pipeline 1/10/20 kHz amplitude, >58 dB selected alias rejection (dithered synthetic source) and channel synchronization");
}
