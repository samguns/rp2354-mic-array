#include "pdm_filter.h"
#include <stdint.h>
#include <stdio.h>
static struct pdm_filter whole,left,right,cic_stage,fir_stage;
static uint32_t words[1024],random_state=42;
static int16_t all[512],stage[512],a[256],b[256];
int main(void)
{
 pdm_filter_init(&whole);pdm_filter_init(&left);pdm_filter_init(&right);pdm_filter_init(&cic_stage);pdm_filter_init(&fir_stage);
 for(unsigned block=0;block<100;block++){
  if(block==50){pdm_filter_init(&whole);pdm_filter_init(&left);pdm_filter_init(&right);pdm_filter_init(&cic_stage);pdm_filter_init(&fir_stage);}
  for(unsigned n=0;n<1024;n++){
   random_state^=random_state<<13;random_state^=random_state>>17;random_state^=random_state<<5;words[n]=random_state;
  }
  pdm_filter_process(&whole,words,1024,all);
  pdm_filter_cic(&cic_stage,words,1024);
  pdm_filter_fir(&fir_stage,&cic_stage.intermediate[0][0],512,stage);
  pdm_filter_process_channels(&left,words,1024,a,0,2);
  pdm_filter_process_channels(&right,words,1024,b,2,2);
  for(unsigned n=0;n<128;n++)for(unsigned c=0;c<4;c++){
   int16_t actual=c<2?a[n*2+c]:b[n*2+c-2];
   if(actual!=all[n*4+c] || stage[n*4+c]!=all[n*4+c]){fprintf(stderr,"Partition mismatch block=%u frame=%u channel=%u\n",block,n,c);return 1;}
  }
 }
 puts("PASS: partitioned channels and separated CIC/FIR stages match four-channel conversion exactly, including reset");
 return 0;
}
