#include "beamform.h"
#include "noise_reduce.h"
#include <assert.h>
#include <stdio.h>
static int16_t input[512], beam[512], mono[128];
static struct nr_state nr;
int main(void)
{
 /* Coherent speech is preserved even at both PCM limits. */
 for(int n=0;n<128;n++)for(int c=0;c<4;c++)input[4*n+c]=(n&1)?32767:-32768;
 beamform_broadside(input,beam,128);
 for(int i=0;i<512;i++)assert(beam[i]==input[i]);
 /* Opposite spatial phases cancel; channels remain identical. */
 for(int n=0;n<128;n++){input[4*n]=12000;input[4*n+1]=-12000;input[4*n+2]=3000;input[4*n+3]=-3000;}
 beamform_broadside(input,beam,128);
 for(int i=0;i<512;i++)assert(beam[i]==0);
 /* Equal-energy orthogonal channel noise loses 6.02 dB in power. */
 long long before=0,after=0;
 for(int n=0;n<128;n++)for(int c=0;c<4;c++){
  input[4*n+c]=(n&(1<<c))?4000:-4000;
  if(c==0)before+=(long long)input[4*n+c]*input[4*n+c];
 }
 beamform_broadside(input,beam,128);
 for(int n=0;n<128;n++){after+=(long long)beam[4*n]*beam[4*n];for(int c=1;c<4;c++)assert(beam[4*n+c]==beam[4*n]);}
 assert(after*4==before);
 /* Existing NR channel selection is transparent on the duplicated beam. */
 for(int c=0;c<4;c++){
  nr_init(&nr,0,0,c);nr_process(&nr,beam,mono);
  for(int n=0;n<128;n++)assert(mono[n]==beam[4*n]);
 }
 puts("Broadside: coherent gain, PCM limits, cancellation, 6.02 dB orthogonal-noise reduction and NR input passed");
 return 0;
}
