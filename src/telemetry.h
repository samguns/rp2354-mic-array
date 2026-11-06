#ifndef MIC_TELEMETRY_H
#define MIC_TELEMETRY_H
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
void telemetry_notify(void);
int doa_snapshot_json(char *buffer,size_t capacity,unsigned version,uint32_t session);
void doa_set_enabled(bool on);
#endif
