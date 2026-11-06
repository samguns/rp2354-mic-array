#include "pdm_filter.h"
#include "pdm_coefficients.h"
#include <string.h>
#ifdef __ZEPHYR__
#include <zephyr/kernel.h>
#include <hardware/timer.h>
static inline uint32_t profile_cycles(void){return time_us_32()*(CONFIG_SYS_CLOCK_HW_CYCLES_PER_SEC/1000000u);}
#define PDM_HOT __ramfunc
#else
#define PDM_HOT
#endif

static int16_t clip(int32_t value)
{
 if(value>32767)return 32767;
 if(value< -32768)return -32768;
 return (int16_t)value;
}
static __attribute__((always_inline)) inline int16_t halfband_step(
 int16_t *restrict history,unsigned pos,unsigned odd,int16_t input,const int16_t *restrict taps,unsigned count)
{
 /* Split even/odd input phases. The nonzero halfband branch is contiguous,
  * allowing exact paired signed MACs without rounding symmetric sums. */
 unsigned length=count/2;
 int16_t *branch=history+(odd?0:2*length);
 branch[pos]=branch[pos+length]=input;
 if(!odd)return 0;
 int32_t sum=(int32_t)history[2*length+pos+length/2+1]*taps[length];
 const int16_t *samples=history+pos+1;
#if defined(__arm__) && defined(__ARM_FEATURE_DSP)
#define DOT_PAIR(n) do { uint32_t a,b;memcpy(&a,samples+(n),4);memcpy(&b,taps+(n),4); \
 __asm__ volatile("smlad %0, %1, %2, %0" : "+r"(sum) : "r"(a), "r"(b)); } while(0)
#else
#define DOT_PAIR(n) do {sum+=(int32_t)samples[n]*taps[n]+(int32_t)samples[(n)+1]*taps[(n)+1];} while(0)
#endif
 DOT_PAIR(0);DOT_PAIR(2);DOT_PAIR(4);DOT_PAIR(6);
 DOT_PAIR(8);DOT_PAIR(10);DOT_PAIR(12);DOT_PAIR(14);
 if(length==32){
  DOT_PAIR(16);DOT_PAIR(18);DOT_PAIR(20);DOT_PAIR(22);
  DOT_PAIR(24);DOT_PAIR(26);DOT_PAIR(28);DOT_PAIR(30);
 }
#undef DOT_PAIR
 return clip((sum>=0?sum+8192:sum-8192)/16384);
}

/* A CIC4 /16 is exactly the 61-tap convolution of four 16-sample boxes.
 * Eight byte-position tables evaluate it without high-rate integrators. */
static int16_t weights[8][256];
static int weights_ready;
void pdm_filter_init(struct pdm_filter *f)
{
 memset(f,0,sizeof(*f));
 for(unsigned n=0;n<PDM_HALF_TAPS/2;n++)f->half_coeff[n]=pdm_halfband[1+2*n];
 f->half_coeff[PDM_HALF_TAPS/2]=pdm_halfband[PDM_HALF_TAPS/2];
 for(unsigned n=0;n<PDM_FINAL_TAPS/2;n++)f->final_coeff[n]=pdm_final[1+2*n];
 f->final_coeff[PDM_FINAL_TAPS/2]=pdm_final[PDM_FINAL_TAPS/2];
 memcpy(f->comp_coeff,pdm_comp,sizeof(pdm_comp));
 if(weights_ready)return;
 int taps[64]={1};
 for(unsigned stage=0;stage<4;stage++)for(int n=63;n>=0;n--){
  int sum=0;for(int k=0;k<16 && k<=n;k++)sum+=taps[n-k];taps[n]=sum;
 }
 for(unsigned pos=0;pos<8;pos++)for(unsigned b=0;b<256;b++){
  int sum=0;for(unsigned bit=0;bit<8;bit++)sum+=((b&(1u<<bit))?1:-1)*taps[63-(pos*8+bit)];
  weights[pos][b]=sum;
 }
 weights_ready=1;
}
static PDM_HOT __attribute__((noinline)) void cic_process(struct pdm_filter *f,const uint32_t *words,size_t count,unsigned first,unsigned channels)
{
 for(size_t n=0;n<count;n++){
  unsigned pos=f->pdm_pos;
  if(f->valid_bytes<8)f->valid_bytes++;
  for(unsigned c=first;c<first+channels;c++){
   uint32_t b=(words[n]>>c)&0x11111111u;
   b=(b|(b>>3))&0x03030303u;b=(b|(b>>6))&0x000f000fu;b=(b|(b>>12))&255u;
   f->pdm_history[c][pos]=f->pdm_history[c][pos+8]=b;
   if(!(n&1u))continue;
   const uint8_t *h=&f->pdm_history[c][pos+1];
   int32_t v=0;
   if(f->valid_bytes==8){
    v=weights[0][h[0]]+weights[1][h[1]]+weights[2][h[2]]+weights[3][h[3]]+
      weights[4][h[4]]+weights[5][h[5]]+weights[6][h[6]]+weights[7][h[7]];
   }else for(unsigned k=8-f->valid_bytes;k<8;k++)v+=weights[k][h[k]];
   f->intermediate[n/2][c]=clip((v>=0?v+1:v-1)/2);
  }
  f->pdm_pos=(pos+1)&7u;
 }
}
static PDM_HOT __attribute__((optimize("O3"))) void fir_process(struct pdm_filter *f,const int16_t *intermediate,size_t frames,int16_t *pcm,unsigned first,unsigned channels)
{
 /* Four CIC samples produce one PCM frame. Process a whole channel at once
  * so phase branches, repeated state loads and lane indexing leave the hot loop. */
 unsigned final_hp=f->half_pos,final_fp=f->final_pos;
 for(unsigned c=first;c<first+channels;c++){
  unsigned hp=f->half_pos,fp=f->final_pos;
  int16_t *half=f->half[c],*final=f->final[c],*h=f->comp[c];
  const int16_t *input=intermediate+c;
  int16_t *output=pcm+(c-first);
  for(size_t n=0;n<frames/4;n++){
   halfband_step(half,hp,0,input[0],f->half_coeff,PDM_HALF_TAPS);
   int16_t a=halfband_step(half,hp,1,input[4],f->half_coeff,PDM_HALF_TAPS);
   halfband_step(final,fp,0,a,f->final_coeff,PDM_FINAL_TAPS);
   hp=(hp+1)&(PDM_HALF_TAPS/2-1);
   halfband_step(half,hp,0,input[8],f->half_coeff,PDM_HALF_TAPS);
   a=halfband_step(half,hp,1,input[12],f->half_coeff,PDM_HALF_TAPS);
   a=halfband_step(final,fp,1,a,f->final_coeff,PDM_FINAL_TAPS);
   h[0]=h[1];h[1]=h[2];h[2]=a;
   int32_t sum=(int32_t)h[1]*16384+((int32_t)h[0]+h[2]-2*(int32_t)h[1])*f->comp_coeff[0];
   *output=clip((sum>=0?sum+8192:sum-8192)/16384);
   output+=channels;input+=16;
   hp=(hp+1)&(PDM_HALF_TAPS/2-1);fp=(fp+1)&(PDM_FINAL_TAPS/2-1);
  }
  final_hp=hp;final_fp=fp;
 }
 f->half_pos=final_hp;f->final_pos=final_fp;
}

PDM_HOT void pdm_filter_cic(struct pdm_filter *f,const uint32_t *words,size_t count)
{
#ifdef __ZEPHYR__
 uint32_t begin=profile_cycles();
#endif
 cic_process(f,words,count,0,MIC_CHANNELS);
#ifdef __ZEPHYR__
 f->cic_cycles=profile_cycles()-begin;
#endif
}
PDM_HOT void pdm_filter_fir(struct pdm_filter *f,const int16_t *intermediate,size_t frames,int16_t *pcm)
{
#ifdef __ZEPHYR__
 uint32_t begin=profile_cycles();
#endif
 fir_process(f,intermediate,frames,pcm,0,MIC_CHANNELS);
#ifdef __ZEPHYR__
 f->fir_cycles=profile_cycles()-begin;
#endif
}
void pdm_filter_process_channels(struct pdm_filter *f,const uint32_t *words,size_t count,int16_t *pcm,unsigned first,unsigned channels)
{
 while(count){
  size_t chunk=count>1024?1024:count;
  cic_process(f,words,chunk,first,channels);
  fir_process(f,&f->intermediate[0][0],chunk/2,pcm,first,channels);
  pcm+=chunk/PDM_WORDS_PER_FRAME*channels;
  count-=chunk;words+=chunk;
 }
}
void pdm_filter_process(struct pdm_filter *f,const uint32_t *words,size_t count,int16_t *pcm)
{pdm_filter_process_channels(f,words,count,pcm,0,MIC_CHANNELS);}
