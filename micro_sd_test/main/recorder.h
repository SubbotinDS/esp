#ifndef RECORDER_H
#define RECORDER_H

#include <stdbool.h>

#include "esp_err.h"
#include "ring_buffer.h" 
#include "button.h"
#include "mic.h"

typedef enum {
    NOT_RECORDING_STATE = 0, // ожидание (запись выключена)
    RECORDING_STATE,         // идет запись
} rec_state_t;

typedef enum {
    REC_OK = 0,
    REC_ERR_ALREADY_INIT,
    REC_ERR_ARG,
    REC_ERR_RING_BUF,
    REC_ERR_MIC,
    REC_ERR_TASK_CREATE,
    REC_ERR_NOT_INIT,
} rec_err_t;

typedef struct {
    rb_cfg_t  rb_cfg;            // настройки кольцевого буфера
    mic_cfg_t mic_cfg;           // настройка микрофона
    uint8_t   capture_priority;
    uint16_t  capture_stack_size;

    uint8_t   record_priority;
    uint16_t  record_stack_size;
} rec_cfg_t;

bool recorder_create(const rec_cfg_t *cfg);
bool recorder_delete();

void HandleBtnEvent(btn_id_t id, btn_event_t event, uint32_t duration_ms);


#endif // RECORDER_H