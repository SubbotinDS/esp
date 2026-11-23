#ifndef RECORDER_H
#define RECORDER_H

#include <stdbool.h>
#include <stdint.h>

#include "ring_buffer.h"
#include "mic.h"

typedef struct {
    rb_cfg_t rb_cfg;
    mic_cfg_t mic_cfg;
    const char *file_path;

    uint8_t capture_priority;
    uint16_t capture_stack_size;
    uint8_t record_priority;
    uint16_t record_stack_size;
} rec_cfg_t;

bool recorder_create(const rec_cfg_t *cfg);
bool recorder_delete(void);

bool recorder_start(void);
bool recorder_stop(void);
bool recorder_pause(void);
bool recorder_resume(void);

bool recorder_is_recording(void);
bool recorder_is_paused(void);

#endif