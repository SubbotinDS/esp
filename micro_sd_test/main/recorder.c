//recorder.c
#include "esp_timer.h"
#include "esp_log.h"
#include <string.h>
#include <unistd.h>

#include "recorder.h"

static const char *TAG = "RECORDER";


#define TEST_FILE_PATH   "/sdcard/write_test.bin"
#define TEST_BLOCK_SIZE  4096

static uint8_t   s_buf[TEST_BLOCK_SIZE];
static uint32_t  s_write_count = 0;
static FILE     *s_file_ptr = NULL; //если это указатель на файл, то как это можно понять, есть ли стндартизация как называть такие указатели?

esp_err_t recorder_init(void)
{
    if (s_file_ptr != NULL) {ESP_LOGW(TAG, "Recorder already initialized"); return ESP_OK;}

    s_file_ptr = fopen(TEST_FILE_PATH, "wb");
    if (s_file_ptr == NULL) {
        ESP_LOGE(TAG, "Cannot open %s", TEST_FILE_PATH);
        return ESP_FAIL;
    }
    ESP_LOGI(TAG, "Recorder ready, file %s opened", TEST_FILE_PATH);
    return ESP_OK;
}

void HandleBtnEvent(btn_event_t event, uint32_t duration_ms)
{
    switch (event) {
        case BUTTON_EVENT_PRESS:
            // пока не используем
            break;

        case BUTTON_EVENT_SHORT_CLICK:
        {
            ESP_LOGI(TAG, "SHORT_CLICK -> write test block");
            memset(s_buf, 0xAA, sizeof(s_buf));   // заполняем чем-нибудь
            int64_t t0 = esp_timer_get_time();
            size_t  n  = fwrite(s_buf, 1, sizeof(s_buf), s_file_ptr);
            int64_t t1 = esp_timer_get_time();
            fflush(s_file_ptr);                        // сброс stdio-буфера в FATFS
            int64_t t2 = esp_timer_get_time();
            fsync(fileno(s_file_ptr));                 // сброс FATFS на реальную SD
            int64_t t3 = esp_timer_get_time();
            s_write_count++;
            ESP_LOGI(TAG, "#%lu: fwrite(%zu B)=%lldus  fflush=%lldus  fsync=%lldus  total=%lldus",  s_write_count, n,  t1 - t0, t2 - t1, t3 - t2, t3 - t0);
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