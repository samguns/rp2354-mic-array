#ifndef DSP_PARALLEL_H
#define DSP_PARALLEL_H
#include "pdm_filter.h"
#include "capture.h"
#include "doa.h"
#include "beamform.h"
int dsp_parallel_beam_wait(uint32_t *elapsed_us);
void dsp_parallel_beam_submit(struct beamformer *state,const int16_t *input,int16_t *output);
int dsp_parallel_doa_window(struct doa_workspace *workspace,const int16_t *pcm,struct doa_result *result,unsigned window);
void dsp_parallel_doa_profile(uint32_t *active_us,uint32_t *max_cic_wait_us);
int dsp_parallel_init(void);
int dsp_parallel_park(void);
void dsp_parallel_profile(uint32_t *local_us,uint32_t *remote_us,uint32_t *status);
int dsp_parallel_process(struct pdm_filter *local,struct capture_block *incoming,int16_t *pcm,struct capture_block **completed);
#endif
