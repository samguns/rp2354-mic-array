#include "doa.h"
#include <stdio.h>
static struct doa_workspace work;
static int16_t pcm[DOA_FRAMES*4];
int main(int argc,char **argv)
{
 if(argc!=2)return 2;
 FILE *f=fopen(argv[1],"rb");if(!f)return 3;
 if(fread(pcm,sizeof(int16_t),DOA_FRAMES*4,f)!=DOA_FRAMES*4){fclose(f);return 4;}fclose(f);
 doa_init(&work);struct doa_result r;doa_estimate(&work,pcm,&r);
 printf("{\"rejected\":%u,\"angle\":%.7f,\"rms\":%.7f,\"fit_rms\":%.7f,\"pairs\":[",r.rejected,r.angle,r.rms,r.fit_rms);
 for(unsigned p=0;p<6;p++)printf("%s{\"lag_samples\":%.7f,\"gcc_score\":%.7f,\"mean_coherence\":%.7f,\"coherent_band_fraction\":%.7f}",
  p?",":"",r.pair[p].lag,r.pair[p].score,r.pair[p].coherence,r.pair[p].band_fraction);
 puts("]}");return 0;
}
