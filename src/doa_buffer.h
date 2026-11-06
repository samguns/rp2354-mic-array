#ifndef DOA_BUFFER_H
#define DOA_BUFFER_H
#include "doa.h"
/* Metadata is protected by the caller's core-0 lock. The producer copies into
 * FILLING slots outside that lock; the consumer only reads READING slots. */
enum doa_slot_state { DOA_FREE,DOA_FILLING,DOA_READY,DOA_READING };
struct doa_slot {
 int16_t pcm[DOA_FFT*4];
 unsigned state,epoch;
 uint32_t first_sequence,last_sequence,end_us;
};
struct doa_window_queue {
 struct doa_slot slot[2];
 int filling;
 unsigned frames,epoch;
 uint32_t dropped,gaps;
};
static inline void doa_queue_reset(struct doa_window_queue *q,unsigned epoch)
{
 for(unsigned i=0;i<2;i++)if(q->slot[i].state!=DOA_READING)q->slot[i].state=DOA_FREE;
 q->filling=-1;q->frames=0;q->epoch=epoch;
}
static inline int doa_queue_reserve(struct doa_window_queue *q,unsigned epoch,uint32_t sequence,unsigned *offset)
{
 if(q->epoch!=epoch)doa_queue_reset(q,epoch);
 if(q->filling>=0 && q->frames && sequence!=q->slot[q->filling].last_sequence+1){q->frames=0;q->gaps++;}
 if(q->filling<0){
  int i=-1;
  for(unsigned n=0;n<2;n++)if(q->slot[n].state==DOA_FREE){i=n;break;}
  if(i<0)for(unsigned n=0;n<2;n++)if(q->slot[n].state==DOA_READY &&
      (i<0 || (int32_t)(q->slot[n].last_sequence-q->slot[i].last_sequence)<0))i=n;
  if(i<0){q->dropped++;return -1;}
  if(q->slot[i].state==DOA_READY)q->dropped++;
  q->filling=i;q->frames=0;q->slot[i].state=DOA_FILLING;q->slot[i].epoch=epoch;
 }
 struct doa_slot *s=&q->slot[q->filling];
 if(!q->frames)s->first_sequence=sequence;
 *offset=q->frames;return q->filling;
}
static inline int doa_queue_commit(struct doa_window_queue *q,uint32_t sequence,uint32_t end_us,unsigned frames)
{
 struct doa_slot *s=&q->slot[q->filling];
 s->last_sequence=sequence;s->end_us=end_us;q->frames+=frames;
 if(q->frames<DOA_FFT)return 0;
 s->state=DOA_READY;q->filling=-1;q->frames=0;return 1;
}
static inline int doa_queue_take_latest(struct doa_window_queue *q)
{
 int i=-1;
 for(unsigned n=0;n<2;n++)if(q->slot[n].state==DOA_READING)return -1;
 for(unsigned n=0;n<2;n++)if(q->slot[n].state==DOA_READY &&
     (i<0 || (int32_t)(q->slot[n].last_sequence-q->slot[i].last_sequence)>0))i=n;
 if(i<0)return -1;
 for(unsigned n=0;n<2;n++)if((int)n!=i && q->slot[n].state==DOA_READY){q->slot[n].state=DOA_FREE;q->dropped++;}
 q->slot[i].state=DOA_READING;return i;
}
static inline void doa_queue_release(struct doa_window_queue *q,unsigned i){q->slot[i].state=DOA_FREE;}
#endif
