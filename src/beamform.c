#include "beamform.h"
#include <math.h>
#include <string.h>
#ifdef __ZEPHYR__
#include <zephyr/linker/section_tags.h>
#define BF_HOT __ramfunc __attribute__((optimize("O3")))
#else
#define BF_HOT
#endif

#define PI 3.14159265358979323846f
int beamform_prepare(struct beam_bank *b,int mode,unsigned azimuth,unsigned elevation)
{
 if(mode<BF_OFF || mode>BF_MANUAL || azimuth>=3600 || elevation>900)return -1;
 memset(b,0,sizeof(*b));b->mode=mode;
 b->azimuth_tdeg=mode==BF_MANUAL?azimuth:0;b->elevation_tdeg=mode==BF_MANUAL?elevation:900;
 float ux=0,uy=0;
 if(mode==BF_MANUAL && elevation<900){
  float scale=.025f*48000.f/343.f*cosf(elevation*PI/1800.f);
  ux=scale*cosf(azimuth*PI/1800.f);uy=scale*sinf(azimuth*PI/1800.f);
 }
 const float projection[4]={uy,-ux,-uy,ux};
 for(unsigned c=0;c<4;c++){
  float delay=BF_COMMON_DELAY+projection[c];
  if(fabsf(delay-BF_COMMON_DELAY)<.00001f)delay=BF_COMMON_DELAY;
  b->delay[c]=mode==BF_OFF?0:delay;
  unsigned whole=(unsigned)floorf(delay);float fraction=delay-whole;
  b->integral[c]=whole-BF_TAPS/2;
  if(fraction<.00001f){b->coefficient[c][BF_TAPS/2]=16384;continue;}
  float sum=0,coeff[BF_TAPS];
  for(unsigned k=0;k<BF_TAPS;k++){
   float x=(float)k-BF_TAPS/2-fraction;
   float sinc=fabsf(x)<.00001f?1:sinf(PI*x)/(PI*x);
   float window=.54f-.46f*cosf(2*PI*k/(BF_TAPS-1));
   coeff[k]=sinc*window;sum+=coeff[k];
  }
  int32_t total=0;
  for(unsigned k=0;k<BF_TAPS;k++){b->coefficient[c][k]=(int32_t)lroundf(coeff[k]/sum*16384.f);total+=b->coefficient[c][k];}
  b->coefficient[c][BF_TAPS/2]+=16384-total;
  for(unsigned k=0;k<BF_TAPS/2;k++){int16_t t=b->coefficient[c][k];b->coefficient[c][k]=b->coefficient[c][BF_TAPS-1-k];b->coefficient[c][BF_TAPS-1-k]=t;}
 }
 return 0;
}
void beamform_init(struct beamformer *s){memset(s,0,sizeof(*s));}
void beamform_target(struct beamformer *s,const struct beam_bank *b)
{s->target=*b;s->fade_left=BF_FADE_FRAMES;}
static BF_HOT float render(const struct beamformer *s,const struct beam_bank *b,unsigned channel)
{
 if(b->mode==BF_OFF)return s->history[channel][s->position];
 float sum=0;
 if(b->mode==BF_BROADSIDE || b->elevation_tdeg==900){
  for(unsigned c=0;c<4;c++)sum+=s->history[c][(s->position-BF_COMMON_DELAY)&(BF_HISTORY-1)];
 }else{
  int64_t accumulator=0;
  for(unsigned c=0;c<4;c++){
   const int16_t *samples=s->history[c]+s->position+BF_HISTORY-b->integral[c]-(BF_TAPS-1);
#if defined(__arm__) && defined(__ARM_FEATURE_DSP)
   const int16_t *coeff=b->coefficient[c];unsigned count=(BF_TAPS-1)/2;uint32_t x,y;
   __asm__ volatile("1: ldr %2, [%4], #4\n"
                    "ldr %3, [%5], #4\n"
                    "smlald %Q0, %R0, %2, %3\n"
                    "subs %1, %1, #1\n"
                    "bne 1b"
                    : "+&r"(accumulator), "+&r"(count), "=&r"(x), "=&r"(y), "+&r"(samples), "+&r"(coeff)
                    : : "cc", "memory");
   accumulator+=(int32_t)*samples**coeff;
#else
   for(unsigned k=0;k<BF_TAPS;k++)accumulator+=(int32_t)samples[k]*b->coefficient[c][k];
#endif
  }
  return (float)(int32_t)(accumulator>>2)*(1.f/16384.f);
 }
 return sum*.25f;
}
static int16_t rounded(float value)
{if(value>=32767)return 32767;if(value<=-32768)return -32768;return (int16_t)(value+(value>=0?.5f:-.5f));}
BF_HOT void beamform_process(struct beamformer *s,const int16_t *input,int16_t *output,size_t frames)
{
 for(size_t n=0;n<frames;n++){
  for(unsigned c=0;c<4;c++){s->history[c][s->position]=input[4*n+c];s->history[c][s->position+BF_HISTORY]=input[4*n+c];}
  float old=0,next=0;
  if(s->current.mode!=BF_OFF)old=render(s,&s->current,0);
  if(s->fade_left && s->target.mode!=BF_OFF)next=render(s,&s->target,0);
  if(!s->fade_left){
   if(s->current.mode==BF_OFF){for(unsigned c=0;c<4;c++)output[4*n+c]=input[4*n+c];}
   else {int16_t value=rounded(old);for(unsigned c=0;c<4;c++)output[4*n+c]=value;}
   s->position=(s->position+1)&(BF_HISTORY-1);continue;
  }
  float alpha=s->fade_left?(float)(BF_FADE_FRAMES-s->fade_left+1)/BF_FADE_FRAMES:0;
  for(unsigned c=0;c<4;c++){
   float a=s->current.mode==BF_OFF?s->history[c][s->position]:old;
   float b=s->target.mode==BF_OFF?s->history[c][s->position]:next;
   output[4*n+c]=rounded(s->fade_left?a+(b-a)*alpha:a);
  }
  if(s->fade_left && !--s->fade_left)s->current=s->target;
  s->position=(s->position+1)&(BF_HISTORY-1);
 }
}

BF_HOT void beamform_broadside(const int16_t *input, int16_t *output, size_t frames)
{
 for (size_t n=0;n<frames;n++) {
  int32_t sum=0;
  for(int c=0;c<4;c++)sum+=input[4*n+c];
  /* Round ties away from zero. A four-input average cannot overflow PCM16. */
  int16_t value=(sum>=0?(sum+2)/4:(sum-2)/4);
  for(int c=0;c<4;c++)output[4*n+c]=value;
 }
}
