#include "nr_control.h"
#include "beamform.h"
#include "dsp_parallel.h"
#include "capture.h"
#include "audio_settings.h"
#include "audio_config.h"
#include "noise_reduce.h"
#include "usb_audio.h"
#include <zephyr/kernel.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/shell/shell.h>
#include <stdlib.h>
#include <math.h>
#include <string.h>

static struct nr_state dsp;
static void reset_dsp(int mode,int hum,int channel)
{
 nr_init_rate(&dsp,mode,hum,channel,PCM_SAMPLE_RATE);
}
static int16_t mono[128], processed[512];
static int16_t beam_pcm[2][512],beam_input[512];
static unsigned beam_slot;
static struct beamformer beam;
static struct beam_bank pending_bank;
static bool pending_beam;static uint32_t beam_revision,applied_revision;
static struct output_status output_info;
static atomic_t requested, learn, manual_active;
bool noise_control_pending(void){return atomic_get(&requested)!=0 || atomic_get(&learn)!=0;}
bool beam_manual_active(void){return atomic_get(&manual_active)!=0;}
static uint64_t whole_total,beam_total;static uint32_t whole_blocks;
static struct k_spinlock lock;
static struct { int mode,hum,channel,left,learned; uint32_t blocks,max_us; uint64_t total_us; } info;

__ramfunc __attribute__((optimize("O3"))) const int16_t *nr_apply(const int16_t *pcm)
{
 uint32_t whole_start=k_cycle_get_32(),beam_us=0;
 if(dsp_parallel_beam_wait(&beam_us)){capture_fail();return pcm;}
 beam_total+=beam_us;
 int request=atomic_set(&requested,0);
 if(request){reset_dsp((request>>2)&3,(request>>4)&127,(request>>12)&3);
  k_spinlock_key_t key=k_spin_lock(&lock);memset(&info,0,sizeof(info));k_spin_unlock(&lock,key);}
 k_spinlock_key_t beam_key=k_spin_lock(&lock);
 bool change=pending_beam && !beam.fade_left;
 if(change){beamform_target(&beam,&pending_bank);pending_beam=false;applied_revision=beam_revision;}
 k_spin_unlock(&lock,beam_key);
 if(change){dsp.learned=0;dsp.learn_left=0;memset(dsp.noise,0,sizeof(dsp.noise));}
 if(!beam.fade_left && atomic_set(&learn,0) && dsp.mode==2)nr_learn(&dsp);
 uint32_t start=k_cycle_get_32();
 memcpy(beam_input,pcm,sizeof(beam_input));pcm=beam_pcm[beam_slot];
 if(dsp.mode){
  nr_process(&dsp,pcm,mono);
  for(int n=0;n<128;n++)for(int c=0;c<4;c++)processed[4*n+c]=mono[n];}
 uint32_t elapsed=k_cyc_to_us_floor32(k_cycle_get_32()-start);
 k_spinlock_key_t key=k_spin_lock(&lock);
 info.mode=dsp.mode;info.hum=dsp.hum;info.channel=dsp.channel+1;info.left=dsp.learn_left;info.learned=dsp.learned;
 info.blocks++;info.total_us+=elapsed;info.max_us=MAX(info.max_us,elapsed);
 const struct beam_bank *bank=beam.fade_left?&beam.target:&beam.current;
 atomic_set(&manual_active,bank->mode==BF_MANUAL && bank->elevation_tdeg<900);
 output_info.beam_mode=bank->mode;output_info.azimuth_tdeg=bank->azimuth_tdeg;output_info.elevation_tdeg=bank->elevation_tdeg;
 output_info.revision=applied_revision;output_info.fade_left=beam.fade_left;output_info.pending=pending_beam;
 output_info.hum=dsp.hum;output_info.channel=dsp.channel+1;output_info.nr_mode=dsp.mode;output_info.learned=dsp.learned;output_info.learn_left=dsp.learn_left;
 for(unsigned c=0;c<4;c++)output_info.delay_milli[c]=(int)(bank->delay[c]*1000);
 k_spin_unlock(&lock,key);
 beam_slot^=1;dsp_parallel_beam_submit(&beam,beam_input,beam_pcm[beam_slot]);
 whole_total+=k_cyc_to_us_floor32(k_cycle_get_32()-whole_start);whole_blocks++;
 return dsp.mode?processed:pcm;
}
static int config(const struct shell *sh,size_t argc,char **argv)
{
 if(usb_audio_active()){shell_error(sh,"Close UAC capture before changing mode");return -EBUSY;}
 int mode=!strcmp(argv[1],"off")?0:!strcmp(argv[1],"filter")?1:!strcmp(argv[1],"suppress")?2:-1;
 char *end;long hum=0,channel=1;
 if(argc>2){hum=strtol(argv[2],&end,10);if(*end)return -EINVAL;}
 if(argc>3){channel=strtol(argv[3],&end,10);if(*end)return -EINVAL;}
 if(mode<0 || (hum!=0 && hum!=50 && hum!=60) || channel<1 || channel>4)return -EINVAL;
 int result=noise_control_set(mode,hum,channel);if(result)return result;
 shell_print(sh,"NR_CONFIG mode=%d hum=%ld channel=%ld; raw CDC unchanged",mode,hum,channel);
 if(mode==2)shell_print(sh,"Full-band suppression: 48 kHz DSP, 20 Hz low cut; mono duplicated in all slots");
 return 0;
}
static int learn_cmd(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);ARG_UNUSED(argv);
 k_spinlock_key_t key=k_spin_lock(&lock);int mode=info.mode;k_spin_unlock(&lock,key);
 int pending=atomic_get(&requested);if(pending)mode=(pending>>2)&3;
 if(mode!=2)return -EINVAL;
 audio_settings_changed();atomic_set(&learn,1);shell_print(sh,"Learning noise for 2 seconds; keep desired speech absent");return 0;
}
static int stats(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);ARG_UNUSED(argv);
 k_spinlock_key_t key=k_spin_lock(&lock);__typeof__(info) snap=info;k_spin_unlock(&lock,key);
 shell_print(sh,"NR mode=%d hum=%d channel=%d learn_left=%d learned=%d blocks=%u mean_us=%u max_us=%u state_bytes=%u",
  snap.mode,snap.hum,snap.channel,snap.left,snap.learned,snap.blocks,
  snap.blocks?(unsigned)(snap.total_us/snap.blocks):0,snap.max_us,(unsigned)sizeof(dsp));
 shell_print(sh,"BEAM_PROFILE blocks=%u whole_mean_us=%u beam_mean_us=%u",whole_blocks,whole_blocks?(unsigned)(whole_total/whole_blocks):0,whole_blocks?(unsigned)(beam_total/whole_blocks):0);
 shell_print(sh,"NR_FORMAT rate=%u channels=1 usb_slots=4 low_cut_hz=%u fft=%u hop=%u extra_delay_samples=%u",
  PCM_SAMPLE_RATE,snap.mode==2?20u:snap.mode==1?80u:0u,snap.mode==2?NR_FFT:0u,NR_HOP,snap.mode==2?NR_HOP:0u);
 return 0;
}
SHELL_STATIC_SUBCMD_SET_CREATE(nr_commands,
 SHELL_CMD_ARG(config,NULL,"off|filter|suppress [hum=0|50|60] [channel=1..4]",config,2,2),
 SHELL_CMD(learn,NULL,"Learn two seconds of noise, suppress mode only",learn_cmd),
 SHELL_CMD(stats,NULL,"DSP mode and processing cost",stats),SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(nr,&nr_commands,"Onboard mono noise reduction (UAC slots duplicated)",NULL);

int beam_control_set(int mode,unsigned azimuth,unsigned elevation,uint32_t *revision)
{
 if(audio_settings_saving())return -EBUSY;
 struct beam_bank bank;
 if(beamform_prepare(&bank,mode,azimuth,elevation))return -EINVAL;
 k_spinlock_key_t key=k_spin_lock(&lock);
 int old_mode=pending_beam?pending_bank.mode:output_info.beam_mode;
 unsigned old_az=pending_beam?pending_bank.azimuth_tdeg:(unsigned)output_info.azimuth_tdeg;
 unsigned old_el=pending_beam?pending_bank.elevation_tdeg:(unsigned)output_info.elevation_tdeg;
 if(old_mode!=bank.mode || old_az!=bank.azimuth_tdeg || old_el!=bank.elevation_tdeg){pending_bank=bank;pending_beam=true;beam_revision++;audio_settings_changed();}
 *revision=beam_revision;k_spin_unlock(&lock,key);return 0;
}
int noise_control_learn(void)
{
 if(audio_settings_saving())return -EBUSY;
 k_spinlock_key_t key=k_spin_lock(&lock);int mode=info.mode;k_spin_unlock(&lock,key);
 if(mode!=2)return -EINVAL;
 audio_settings_changed();atomic_set(&learn,1);return 0;
}
void output_status_get(struct output_status *status)
{
 k_spinlock_key_t key=k_spin_lock(&lock);*status=output_info;status->pending=pending_beam;k_spin_unlock(&lock,key);
}
static int beam_config(const struct shell *sh,size_t argc,char **argv)
{
 int mode=!strcmp(argv[1],"off")?BF_OFF:!strcmp(argv[1],"broadside")?BF_BROADSIDE:!strcmp(argv[1],"manual")?BF_MANUAL:-1;
 unsigned az=0,el=900;char *end;
 if(mode==BF_MANUAL){
  if(argc<3)return -EINVAL;
  double value=strtod(argv[2],&end);if(*end || !isfinite(value) || value<0 || value>=360)return -EINVAL;az=(unsigned)(value*10+.5);if(az>=3600)az=0;
  el=0;if(argc>3){value=strtod(argv[3],&end);if(*end || !isfinite(value) || value<0 || value>90)return -EINVAL;el=(unsigned)(value*10+.5);}
 }
 uint32_t revision;int result=beam_control_set(mode,az,el,&revision);
 if(result)return result;
 shell_print(sh,"BF queued revision=%u; 20-ms crossfade; NR profile invalidated on change",revision);return 0;
}
static int beam_stats(const struct shell *sh,size_t argc,char **argv)
{
 ARG_UNUSED(argc);ARG_UNUSED(argv);struct output_status s;output_status_get(&s);
 shell_print(sh,"BF mode=%d azimuth_tdeg=%d elevation_tdeg=%d revision=%u pending=%d fade_left=%d common_delay_samples=%u delays_milli=%d,%d,%d,%d",
  s.beam_mode,s.azimuth_tdeg,s.elevation_tdeg,s.revision,s.pending,s.fade_left,s.beam_mode?BF_COMMON_DELAY:0,s.delay_milli[0],s.delay_milli[1],s.delay_milli[2],s.delay_milli[3]);return 0;
}
SHELL_STATIC_SUBCMD_SET_CREATE(beam_commands,
 SHELL_CMD_ARG(config,NULL,"off|broadside|manual <azimuth-deg> [elevation-deg=0]",beam_config,2,2),
 SHELL_CMD(stats,NULL,"Manual beamformer state and applied delays",beam_stats),SHELL_SUBCMD_SET_END);
SHELL_CMD_REGISTER(bf,&beam_commands,"Manual delay-and-sum steering before noise reduction",NULL);

int noise_control_set(unsigned mode,unsigned hum,unsigned channel)
{
 if(audio_settings_saving())return -EBUSY;
 if(mode>2 || (hum!=0 && hum!=50 && hum!=60) || channel<1 || channel>4)return -EINVAL;
 if(usb_audio_active())return -EBUSY;
 audio_settings_changed();atomic_set(&requested,1|(mode<<2)|(hum<<4)|((channel-1)<<12));return 0;
}
void audio_control_restore(void)
{
 struct audio_config_record saved;
 if(!audio_settings_load(&saved))return;
 reset_dsp(saved.nr_mode,saved.hum,saved.channel-1);
 if(saved.learned){memcpy(dsp.noise,saved.noise,sizeof(dsp.noise));dsp.learned=1;}
 struct beam_bank bank;beamform_prepare(&bank,saved.beam_mode,saved.azimuth,saved.elevation);
 beamform_init(&beam);beam.current=bank;
 output_info.beam_mode=bank.mode;output_info.azimuth_tdeg=bank.azimuth_tdeg;output_info.elevation_tdeg=bank.elevation_tdeg;
 output_info.learned=dsp.learned;output_info.nr_mode=dsp.mode;output_info.hum=dsp.hum;output_info.channel=dsp.channel+1;
 info.learned=dsp.learned;info.mode=dsp.mode;info.hum=dsp.hum;info.channel=dsp.channel+1;
 atomic_set(&manual_active,bank.mode==BF_MANUAL && bank.elevation_tdeg<900);
}

void noise_profile_copy(unsigned *learned,float *noise)
{
 k_spinlock_key_t key=k_spin_lock(&lock);*learned=dsp.learned && !dsp.learn_left;
 if(*learned)memcpy(noise,dsp.noise,sizeof(dsp.noise));else memset(noise,0,sizeof(dsp.noise));
 k_spin_unlock(&lock,key);
}
