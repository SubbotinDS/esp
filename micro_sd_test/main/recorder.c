//recorder.c
#include <stdio.h>
//#include <string.h>
//#include <stdlib.h> //может понадобится для free
//#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "esp_log.h"
//#include "esp_timer.h"

//#include "esp_heap_caps.h" не факт что понадобится

#include "recorder.h"
#include "ring_buffer.h"
#include "mic.h"
#include "wav.h"
#include "config.h"

static const char *TAG = "RECORDER";

#define TEST_FILE_PATH   "/sdcard/record.wav"

static FILE                *s_file_ptr = NULL;
static bool                 s_rec_initialized = false;
static rec_cfg_t            s_rec_cfg;
static ring_buf_t           s_rbuf;
static TaskHandle_t         s_audio_record_task_handle  = NULL;
static TaskHandle_t         s_audio_capture_task_handle = NULL;
static volatile rec_state_t s_rec_state      = NOT_RECORDING_STATE;


// прототипы внутренних функций
static rec_err_t  recorder_init(const rec_cfg_t *cfg);
static rec_err_t  recorder_deinit();
static BaseType_t create_audio_capture_task(void);
static BaseType_t create_audio_record_task();
static void       audio_capture_task(void *pvParameters);
static void       audio_record_task(void *pvParameters);
static void       rec_cleanup();
static void       rec_reset_state();
static void       start_stop_record();
static void       rec_err_init_handler(rec_err_t err);
static void       rec_err_delete_handler(rec_err_t err);



// внешние функции
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


static void audio_capture_task(void *pvParameters) // читаем из микрофона, пишем в микроконтроллер(РингБуфер)
{
    int32_t mic_buffer[MIC_DMA_BUF_SAMPLES]; 
    while (1) {
        size_t samples_read = mic_read_samples(mic_buffer, MIC_DMA_BUF_SAMPLES, portMAX_DELAY);
        if (samples_read == 0 || s_rec_state != RECORDING_STATE) continue;
        #if MIC_TARGET_BITS <= 16
                //16-битный формат: 2 байта на сэмпл
                int16_t packed[MIC_DMA_BUF_SAMPLES];
                for (size_t i = 0; i < samples_read; i++) {
                    packed[i] = (int16_t)mic_buffer[i];
                }
                rb_write(&s_rbuf, (uint8_t *)packed, samples_read * 2);

        #else
                //24-битный формат: 3 байта на сэмпл, с выравниванием по старшему
                uint8_t packed[MIC_DMA_BUF_SAMPLES * 3];
                const uint8_t left_shift = 24 - MIC_TARGET_BITS;
                size_t out_idx = 0;
                for (size_t i = 0; i < samples_read; i++) {
                    int32_t val = mic_buffer[i] << left_shift; // выравниваем по 24 битам
                    packed[out_idx++] = (val >> 0)  & 0xFF;
                    packed[out_idx++] = (val >> 8)  & 0xFF;
                    packed[out_idx++] = (val >> 16) & 0xFF;
                }
                rb_write(&s_rbuf, (uint8_t *)packed, samples_read * 3);
        #endif
    }
}

static void audio_record_task(void *pvParameters)
{
    uint8_t wav_buffer[RECORDER_FILE_BUF_SIZE];
    /* char    path[RECORD_PATH_LEN]; */

    const uint16_t wav_bits = (MIC_TARGET_BITS <= 16) ? 16 : 24;

    while (1) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        if (s_rec_state == NOT_RECORDING_STATE) continue;

        // if (!next_record_path(TEST_FILE_PATH, sizeof(TEST_FILE_PATH))) {
        //     ESP_LOGE(TAG, "No free filename");
        //     s_rec_state = NOT_RECORDING_STATE;
        //     continue;
        // }

        if (!wav_open(TEST_FILE_PATH, MIC_SAMPLE_RATE, 1, wav_bits)) {
            s_rec_state = NOT_RECORDING_STATE;
            continue;
        }

        while (s_rec_state == RECORDING_STATE) {
            size_t n = rb_read(&s_rbuf, wav_buffer, sizeof(wav_buffer));
            if (n == 0) { vTaskDelay(pdMS_TO_TICKS(10)); continue; }
            if (!wav_write(wav_buffer, n)) {
                ESP_LOGE(TAG, "Aborting recording: write failed");
                break;
            }
        }

        uint32_t total = wav_data_bytes();
        wav_close();
        ESP_LOGI(TAG, "Saved %s (%lu bytes)", TEST_FILE_PATH, (unsigned long)total);
    }
}


// внутренние функции
static rec_err_t recorder_init(const rec_cfg_t *cfg)
{
    if (s_rec_initialized) return REC_ERR_ALREADY_INIT;
    if (cfg == NULL)       return REC_ERR_ARG;
    s_rec_cfg = *cfg;

    if (!rb_create(&s_rbuf, &s_rec_cfg.rb_cfg)) return REC_ERR_RING_BUF;        // 1. Создание ринг буфера
    if (!mic_create(&s_rec_cfg.mic_cfg))        return REC_ERR_MIC;             // 2. Создание микрофона
    if (create_audio_capture_task() != pdPASS)  return REC_ERR_TASK_CREATE;     // 3. Создание задачи получения данных с микрофона и заполнение рингбуфера
    if (create_audio_record_task() != pdPASS)   return REC_ERR_TASK_CREATE;     // 4. Создание задачи получения данных с рингбуфера и заполнение sd карты

    s_rec_initialized = true;
    return REC_OK;
}
static rec_err_t recorder_deinit(void)
{
    if (!s_rec_initialized) return REC_ERR_NOT_INIT;
    rec_cleanup();
    return REC_OK;
}

static BaseType_t create_audio_capture_task()
{
    return xTaskCreate(
        audio_capture_task, "audio_capture",
        s_rec_cfg.capture_stack_size,
        NULL,
        s_rec_cfg.capture_priority,
        &s_audio_capture_task_handle
    );
}

static BaseType_t create_audio_record_task()
{
    return xTaskCreate(
        audio_record_task, "record",
        s_rec_cfg.record_stack_size,
        NULL,
        s_rec_cfg.record_priority,
        &s_audio_record_task_handle
    );
}

static void rec_cleanup(void)
{
    if (s_audio_record_task_handle != NULL) { vTaskDelete(s_audio_record_task_handle); s_audio_record_task_handle = NULL; }
    if (s_audio_capture_task_handle != NULL) { vTaskDelete(s_audio_capture_task_handle); s_audio_capture_task_handle = NULL; }
    mic_delete();
    rb_delete(&s_rbuf);
    rec_reset_state();
}
static void rec_reset_state()
{
    s_rec_state       = NOT_RECORDING_STATE;
    s_rec_initialized = false;
}

static void start_stop_record()
{
    if(s_rec_state == NOT_RECORDING_STATE) {
        s_rec_state = RECORDING_STATE;
        xTaskNotifyGive(s_audio_record_task_handle); 
    }
    else{
        s_rec_state = NOT_RECORDING_STATE;
    }
}


void HandleBtnEvent(btn_id_t id, btn_event_t event, uint32_t duration_ms)
{
    if (id != REC_BTN_ID) return;   // пока реагируем только на кнопку записи
    switch (event) {
        case BUTTON_EVENT_PRESS: /* пока не используем */  break;
        case BUTTON_EVENT_SHORT_CLICK: start_stop_record(); break;
        case BUTTON_EVENT_LONG_PRESS: /* пока не используем */ break;
        case BUTTON_EVENT_LONG_CLICK: /* пока не используем */ break;
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