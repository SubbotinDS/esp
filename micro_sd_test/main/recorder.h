#ifndef RECORDER_H
#define RECORDER_H

#include <stdbool.h>

#include "esp_err.h"
#include "ring_buffer.h" 
#include "button.h"

typedef enum {
    NOT_RECORDING_STATE = 0, // ожидание (запись выключена)
    RECORDING_STATE,         // идет запись
} rec_state_t;

typedef enum {
    REC_OK = 0,
    REC_ERR_ALREADY_INIT,
    REC_ERR_ARG,
    REC_ERR_RING_BUF,
    REC_ERR_TASK_CREATE,
    REC_ERR_NOT_INIT,
} rec_err_t;

typedef struct {
    rb_cfg_t rb_cfg;            // настройки кольцевого буфера
    uint8_t  task_priority;     // приоритет задачи диктофона (например, 5)
    uint16_t task_stack_size;   // размер стека (например, 2048)
} rec_cfg_t;

bool recorder_create(const rec_cfg_t *cfg);
bool recorder_delete();

void HandleBtnEvent(btn_event_t event, uint32_t duration_ms);


#endif // RECORDER_H