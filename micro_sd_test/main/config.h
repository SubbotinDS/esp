//config.h для того, что меняет разработчик
#ifndef CONFIG_H
#define CONFIG_H

#include "button.h"
#include "ring_buffer.h"
#include "recorder.h"
#include "mic.h"
#include "driver/gpio.h"

// пины 
#define BUTTON_GPIO_REC          GPIO_NUM_10
#define BUTTON_GPIO_PAUSE        GPIO_NUM_9

#define MIC_GPIO_SCK             GPIO_NUM_6
#define MIC_GPIO_WS              GPIO_NUM_4
#define MIC_GPIO_SD              GPIO_NUM_5




// ─── Ring buffer ─────────────────────────────────────
#define RING_BUFFER_SIZE  (512 * 1024)  // значение в байтах

// ─── Buttons ─────────────────────────────────────────
#define BUTTON_LONG_PRESS_MS     1500
#define BUTTON_TASK_PRIORITY     5
#define BUTTON_TASK_STACK_SIZE   4096
#define BUTTON_ACTIVE_LEVEL      BUTTON_ACTIVE_LOW //по дефолту низкий уровень

enum {
    REC_BTN_ID   = 0,
    PAUSE_BTN_ID = 1,
};

// ─── Recorder ────────────────────────────────────────
#define RECORDER_CAPTURE_PRIORITY     6
#define RECORDER_CAPTURE_STACK_SIZE   4096
#define RECORDER_FILE_BUF_SIZE        2048
#define RECORDER_RECORD_PRIORITY      5
#define RECORDER_RECORD_STACK_SIZE    8192

// ─── Mic ─────────────────────────────────────────────
#define MIC_SAMPLE_RATE       16000
#define MIC_DMA_BUF_COUNT     8
#define MIC_DMA_BUF_SAMPLES   256 // количество (верхнее ограничкние 512)
#define MIC_TARGET_BITS       16  // количество значащих битов, которые хотим оставить из полного 24 битного семпла микрофона

btn_cfg_t config_button(btn_id_t id, gpio_num_t gpio);
rb_cfg_t  config_ring_buffer();
rec_cfg_t config_recorder();
mic_cfg_t config_mic();


#endif