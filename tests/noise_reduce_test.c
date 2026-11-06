#include "noise_reduce.h"
#define CHECK(x) do { if(!(x)){fprintf(stderr,"FAIL %s at %d\n",#x,__LINE__);return 1;} } while(0)
#include <math.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>

static struct nr_state state, reference;
static int16_t input[512],output[128],plain[128],previous[128];
static uint32_t random_state=42;
static float noise(void){random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;return ((int32_t)(random_state&65535)-32768)/32768.0f*200;}
static void fill(unsigned start,float frequency,float amplitude,int noisy)
{
 for(int n=0;n<128;n++){int16_t x=lrintf(amplitude*sinf(6.28318530718f*frequency*(start+n)/NR_SAMPLE_RATE)+(noisy?noise():0));
  for(int c=0;c<4;c++)input[n*4+c]=x;}
}
int main(void)
{
 nr_init(&state,0,0,2);for(int i=0;i<512;i++)input[i]=i-256;nr_process(&state,input,output);
 for(int n=0;n<128;n++)CHECK(output[n]==input[4*n+2]);
 nr_init(&state,2,0,0);nr_init(&reference,1,0,0);reference.hp_alpha=state.hp_alpha;
 int max_error=0;
 for(int b=0;b<30;b++){fill(b*128,1000,1000,0);nr_process(&state,input,output);nr_process(&reference,input,plain);
  if(b>2)for(int n=0;n<128;n++){int error=abs(output[n]-previous[n]);if(error>max_error)max_error=error;}
  for(int n=0;n<128;n++)previous[n]=plain[n];}
 CHECK(max_error<=2);printf("Unlearned STFT reconstruction max PCM error: %d\n",max_error);
 nr_init(&state,1,50,0);double notch=0;
 for(int b=0;b<900;b++){fill(b*128,50,1000,0);nr_process(&state,input,output);if(b>800)for(int n=0;n<128;n++)notch+=(double)output[n]*output[n];}
 double notch_rms=sqrt(notch/(99*128));CHECK(notch_rms<10);printf("50 Hz notch output RMS for 1000-peak input: %.3f\n",notch_rms);
 nr_init(&state,2,0,0);nr_init(&reference,1,0,0);nr_learn(&state);
 for(unsigned b=0;b<NR_LEARN_BLOCKS;b++){fill(b*128,0,0,1);nr_process(&state,input,output);}
 CHECK(state.learned && !state.learn_left);
 double reduced=0,baseline=0;
 for(int b=0;b<300;b++){fill(b*128,0,0,1);nr_process(&state,input,output);nr_process(&reference,input,plain);
  if(b>30)for(int n=0;n<128;n++){reduced+=(double)output[n]*output[n];baseline+=(double)plain[n]*plain[n];}}
 double attenuation=10*log10(reduced/baseline);printf("Stationary noise change: %.3f dB\n",attenuation);CHECK(attenuation<-5);
 double re=0,im=0;int count=0;
 for(int b=0;b<300;b++){fill(b*128,1000,1000,1);nr_process(&state,input,output);
  if(b>30)for(int n=0;n<128;n++){double phase=6.28318530718*1000*(b*128+n-128)/NR_SAMPLE_RATE;re+=output[n]*cos(phase);im+=output[n]*sin(phase);count++;}}
 double amplitude=2*hypot(re,im)/count;printf("1000 Hz tone retained with noise: %.3f PCM peak\n",amplitude);CHECK(amplitude>900 && amplitude<1050);
 printf("DSP state bytes: %u\n",(unsigned)sizeof(state));puts("PASS noise reduction");return 0;
}
