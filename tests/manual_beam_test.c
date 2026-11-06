#include "beamform.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#define PI 3.14159265358979323846
static struct beamformer beam;
static struct beam_bank bank;
static int16_t input[512],output[512];
static double amplitude(unsigned az,unsigned el,unsigned steering,unsigned steering_el,double frequency)
{
 beamform_init(&beam);assert(!beamform_prepare(&bank,BF_MANUAL,steering,steering_el));beamform_target(&beam,&bank);
 double scale=.025*48000/343*cos(el*PI/1800),ux=scale*cos(az*PI/1800),uy=scale*sin(az*PI/1800);
 double offset[4]={uy,-ux,-uy,ux},energy=0,reference=0;
 for(unsigned start=0;start<8192;start+=128){
  for(unsigned n=0;n<128;n++)for(unsigned c=0;c<4;c++)input[4*n+c]=(int16_t)lround(10000*sin(2*PI*frequency*(start+n+offset[c])/48000));
  beamform_process(&beam,input,output,128);
  if(start>=1024)for(unsigned n=0;n<128;n++){
   for(unsigned c=1;c<4;c++)assert(output[4*n+c]==output[4*n]);
   double expected=10000*sin(2*PI*frequency*((double)start+n-BF_COMMON_DELAY)/48000);
   energy+=(double)output[4*n]*output[4*n];reference+=expected*expected;
  }
 }
 return sqrt(energy/reference);
}
int main(void)
{
 assert(beamform_prepare(&bank,2,3600,0));assert(beamform_prepare(&bank,2,0,901));
 assert(!beamform_prepare(&bank,2,0,0));
 assert(bank.delay[3]>bank.delay[1]);assert(fabs(bank.delay[0]-14)<.0001);
 assert(fabs(bank.delay[3]-17.498542)<.001);assert(fabs(bank.delay[1]-10.501458)<.001);
 /* Q14 four-channel accumulation stays below INT32_MAX even at full scale. */
 for(unsigned a=0;a<3600;a+=10)for(unsigned e=0;e<=900;e+=10){
  assert(!beamform_prepare(&bank,2,a,e));long total=0;
  for(unsigned c=0;c<4;c++)for(unsigned k=0;k<BF_TAPS;k++)total+=abs(bank.coefficient[c][k]);
  assert(total<262144);
 }
 const unsigned azimuths[]={0,450,900,1800,2700,3599};
 const double frequencies[]={100,1000,4000,8000,12000,16000,20000};
 for(unsigned a=0;a<6;a++)for(unsigned e=0;e<=900;e+=300)for(unsigned f=0;f<7;f++){
  double gain=amplitude(azimuths[a],e,azimuths[a],e,frequencies[f]);
  if(gain<.987 || gain>1.01){printf("Gain mismatch %u %u %.0f: %.5f\n",azimuths[a],e,frequencies[f],gain);return 1;}
 }
 double matched=amplitude(0,0,0,0,4000),opposite=amplitude(0,0,1800,0,4000);
 assert(matched>.99 && opposite<.08);
 beamform_init(&beam);
 for(unsigned i=0;i<512;i++)input[i]=(int16_t)(i*137-32768);
 beamform_process(&beam,input,output,128);for(unsigned i=0;i<512;i++)assert(input[i]==output[i]);
 for(unsigned i=0;i<512;i++)input[i]=12000;
 for(unsigned n=0;n<10;n++)beamform_process(&beam,input,output,128);
 assert(!beamform_prepare(&bank,2,350,200));beamform_target(&beam,&bank);
 for(unsigned n=0;n<10;n++){beamform_process(&beam,input,output,128);for(unsigned i=0;i<512;i++)assert(abs(output[i]-12000)<=1);}
 puts("Manual beam: direction sign, coherent gain 100 Hz-20 kHz, opposite-source attenuation, raw transparency and transition continuity passed");return 0;
}
