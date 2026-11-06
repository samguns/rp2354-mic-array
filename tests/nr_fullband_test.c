#include "noise_reduce.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %s at %d\n",#x,__LINE__);return 1;}}while(0)
static struct nr_state s;
static int16_t in[512],out[128],previous[128];
static unsigned random_state=42;
static int random_sample(void){random_state=random_state*1664525u+1013904223u;return (int)(random_state>>16)-32768;}
int main(void)
{
 nr_init(&s,2,0,2);float hp_x=0,hp_y=0;int max_error=0;
 for(unsigned b=0;b<400;b++){
  int16_t reference[128];
  for(unsigned n=0;n<128;n++){
   for(unsigned c=0;c<4;c++)in[4*n+c]=random_sample()/3;
   float x=in[4*n+2],y=s.hp_alpha*(hp_y+x-hp_x);hp_x=x;hp_y=y;reference[n]=lrintf(y);
  }
  nr_process(&s,in,out);
  if(b>0)for(unsigned n=0;n<128;n++){
   int error=abs(out[n]-previous[n]);if(error>max_error)max_error=error;
  }
  for(unsigned n=0;n<128;n++)previous[n]=reference[n];
 }
 CHECK(max_error<=2);printf("Random full-band overlap reconstruction max error %d PCM\n",max_error);
 const double frequencies[]={20,80,1000,8000,12000,20000};
 for(unsigned f=0;f<sizeof(frequencies)/sizeof(frequencies[0]);f++){
  nr_init(&s,2,0,2);double re=0,im=0;unsigned count=0;
  for(unsigned b=0;b<900;b++){
   for(unsigned n=0;n<128;n++){
    for(unsigned c=0;c<4;c++)in[4*n+c]=0;
    in[4*n+2]=lrint(1000*sin(6.283185307179586*frequencies[f]*(b*128+n)/48000));
   }
   nr_process(&s,in,out);
   if(b>400)for(unsigned n=0;n<128;n++){
    double phase=6.283185307179586*frequencies[f]*(b*128+n-128)/48000;
    re+=out[n]*cos(phase);im+=out[n]*sin(phase);count++;
   }
  }
  double amplitude=2*hypot(re,im)/count;
  printf("Unlearned %.0f Hz retained %.3f peak\n",frequencies[f],amplitude);
  CHECK(amplitude>(f==0?680:f==1?950:980) && amplitude<1010);
 }
 nr_init(&s,2,0,0);nr_learn(&s);
 for(unsigned b=0;b<750;b++){
  for(unsigned n=0;n<128;n++)for(unsigned c=0;c<4;c++)in[4*n+c]=lrint(500*sin(6.283185307179586*12000*(b*128+n)/48000));
  nr_process(&s,in,out);
  if(b==748)CHECK(s.learn_left==1 && !s.learned);
 }
 CHECK(s.learned && s.learn_left==0);
 double re12=0,im12=0,re20=0,im20=0;unsigned count=0;
 for(unsigned b=0;b<600;b++){
  for(unsigned n=0;n<128;n++){
   double t=(b*128+n)/48000.0;
   for(unsigned c=0;c<4;c++)in[4*n+c]=lrint(500*sin(6.283185307179586*12000*t)+1000*sin(6.283185307179586*20000*t));
  }
  nr_process(&s,in,out);
  if(b>100)for(unsigned n=0;n<128;n++){
   double t=(b*128+n-128)/48000.0;
   re12+=out[n]*cos(6.283185307179586*12000*t);im12+=out[n]*sin(6.283185307179586*12000*t);
   re20+=out[n]*cos(6.283185307179586*20000*t);im20+=out[n]*sin(6.283185307179586*20000*t);count++;
  }
 }
 double a12=2*hypot(re12,im12)/count,a20=2*hypot(re20,im20)/count;
 printf("Learned 12 kHz noise %.3f peak, desired 20 kHz tone %.3f peak\n",a12,a20);
 CHECK(a12<90 && a20>950 && a20<1010);
 puts("PASS full-band suppression and exact two-second learning");return 0;
}
