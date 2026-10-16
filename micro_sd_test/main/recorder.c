//recorder.c

#include "freertos/FreeRTOS.h"
//#include <string.h>
//#include <stdlib.h> //может понадобится для free
//#include <unistd.h>
//#include "esp_timer.h"
#include "esp_log.h"

//#include "esp_heap_caps.h" не факт что понадобится

#include "recorder.h"
#include "ring_buffer.h"

static const char *TAG = "RECORDER";

#define TEST_FILE_PATH   "/sdcard/write_test.bin"
#define TEST_BLOCK_SIZE  4096

static uint8_t   s_buf[TEST_BLOCK_SIZE];
static uint32_t  s_write_count = 0;
static FILE     *s_file_ptr = NULL;



static bool                s_rec_initialized = false;
static rec_cfg_t           s_rec_cfg;
static ring_buf_t          s_rbuf;
static TaskHandle_t        s_rec_task_handle = NULL;
static volatile rec_state_t s_rec_state      = NOT_RECORDING_STATE; // нужен ли volatile?


// прототипы внутренних функций
static rec_err_t  recorder_init(const rec_cfg_t *cfg);
static rec_err_t  recorder_deinit(void);
static BaseType_t create_rec_task(void);
static void rec_task(void *pvParameters);
static void rec_reset_resources(void);
static void rec_reset_state(void);
static void rec_err_init_handler(rec_err_t err);
static void rec_err_delete_handler(rec_err_t err);








bool recorder_create(const rec_cfg_t *cfg)
{
    rec_err_t err = recorder_init(cfg);
    rec_err_init_handler(err);
    return (err == REC_OK);
}

bool recorder_delete()
{
    rec_err_t err = recorder_deinit();
    rec_err_delete_handler(err);
    return (err == REC_OK);
}

static rec_err_t recorder_init(const rec_cfg_t *cfg)
{
    if (s_rec_initialized) return REC_ERR_ALREADY_INIT;
    if (cfg == NULL)       return REC_ERR_ARG;
    s_rec_cfg = *cfg;

    // 1. Кольцевой буфер
    if (!rb_create(&s_rbuf, &s_rec_cfg.rb_cfg)) {
        return REC_ERR_RING_BUF;
    }

    // 2. Задача
    if (create_rec_task() != pdPASS) {
        rb_delete(&s_rbuf);   // откат буфера при ошибке
        return REC_ERR_TASK_CREATE;
    }

    s_rec_initialized = true;
    return REC_OK;
}

static rec_err_t recorder_deinit() // почему только проверка только на инициализцию
{
    if (!s_rec_initialized) return REC_ERR_NOT_INIT;

    rec_reset_resources();
    rb_delete(&s_rbuf);
    rec_reset_state();
    return REC_OK;
}


static void rec_reset_resources()
{
    if (s_rec_task_handle != NULL) { vTaskDelete(s_rec_task_handle); s_rec_task_handle = NULL;}
}

static void rec_reset_state()
{
    s_rec_state       = NOT_RECORDING_STATE;
    s_rec_initialized = false;
}


static BaseType_t create_rec_task()
{
    return xTaskCreate(
        rec_task,
        "rec_task",
        s_rec_cfg.task_stack_size,
        NULL,
        s_rec_cfg.task_priority,
        &s_rec_task_handle
    );
}


static void rec_task(void *pvParameters)
{
    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);   // спим до сигнала
        if (s_rec_state == NOT_RECORDING_STATE) continue;
        ESP_LOGI(TAG, "START recording");

        while (s_rec_state == RECORDING_STATE) {
            // читаем буфер, пишем на SD
            ESP_LOGI(TAG, "Recording... fill=%.2f", rb_fill_ratio(&s_rbuf));
            vTaskDelay(pdMS_TO_TICKS(1000));   // пока нет I2S — пауза
        }

        ESP_LOGI(TAG, "STOP recording");
    }
}


void HandleBtnEvent(btn_event_t event, uint32_t duration_ms)
{
    switch (event) {
        case BUTTON_EVENT_PRESS: /* пока не используем */ break;

        // первый клик - запись, второй - стоп, нет записи
        case BUTTON_EVENT_SHORT_CLICK:
        {
            if(s_rec_state == NOT_RECORDING_STATE) {
                s_rec_state = RECORDING_STATE;
                xTaskNotifyGive(s_rec_task_handle);
            }
            else{
                s_rec_state = NOT_RECORDING_STATE;
            }
        }
        break;

        case BUTTON_EVENT_LONG_PRESS:
            ESP_LOGI(TAG, "LONG_PRESS -> (not used yet)");
            break;

        case BUTTON_EVENT_LONG_CLICK:
            ESP_LOGI(TAG, "LONG_CLICK -> (not used yet)");
            break;
    }
}





static void rec_err_init_handler(rec_err_t err)
{
    switch (err) {
        case REC_OK:               ESP_LOGI(TAG, "Recorder initialized");           break;
        case REC_ERR_ALREADY_INIT: ESP_LOGW(TAG, "recorder_init: already initialized"); break;
        case REC_ERR_ARG:          ESP_LOGE(TAG, "recorder_init: cfg == NULL");     break;
        case REC_ERR_RING_BUF:     ESP_LOGE(TAG, "recorder_init: ring buffer failed"); break;
        case REC_ERR_TASK_CREATE:  ESP_LOGE(TAG, "recorder_init: task creation failed"); break;
        default:                   ESP_LOGE(TAG, "recorder_init: unknown error %d", (int)err); break;
    }
}

static void rec_err_delete_handler(rec_err_t err)
{
    switch (err) {
        case REC_OK:           ESP_LOGI(TAG, "Recorder deleted");                break;
        case REC_ERR_NOT_INIT: ESP_LOGW(TAG, "recorder_delete: not initialized"); break;
        default:               ESP_LOGE(TAG, "recorder_delete: unknown error %d", (int)err); break;
    }
}