#ifndef RECORDER_H
#define RECORDER_H

#include "esp_err.h"
#include "button.h"    // для btn_event_t

esp_err_t recorder_init(void);
void      HandleBtnEvent(btn_event_t event, uint32_t duration_ms);

#endif // RECORDER_H