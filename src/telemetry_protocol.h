#ifndef MIC_TELEMETRY_PROTOCOL_H
#define MIC_TELEMETRY_PROTOCOL_H
#include <stdint.h>
#include <string.h>
enum mic_command { MIC_SUBSCRIBE, MIC_UNSUBSCRIBE, MIC_PING, MIC_DOA, MIC_BEAM, MIC_NR_LEARN, MIC_NR_CONFIG, MIC_SETTINGS_SAVE };
struct mic_request { uint32_t session,id;enum mic_command command;unsigned enabled,mode,azimuth,elevation,hum,channel; };
static inline int mic_uint(const char **p,uint32_t *value)
{
 uint32_t v=0;const char *s=*p;
 if(*s<'0'||*s>'9')return -1;
 do {unsigned digit=(unsigned)(*s-'0');if(v>(UINT32_MAX-digit)/10)return -1;v=v*10+digit;s++;}while(*s>='0'&&*s<='9');
 *value=v;*p=s;return 0;
}
/* Exact, bounded commands; no shell parsing and no allocation. */
static inline int mic_parse(const char *line,struct mic_request *r)
{
 if(strncmp(line,"MIC/2 ",6))return -1;
 const char *p=line+6;
 if(mic_uint(&p,&r->session)||!r->session||*p++!=' ')return -1;
 if(mic_uint(&p,&r->id)||*p++!=' ')return -1;
 r->enabled=0;
 if(!strcmp(p,"SUBSCRIBE"))r->command=MIC_SUBSCRIBE;
 else if(!strcmp(p,"UNSUBSCRIBE"))r->command=MIC_UNSUBSCRIBE;
 else if(!strcmp(p,"PING"))r->command=MIC_PING;
 else if(!strcmp(p,"DOA 0")||!strcmp(p,"DOA 1")){r->command=MIC_DOA;r->enabled=p[4]=='1';}
 else if(!strcmp(p,"NR_LEARN"))r->command=MIC_NR_LEARN;
 else if(!strcmp(p,"SETTINGS_SAVE"))r->command=MIC_SETTINGS_SAVE;
 else if(!strncmp(p,"NR_CONFIG ",10)){
  p+=10;
  if(mic_uint(&p,&r->mode)||*p++!=' '||mic_uint(&p,&r->hum)||*p++!=' '||mic_uint(&p,&r->channel)||*p)return -1;
  if(r->mode>2 || (r->hum!=0 && r->hum!=50 && r->hum!=60) || r->channel<1 || r->channel>4)return -1;
  r->command=MIC_NR_CONFIG;
 }
 else if(!strncmp(p,"BEAM ",5)){
  p+=5;
  if(mic_uint(&p,&r->mode)||*p++!=' '||mic_uint(&p,&r->azimuth)||*p++!=' '||mic_uint(&p,&r->elevation)||*p)return -1;
  if(r->mode>2 || r->azimuth>=3600 || r->elevation>900)return -1;
  r->command=MIC_BEAM;
 }
 else return -1;
 return 0;
}
#endif
