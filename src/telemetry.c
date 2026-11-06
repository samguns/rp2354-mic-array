#include "telemetry.h"
#include "telemetry_protocol.h"
#include "nr_control.h"
#include "audio_settings.h"
#include "usb_audio.h"
#include <zephyr/kernel.h>
#include <zephyr/drivers/uart.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/printk.h>
#include <hardware/timer.h>
#include <string.h>

#define PACKET_BYTES 1152
#define COMMAND_BYTES 80
#define LEASE_MS 3500
static const struct device *const uart=DEVICE_DT_GET(DT_NODELABEL(cdc_telemetry));
K_SEM_DEFINE(wake,0,1);
K_MSGQ_DEFINE(commands,COMMAND_BYTES,4,4);
struct reply {uint16_t length;char bytes[256];};
K_MSGQ_DEFINE(replies,sizeof(struct reply),4,4);
static struct k_spinlock tx_lock;
static char active[PACKET_BYTES],pending[PACKET_BYTES];
static unsigned active_length,active_offset,pending_length;
static atomic_t changed,skipped,rx_errors;
static atomic_t service_max,format_max,irq_max,config_max;
static void profile(atomic_t *maximum,uint32_t start)
{uint32_t elapsed=time_us_32()-start;if(elapsed>(uint32_t)atomic_get(maximum))atomic_set(maximum,elapsed);}

void telemetry_notify(void){atomic_set(&changed,1);k_sem_give(&wake);}

/* CDC invokes this in its workqueue context. Only bounded FIFO/queue work here;
 * JSON formatting and controls run on the service thread. */
static void interrupt(const struct device *dev,void *unused)
{
 ARG_UNUSED(unused);
 uint32_t started=time_us_32();
 static char line[COMMAND_BYTES];static unsigned length;static bool overflow;
 uart_irq_update(dev);
 if(uart_irq_rx_ready(dev)){
  uint8_t bytes[128];int count=uart_fifo_read(dev,bytes,sizeof(bytes));
  for(int i=0;i<count;i++){
   char c=bytes[i];
   if(c=='\r')continue;
   if(c=='\n'){
    if(!overflow && length){line[length]=0;if(k_msgq_put(&commands,line,K_NO_WAIT))atomic_inc(&rx_errors);else k_sem_give(&wake);}
    length=0;overflow=false;
   }else if(!overflow){
    if(length+1<sizeof(line))line[length++]=c;else{overflow=true;atomic_inc(&rx_errors);}
   }
  }
 }
 if(uart_irq_tx_ready(dev)){
  k_spinlock_key_t key=k_spin_lock(&tx_lock);
  if(active_offset==active_length){
   struct reply reply;
   active_length=active_offset=0;
   if(!k_msgq_get(&replies,&reply,K_NO_WAIT)){
    memcpy(active,reply.bytes,reply.length);active_length=reply.length;
   }else if(pending_length){memcpy(active,pending,pending_length);active_length=pending_length;pending_length=0;}
  }
  if(active_length){
   int sent=uart_fifo_fill(dev,(const uint8_t *)active+active_offset,active_length-active_offset);
   if(sent>0)active_offset+=sent;
  }else uart_irq_tx_disable(dev);
  k_spin_unlock(&tx_lock,key);
 }
 profile(&irq_max,started);
}

static void control_reply(uint32_t session,uint32_t id,const char *type,int enabled)
{
 struct reply r;
 int length=snprintk(r.bytes,sizeof(r.bytes),
  "{\"v\":2,\"type\":\"%s\",\"service\":\"mic_array\",\"session\":%u,\"id\":%u,\"rate\":48000,\"channels\":4,\"enabled\":%d}\n",type,session,id,enabled);
 if(length<=0 || length>=(int)sizeof(r.bytes))return;
 r.length=length;
 if(k_msgq_put(&replies,&r,K_NO_WAIT)){atomic_inc(&rx_errors);return;}
 uart_irq_tx_enable(uart);
}
static void send_snapshot(uint32_t session)
{
 char buffer[PACKET_BYTES];
 uint32_t started=time_us_32();
 int length=doa_snapshot_json(buffer,sizeof(buffer),2,session);
 profile(&format_max,started);
 if(length<=0 || length>=(int)sizeof(buffer)){atomic_inc(&skipped);return;}
 /* Add transport counters before the closing brace/newline. */
 length-=2;
 int extra=snprintk(buffer+length,sizeof(buffer)-length,",\"tx_skipped\":%d,\"rx_errors\":%d,\"service_max_us\":%d,\"format_max_us\":%d,\"irq_max_us\":%d,\"config_max_us\":%d}\n",(int)atomic_get(&skipped),(int)atomic_get(&rx_errors),(int)atomic_get(&service_max),(int)atomic_get(&format_max),(int)atomic_get(&irq_max),(int)atomic_get(&config_max));
 if(extra<0 || extra>=(int)(sizeof(buffer)-length)){atomic_inc(&skipped);return;}
 length+=extra;
 struct output_status s;output_status_get(&s);length-=2;
 extra=snprintk(buffer+length,sizeof(buffer)-length,",\"bf_mode\":%d,\"bf_azimuth_tdeg\":%d,\"bf_elevation_tdeg\":%d,\"bf_revision\":%u,\"bf_pending\":%d,\"bf_fade_left\":%d,\"bf_delays_milli\":[%d,%d,%d,%d],\"nr_mode\":%d,\"nr_learned\":%d,\"nr_learn_left\":%d,\"nr_hum\":%d,\"nr_channel\":%d,\"settings_saved\":%d,\"settings_saving\":%d,\"settings_error\":%d,\"uac_active\":%d}\n",s.beam_mode,s.azimuth_tdeg,s.elevation_tdeg,s.revision,s.pending,s.fade_left,s.delay_milli[0],s.delay_milli[1],s.delay_milli[2],s.delay_milli[3],s.nr_mode,s.learned,s.learn_left,s.hum,s.channel,audio_settings_match(),audio_settings_saving(),audio_settings_error(),usb_audio_active());
 if(extra<0 || extra>=(int)(sizeof(buffer)-length)){atomic_inc(&skipped);return;}length+=extra;
 k_spinlock_key_t key=k_spin_lock(&tx_lock);
 if(pending_length)atomic_inc(&skipped);
 memcpy(pending,buffer,length);pending_length=length;
 k_spin_unlock(&tx_lock,key);
 uart_irq_tx_enable(uart);
}
static void service(void *a,void *b,void *c)
{
 ARG_UNUSED(a);ARG_UNUSED(b);ARG_UNUSED(c);
 if(!device_is_ready(uart))return;
 uart_irq_callback_user_data_set(uart,interrupt,NULL);uart_irq_rx_enable(uart);
 uint32_t session=0,last_rx=0,last_send=0;bool subscribed=false,need_snapshot=false;
 for(;;){
  k_sem_take(&wake,K_MSEC(need_snapshot?20:250));
  uint32_t started=time_us_32();
  uint32_t now=k_uptime_get_32(),dtr=0;
  uart_line_ctrl_get(uart,UART_LINE_CTRL_DTR,&dtr);
  if(!dtr || (subscribed && now-last_rx>LEASE_MS)){
   subscribed=false;session=0;need_snapshot=false;
   k_spinlock_key_t key=k_spin_lock(&tx_lock);pending_length=0;k_spin_unlock(&tx_lock,key);
  }
  char line[COMMAND_BYTES];
  while(!k_msgq_get(&commands,line,K_NO_WAIT)){
   struct mic_request request;
   if(!dtr || mic_parse(line,&request)){atomic_inc(&rx_errors);continue;}
   if(request.command==MIC_SUBSCRIBE){
    session=request.session;subscribed=true;last_rx=now;
    last_send=now-20;need_snapshot=true;
    control_reply(session,request.id,"hello",-1);telemetry_notify();
   }else if(subscribed && request.session==session){
    last_rx=now;
    if(request.command==MIC_UNSUBSCRIBE){subscribed=false;need_snapshot=false;control_reply(session,request.id,"ack",-1);}
    else if(request.command==MIC_PING)control_reply(session,request.id,"pong",-1);
    else if(request.command==MIC_DOA){uint32_t start=time_us_32();doa_set_enabled(request.enabled);profile(&config_max,start);control_reply(session,request.id,"ack",request.enabled);}
    else if(request.command==MIC_BEAM || request.command==MIC_NR_LEARN || request.command==MIC_NR_CONFIG || request.command==MIC_SETTINGS_SAVE){
     uint32_t revision=0;
     const char *control=request.command==MIC_BEAM?"beam":request.command==MIC_NR_CONFIG?"nr_config":request.command==MIC_SETTINGS_SAVE?"settings_save":"nr_learn";
     int error=request.command==MIC_BEAM?beam_control_set(request.mode,request.azimuth,request.elevation,&revision):request.command==MIC_NR_CONFIG?noise_control_set(request.mode,request.hum,request.channel):request.command==MIC_SETTINGS_SAVE?audio_settings_save_restart():noise_control_learn();
     struct reply reply;
     int length=snprintk(reply.bytes,sizeof(reply.bytes),"{\"v\":2,\"type\":\"ack\",\"session\":%u,\"id\":%u,\"control\":\"%s\",\"ok\":%d,\"revision\":%u}\n",session,request.id,control,!error,revision);
     reply.length=length;if(length>0 && length<(int)sizeof(reply.bytes) && !k_msgq_put(&replies,&reply,K_NO_WAIT))uart_irq_tx_enable(uart);else atomic_inc(&rx_errors);
     telemetry_notify();
    }
   }
  }
  if(atomic_set(&changed,0))need_snapshot=true;
  /* Coalesce connection/control bursts. Steady estimator updates are 171 ms
   * apart and stay immediate; back-to-back formatting must let capture run. */
  if(subscribed && (need_snapshot || now-last_send>=1000) && now-last_send>=20){
   send_snapshot(session);last_send=now;need_snapshot=false;
  }
  if(!subscribed)need_snapshot=false;
  profile(&service_max,started);
 }
}
/* Bounded formatting only; waits when idle. USB work and DMA outrank it. */
K_THREAD_DEFINE(telemetry_worker,4096,service,NULL,NULL,NULL,1,0,0);
