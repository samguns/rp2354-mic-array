#include "doa.h"
#include <math.h>
#include <string.h>
#ifdef __ZEPHYR__
#include <zephyr/linker/section_tags.h>
#define HOT __ramfunc __attribute__((optimize("O3")))
#else
#define HOT
#endif
#ifdef __ZEPHYR__
#define POLL() dsp_parallel_doa_poll()
#else
#define POLL() ((void)0)
#endif
#define PI 3.14159265358979323846f
static const uint8_t pairs[6][2]={{0,1},{0,2},{0,3},{1,2},{1,3},{2,3}};
/* Exposed-port view: top, left, bottom, right; USB-C upper right. */
static const float xy[4][2]={{0,.025f},{-.025f,0},{0,-.025f},{.025f,0}};

void doa_init(struct doa_workspace *w)
{
 memset(w,0,sizeof(*w));
 for(unsigned n=0;n<DOA_FFT;n++)w->window[n]=.5f-.5f*cosf(2*PI*n/(DOA_FFT-1));
 for(unsigned n=0;n<DOA_FFT/2;n++){
  w->tw_re[n]=cosf(2*PI*n/DOA_FFT);w->tw_im[n]=-sinf(2*PI*n/DOA_FFT);
 }
 for(unsigned k=0;k<DOA_BINS;k++){
  float step=2*PI*(k+DOA_FIRST_BIN)/(DOA_FFT*4);
  w->step_re[k]=cosf(step);w->step_im[k]=sinf(step);
  for(unsigned group=0;group<2;group++){
   unsigned limit=group?40:32;
   w->scan_re[group][k]=cosf(-(float)limit*step);
   w->scan_im[group][k]=sinf(-(float)limit*step);
  }
 }
}
static HOT void transform(struct doa_workspace *w)
{
 for(unsigned i=1,j=0;i<DOA_FFT;i++){
  if(!(i&255))POLL();
  unsigned bit=DOA_FFT/2;while(j&bit){j^=bit;bit>>=1;}j^=bit;
  if(i<j){float t=w->re[i];w->re[i]=w->re[j];w->re[j]=t;t=w->im[i];w->im[i]=w->im[j];w->im[j]=t;}
 }
 /* Fuse pairs of radix-2 stages: keep intermediate butterflies in FPU
  * registers instead of storing/reloading the whole array between stages. */
 for(unsigned start=0;start<DOA_FFT;start+=4){
  if(!(start&255))POLL();
  float ar=w->re[start],ai=w->im[start],br=w->re[start+1],bi=w->im[start+1];
  float cr=w->re[start+2],ci=w->im[start+2],dr=w->re[start+3],di=w->im[start+3];
  float pr=ar+br,pi=ai+bi,qr=ar-br,qi=ai-bi;
  float rr=cr+dr,ri=ci+di,sr=cr-dr,si=ci-di;
  w->re[start]=pr+rr;w->im[start]=pi+ri;w->re[start+2]=pr-rr;w->im[start+2]=pi-ri;
  w->re[start+1]=qr+si;w->im[start+1]=qi-sr;w->re[start+3]=qr-si;w->im[start+3]=qi+sr;
 }
 unsigned len=8;
 for(;len*2<=DOA_FFT;len*=4){
  unsigned stride=DOA_FFT/len;
  for(unsigned start=0;start<DOA_FFT;start+=2*len)for(unsigned k=0;k<len/2;k++){
   if(!(k&31))POLL();
   unsigned a=start+k,b=a+len/2,c=a+len,d=c+len/2,t=k*stride;
   float ur=w->tw_re[t],ui=w->tw_im[t],vr=w->tw_re[t/2],vi=w->tw_im[t/2];
   float ar=w->re[a],ai=w->im[a],br=w->re[b],bi=w->im[b];
   float cr=w->re[c],ci=w->im[c],dr=w->re[d],di=w->im[d];
   float tr=ur*br-ui*bi,ti=ur*bi+ui*br;
   float pr=ar+tr,pi=ai+ti,qr=ar-tr,qi=ai-ti;
   tr=ur*dr-ui*di;ti=ur*di+ui*dr;
   float rr=cr+tr,ri=ci+ti,sr=cr-tr,si=ci-ti;
   tr=vr*rr-vi*ri;ti=vr*ri+vi*rr;
   w->re[a]=pr+tr;w->im[a]=pi+ti;w->re[c]=pr-tr;w->im[c]=pi-ti;
   /* W^(k+len/2) = -j W^k. */
   tr=vi*sr+vr*si;ti=vi*si-vr*sr;
   w->re[b]=qr+tr;w->im[b]=qi+ti;w->re[d]=qr-tr;w->im[d]=qi-ti;
  }
 }
 if(len<=DOA_FFT)for(unsigned start=0;start<DOA_FFT;start+=len)for(unsigned k=0;k<len/2;k++){
  if(!(k&63))POLL();
  unsigned a=start+k,b=a+len/2,t=k*(DOA_FFT/len);
  float wr=w->tw_re[t],wi=w->tw_im[t];
  float r=wr*w->re[b]-wi*w->im[b],im=wr*w->im[b]+wi*w->re[b];
  w->re[b]=w->re[a]-r;w->im[b]=w->im[a]-im;w->re[a]+=r;w->im[a]+=im;
 }

}
HOT void doa_begin(struct doa_workspace *w,struct doa_result *out)
{
 memset(out,0,sizeof(*out));
 memset(w->power,0,sizeof(w->power));memset(w->cross_re,0,sizeof(w->cross_re));memset(w->cross_im,0,sizeof(w->cross_im));
}
HOT void doa_add_window(struct doa_workspace *w,const int16_t *pcm,struct doa_result *out)
{
 float energy=0;
 for(unsigned i=0;i<DOA_FFT*4;i++){
  if(!(i&1023))POLL();
  float x=pcm[i];energy+=x*x;out->channel_rms[i&3]+=x*x/DOA_FRAMES;
  if(pcm[i]==32767 || pcm[i]==-32768)out->rejected|=DOA_CLIPPED;
 }
 out->rms+=energy/(DOA_FRAMES*4);
  /* Pack two real channels into one complex FFT, then recover their spectra. */
  for(unsigned c=0;c<4;c+=2){
   POLL();
   float mean=0,mean_b=0;const int16_t *p=pcm+c;
   for(unsigned n=0;n<DOA_FFT;n++){mean+=p[4*n];mean_b+=p[4*n+1];}
   mean/=DOA_FFT;mean_b/=DOA_FFT;
   for(unsigned n=0;n<DOA_FFT;n++){w->re[n]=(p[4*n]-mean)*w->window[n];w->im[n]=(p[4*n+1]-mean_b)*w->window[n];}
   transform(w);
   for(unsigned k=0;k<DOA_BINS;k++){
    unsigned bin=k+DOA_FIRST_BIN,mirror=DOA_FFT-bin;
    float r=.5f*(w->re[bin]+w->re[mirror]),i=.5f*(w->im[bin]-w->im[mirror]);
    float br=.5f*(w->im[bin]+w->im[mirror]),bi=.5f*(w->re[mirror]-w->re[bin]);
    w->bins_re[c][k]=r;w->bins_im[c][k]=i;w->power[c][k]+=r*r+i*i;
    w->bins_re[c+1][k]=br;w->bins_im[c+1][k]=bi;w->power[c+1][k]+=br*br+bi*bi;
   }
  }
  for(unsigned p=0;p<6;p++)for(unsigned k=0;k<DOA_BINS;k++){
   unsigned a=pairs[p][0],b=pairs[p][1];
   float ar=w->bins_re[a][k],ai=w->bins_im[a][k],br=w->bins_re[b][k],bi=w->bins_im[b][k];
   w->cross_re[p][k]+=ar*br+ai*bi;w->cross_im[p][k]+=ar*bi-ai*br;
  }
}
HOT void doa_finish(struct doa_workspace *w,struct doa_result *out)
{
 out->rms=sqrtf(out->rms);
 for(unsigned c=0;c<4;c++)out->channel_rms[c]=sqrtf(out->channel_rms[c]);
 if(out->rms<4)out->rejected|=DOA_LOW_LEVEL;
 float xx=0,yy=0,x_y=0,xt=0,yt=0;
 for(unsigned p=0;p<6;p++){
  unsigned a=pairs[p][0],b=pairs[p][1];
  float dx=xy[b][0]-xy[a][0],dy=xy[b][1]-xy[a][1];
  float bound=sqrtf(dx*dx+dy*dy)*DOA_RATE/343;
  unsigned group=(p==1 || p==4)?1:0;
  unsigned limit=group?40:32,count=limit*2+1;
  memset(w->scores,0,count*sizeof(float));
  float floor=0;
  for(unsigned k=0;k<DOA_BINS;k++){
   float r=w->cross_re[p][k],i=w->cross_im[p][k],m=sqrtf(r*r+i*i);
   if(m>floor)floor=m;
  }
  floor*=.01f;
  float sum_weight=0;unsigned active=0;
  for(unsigned k=0;k<DOA_BINS;k++){
   POLL();
   float r=w->cross_re[p][k],i=w->cross_im[p][k],mag2=r*r+i*i,mag=sqrtf(mag2);
   float denom=w->power[a][k]*w->power[b][k];
   float weight=denom>1e-30f?mag2/denom:0;
   if(weight>1)weight=1;
   sum_weight+=weight;if(weight>=.5f && mag>floor)active++;
   float scale=weight/(mag>floor?mag:floor>0?floor:1);
   float cr=w->scan_re[group][k],ci=w->scan_im[group][k],dr=w->step_re[k],di=w->step_im[k];
   r*=scale;i*=scale;
   for(unsigned g=0;g<count;g++){
    w->scores[g]+=r*cr-i*ci;
    float next=cr*dr-ci*di;ci=cr*di+ci*dr;cr=next;
   }
  }
  unsigned best=0;for(unsigned g=1;g<count;g++)if(w->scores[g]>w->scores[best])best=g;
  float lag=((int)best-(int)limit)/4.f;
  if(best && best+1<count){
   float lo=w->scores[best-1],mid=w->scores[best],hi=w->scores[best+1],d=lo-2*mid+hi;
   if(d!=0)lag+=.5f*(lo-hi)/d/4;
  }
  struct doa_pair *v=&out->pair[p];v->lag=lag;v->score=sum_weight>0?w->scores[best]/sum_weight:0;
  v->coherence=sum_weight/DOA_BINS;v->band_fraction=(float)active/DOA_BINS;
  if(best==0 || best+1==count || v->coherence<.5f || v->score<.5f || v->band_fraction<.4f)out->rejected|=DOA_POOR_BAND;
  if(fabsf(lag)>bound+.45f)out->rejected|=DOA_DELAY_BOUND;
  float x=-dx*DOA_RATE/343,y=-dy*DOA_RATE/343;
  xx+=sum_weight*x*x;yy+=sum_weight*y*y;x_y+=sum_weight*x*y;xt+=sum_weight*x*lag;yt+=sum_weight*y*lag;
 }
 float det=xx*yy-x_y*x_y;
 if(det<=1e-12f){out->rejected|=DOA_BAD_FIT;return;}
 out->ux=(xt*yy-yt*x_y)/det;out->uy=(yt*xx-xt*x_y)/det;
 float sum=0;
 for(unsigned p=0;p<6;p++){
  unsigned a=pairs[p][0],b=pairs[p][1];
  float fitted=-((xy[b][0]-xy[a][0])*out->ux+(xy[b][1]-xy[a][1])*out->uy)*DOA_RATE/343;
  float error=out->pair[p].lag-fitted;sum+=error*error;
  if(fabsf(error)>out->max_residual)out->max_residual=fabsf(error);
 }
 out->fit_rms=sqrtf(sum/6);
 if(out->fit_rms>.450001f || out->max_residual>.900001f)out->rejected|=DOA_BAD_FIT;
 float lateral=sqrtf(out->ux*out->ux+out->uy*out->uy);
 if(lateral<.25f)out->rejected|=DOA_NEAR_NORMAL;
 if(lateral>1.05f)out->rejected|=DOA_BAD_VECTOR;
 out->angle=atan2f(out->uy,out->ux)*180/PI;if(out->angle<0)out->angle+=360;
}

HOT void doa_estimate(struct doa_workspace *w,const int16_t *pcm,struct doa_result *out)
{
 doa_begin(w,out);
 for(unsigned window=0;window<DOA_WINDOWS;window++)doa_add_window(w,pcm+window*DOA_FFT*4,out);
 doa_finish(w,out);
}
