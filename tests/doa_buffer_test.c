#include "doa_buffer.h"
#include <stdio.h>
#include <string.h>
#define CHECK(x) do {if(!(x)){fprintf(stderr,"FAIL %s at %d\n",#x,__LINE__);return 1;}}while(0)
static struct doa_window_queue q;
static int push(unsigned epoch,uint32_t sequence,int16_t value)
{
 unsigned offset;int i=doa_queue_reserve(&q,epoch,sequence,&offset);
 if(i<0)return -1;
 for(unsigned n=0;n<128*4;n++)q.slot[i].pcm[offset*4+n]=value;
 return doa_queue_commit(&q,sequence,sequence*100,128);
}
int main(void)
{
 doa_queue_reset(&q,1);
 for(unsigned n=0;n<16;n++)CHECK(push(1,n,11)==(n==15));
 int reading=doa_queue_take_latest(&q);CHECK(reading>=0);
 for(unsigned n=16;n<32;n++)CHECK(push(1,n,22)==(n==31));
 CHECK(q.slot[reading].state==DOA_READING);
 for(unsigned n=0;n<DOA_FFT*4;n++)CHECK(q.slot[reading].pcm[n]==11);
 /* Producer overload discards a ready window, never the reader's PCM. */
 for(unsigned n=32;n<48;n++)push(1,n,33);
 CHECK(q.dropped==1);
 for(unsigned n=0;n<DOA_FFT*4;n++)CHECK(q.slot[reading].pcm[n]==11);
 doa_queue_release(&q,reading);reading=doa_queue_take_latest(&q);
 CHECK(q.slot[reading].first_sequence==32 && q.slot[reading].last_sequence==47);
 CHECK(q.slot[reading].pcm[0]==33);
 /* Reconfiguration preserves an old in-flight reader while collecting new data. */
 doa_queue_reset(&q,2);
 for(unsigned n=100;n<116;n++)push(2,n,44);
 CHECK(q.slot[reading].pcm[0]==33);
 doa_queue_release(&q,reading);reading=doa_queue_take_latest(&q);
 CHECK(q.slot[reading].epoch==2 && q.slot[reading].first_sequence==100);
 doa_queue_release(&q,reading);
 /* A gap restarts the partial window rather than stitching missing samples. */
 for(unsigned n=200;n<204;n++)push(2,n,55);
 for(unsigned n=300;n<316;n++)push(2,n,66);
 CHECK(q.gaps==1);reading=doa_queue_take_latest(&q);
 CHECK(q.slot[reading].first_sequence==300 && q.slot[reading].last_sequence==315);
 for(unsigned n=0;n<DOA_FFT*4;n++)CHECK(q.slot[reading].pcm[n]==66);
 doa_queue_release(&q,reading);
 /* With two completed windows, the consumer chooses the newest across wrap. */
 doa_queue_reset(&q,3);
 for(uint32_t n=0;n<32;n++)push(3,0xfffffff0u+n,77);
 unsigned old_drops=q.dropped;reading=doa_queue_take_latest(&q);
 CHECK(q.slot[reading].first_sequence==0 && q.slot[reading].last_sequence==15);
 CHECK(q.dropped==old_drops+1);
 puts("PASS overlapped ownership, overload, generation reset, gaps and sequence wrap");return 0;
}
