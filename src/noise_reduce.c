#include "noise_reduce.h"
#include <math.h>
#include <string.h>

#define PI 3.14159265358979323846f
#ifdef __ZEPHYR__
#include <zephyr/linker/section_tags.h>
#define NR_HOT __ramfunc __attribute__((optimize("O3")))
#else
#define NR_HOT
#endif
void nr_init(struct nr_state *s, int mode, int hum, int channel)
{
 nr_init_rate(s,mode,hum,channel,NR_SAMPLE_RATE);
}
void nr_init_rate(struct nr_state *s, int mode, int hum, int channel, unsigned rate)
{
 memset(s,0,sizeof(*s)); s->mode=mode; s->hum=hum; s->channel=channel;
 s->rate=rate;s->learn_blocks=(2u*rate+NR_HOP-1u)/NR_HOP;
 s->hp_alpha=expf(-2*PI*(mode==2?20:80)/rate);
 for (int n=0;n<256;n++) s->window[n]=sinf(PI*(n+.5f)/256);
 for (int n=0;n<128;n++) { s->tw_re[n]=cosf(2*PI*n/256); s->tw_im[n]=-sinf(2*PI*n/256); }
 for (int n=0;n<129;n++) s->gain[n]=1;
 if (hum) {
  float c=cosf(2*PI*hum/rate),alpha=sinf(2*PI*hum/rate)/(2*20);
  s->nb0=1/(1+alpha);s->nb1=-2*c/(1+alpha);s->nb2=s->nb0;
  s->na1=s->nb1;s->na2=(1-alpha)/(1+alpha);
 }
}
void nr_learn(struct nr_state *s)
{
 memset(s->noise,0,sizeof(s->noise));s->learn_left=s->learn_blocks;s->learned=0;
}
static NR_HOT void transform(struct nr_state *s, int inverse)
{
 for (unsigned i=1,j=0;i<128;i++) {
  unsigned bit=64;while(j&bit){j^=bit;bit>>=1;}j^=bit;
  if(i<j){float t=s->re[i];s->re[i]=s->re[j];s->re[j]=t;t=s->im[i];s->im[i]=s->im[j];s->im[j]=t;}
 }
 for(unsigned len=2;len<=128;len*=2) {
  unsigned stride=256/len;
  for(unsigned start=0;start<128;start+=len)
  for(unsigned k=0;k<len/2;k++) {
   unsigned a=start+k,b=a+len/2,t=k*stride;
   float wr=s->tw_re[t],wi=inverse?-s->tw_im[t]:s->tw_im[t];
   float r=wr*s->re[b]-wi*s->im[b],im=wr*s->im[b]+wi*s->re[b];
   s->re[b]=s->re[a]-r;s->im[b]=s->im[a]-im;s->re[a]+=r;s->im[a]+=im;
  }
 }
 if(inverse)for(int n=0;n<128;n++){s->re[n]/=128;s->im[n]/=128;}
}
static int16_t quantize(float value)
{
 if(value>32767)return 32767;
 if(value<-32768)return -32768;
#if defined(__arm__) && defined(__ARM_FP)
 int32_t rounded;
 __asm__("vcvtr.s32.f32 s15, %1\n\tvmov %0, s15" : "=r"(rounded) : "t"(value) : "s15");
 return (int16_t)rounded;
#else
 return (int16_t)lrintf(value);
#endif
}
NR_HOT void nr_process(struct nr_state *s, const int16_t *four, int16_t *mono)
{
 for(int n=0;n<128;n++) {
  float x=four[4*n+s->channel];
  if(s->mode) {
   /* Suppress: 20 Hz low cut; filter-only: 80 Hz. No AGC. */
   float y=s->hp_alpha*(s->hp_y+x-s->hp_x);s->hp_x=x;s->hp_y=y;x=y;
   if(s->hum){y=s->nb0*x+s->nb1*s->nx1+s->nb2*s->nx2-s->na1*s->ny1-s->na2*s->ny2;
    s->nx2=s->nx1;s->nx1=x;s->ny2=s->ny1;s->ny1=y;x=y;}
  }
  if(s->mode==2) {
   float old=s->history[n]*s->window[n],next=x*s->window[n+128];
   if(n&1){s->im[n/2]=old;s->im[64+n/2]=next;}
   else {s->re[n/2]=old;s->re[64+n/2]=next;}
   s->history[n]=x;
  } else mono[n]=quantize(x);
 }
 if(s->mode!=2)return;
 /* A real 256-point FFT via one packed 128-point complex transform. */
 transform(s,0);
 for(int k=0;k<=128;k++) {
  int a=k&127,b=(128-k)&127;
  float er=.5f*(s->re[a]+s->re[b]),ei=.5f*(s->im[a]-s->im[b]);
  float or=.5f*(s->im[a]+s->im[b]),oi=.5f*(s->re[b]-s->re[a]);
  float wr=k==128?-1:s->tw_re[k],wi=k==128?0:s->tw_im[k];
  s->spectrum_re[k]=er+wr*or-wi*oi;
  s->spectrum_im[k]=ei+wr*oi+wi*or;
 }
 for(int k=0;k<=128;k++) {
  float power=s->spectrum_re[k]*s->spectrum_re[k]+s->spectrum_im[k]*s->spectrum_im[k];
  if(s->learn_left)s->noise[k]+=power/s->learn_blocks;
  float target=1;
  if(s->learned){float ratio=1-1.5f*s->noise[k]/(power+1e-12f);target=sqrtf(ratio>.015848932f?ratio:.015848932f);}
  /* Approximately 36 ms gain smoothing at 48 kHz. */
  const float smoothing=.928317767f;
  s->gain[k]=smoothing*s->gain[k]+(1-smoothing)*target;
  s->spectrum_re[k]*=s->gain[k];s->spectrum_im[k]*=s->gain[k];
 }
 if(s->learn_left && !--s->learn_left)s->learned=1;
 /* Reconstruct the packed complex spectrum from real positive bins. */
 for(int k=0;k<128;k++) {
  float ar=s->spectrum_re[k],ai=s->spectrum_im[k];
  float br=s->spectrum_re[128-k],bi=s->spectrum_im[128-k];
  float er=.5f*(ar+br),ei=.5f*(ai-bi),dr=.5f*(ar-br),di=.5f*(ai+bi);
  float wr=s->tw_re[k],wi=s->tw_im[k];
  float or=dr*wr+di*wi,oi=di*wr-dr*wi;
  s->re[k]=er-oi;s->im[k]=ei+or;
 }
 transform(s,1);
 for(int n=0;n<128;n++) {
  float first=(n&1)?s->im[n/2]:s->re[n/2];
  float second=(n&1)?s->im[64+n/2]:s->re[64+n/2];
  mono[n]=quantize(s->overlap[n]+first*s->window[n]);
  s->overlap[n]=second*s->window[n+128];
 }
}
