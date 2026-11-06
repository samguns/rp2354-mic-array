#include "telemetry_protocol.h"
#include <assert.h>
int main(void)
{
 struct mic_request r;
 assert(!mic_parse("MIC/2 42 1 SUBSCRIBE",&r));assert(r.session==42&&r.id==1&&r.command==MIC_SUBSCRIBE);
 assert(!mic_parse("MIC/2 4294967295 4294967295 PING",&r));assert(r.session==UINT32_MAX&&r.id==UINT32_MAX);
 assert(!mic_parse("MIC/2 42 3 DOA 1",&r));assert(r.command==MIC_DOA&&r.enabled==1);
 assert(!mic_parse("MIC/2 42 4 DOA 0",&r));assert(r.enabled==0);
 assert(!mic_parse("MIC/2 42 5 UNSUBSCRIBE",&r));assert(r.command==MIC_UNSUBSCRIBE);
 assert(!mic_parse("MIC/2 42 6 BEAM 2 3599 900",&r));assert(r.command==MIC_BEAM&&r.mode==2&&r.azimuth==3599&&r.elevation==900);
 assert(!mic_parse("MIC/2 42 7 NR_LEARN",&r));assert(r.command==MIC_NR_LEARN);
 assert(!mic_parse("MIC/2 42 8 NR_CONFIG 2 50 1",&r));assert(r.command==MIC_NR_CONFIG && r.mode==2 && r.hum==50 && r.channel==1);
 assert(!mic_parse("MIC/2 42 9 SETTINGS_SAVE",&r));assert(r.command==MIC_SETTINGS_SAVE);
 const char *bad[]={"","MIC/1 1 1 SUBSCRIBE","MIC/2 -1 1 PING","MIC/2 0 1 PING","MIC/2 4294967296 1 PING",
  "MIC/2 1 4294967296 PING","MIC/2 1","MIC/2 1 ","MIC/2 1 2","MIC/2 1 2 DOA 2","MIC/2 1 2 DOA 1 extra",
  "MIC/2 1 2 PING ","MIC/2 1 2 boot","MIC/2 1 2 SUBSCRIBE\n","MIC/2 1 2 BEAM 3 0 0","MIC/2 1 2 BEAM 2 3600 0","MIC/2 1 2 BEAM 2 0 901","MIC/2 1 2 BEAM 2 -1 0","MIC/2 1 2 BEAM 2 1 0 extra","MIC/2 1 2 NR_CONFIG 3 50 1","MIC/2 1 2 NR_CONFIG 2 49 1","MIC/2 1 2 NR_CONFIG 2 60 0","MIC/2 1 2 NR_CONFIG 2 0 5","MIC/2 1 2 NR_CONFIG 2 0 1 extra","MIC/2 1 2 SETTINGS_SAVE extra"};
 for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++)assert(mic_parse(bad[i],&r));
 return 0;
}
