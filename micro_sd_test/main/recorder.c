// recorder.c
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"

#include "recorder.h"
#include "wav.h"

static const char *TAG = "RECORDER";

#define RECORDER_FILE_BUF_SIZE       2046  // Размер кратен и 2, и 3 байтам
#define RECORDER_MIC_READ_TIMEOUT_MS 100

typedef enum {
    REC_STATE_IDLE = 0,
    REC_STATE_RECORDING,
    REC_STATE_PAUSED,
    REC_STATE_STOPPING
} rec_state_t;

typedef enum {
    REC_OK = 0,
    REC_ERR_ALREADY_INIT,
    REC_ERR_ARG,
    REC_ERR_RING_BUF,
    REC_ERR_MIC,
    REC_ERR_SYNC,
    REC_ERR_TASK_CREATE,
    REC_ERR_NOT_INIT
} rec_err_t;

// Все внутренние данные рекордера
typedef struct {
    bool initialized;
    bool shutdown;
    bool rb_created;
    bool mic_created;

    rec_state_t state;
    rec_cfg_t cfg;
    ring_buf_t rbuf;

    SemaphoreHandle_t state_lock;
    SemaphoreHandle_t tasks_done;

    TaskHandle_t capture_task;
    TaskHandle_t record_task;
} recorder_ctx_t;

static recorder_ctx_t s_rec;


// Прототипы внутренних функций
static rec_err_t recorder_init(const rec_cfg_t *cfg);
static rec_err_t recorder_deinit(void);
static void recorder_release_resources(void);
static void recorder_request_shutdown(void);

static BaseType_t create_audio_capture_task(void);
static BaseType_t create_audio_record_task(void);

static void audio_capture_task(void *pvParameters);
static void audio_record_task(void *pvParameters);

static rec_state_t recorder_get_state(void);
static bool recorder_is_shutting_down(void);
static void recorder_set_state(rec_state_t state);

static size_t recorder_pack_samples(int32_t *samples, size_t count);
static size_t recorder_push_audio(const uint8_t *data, size_t len);
static void recorder_discard_buffer(uint8_t *buffer, size_t size);

static bool recorder_open_file(void);
static void recorder_close_file(void);
static void recorder_task_finished(void);

static void rec_err_init_handler(rec_err_t err);
static void rec_err_delete_handler(rec_err_t err);


// Публичные функции
bool recorder_create(const rec_cfg_t *cfg)
{
    rec_err_t err = recorder_init(cfg);
    rec_err_init_handler(err);
    return (err == REC_OK);
}

bool recorder_delete(void)
{
    rec_err_t err = recorder_deinit();
    rec_err_delete_handler(err);
    return (err == REC_OK);
}


bool recorder_start(void)
{
    if (s_rec.state_lock == NULL) return false;
    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);
    bool result = false;
    if (s_rec.initialized && !s_rec.shutdown) {
        if (s_rec.state == REC_STATE_IDLE) {
            s_rec.state = REC_STATE_RECORDING;
            result = true;
            xTaskNotifyGive(s_rec.record_task);
        }
        else if (s_rec.state == REC_STATE_RECORDING) {
            result = true;
        }
    }
    xSemaphoreGive(s_rec.state_lock);
    if (result) ESP_LOGI(TAG, "START requested");
    else ESP_LOGW(TAG, "START ignored");

    return result;
}


bool recorder_stop(void)
{
    if (s_rec.state_lock == NULL) return false;
    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);
    bool result = false;
    if (s_rec.initialized && !s_rec.shutdown) {
        if (s_rec.state == REC_STATE_IDLE || s_rec.state == REC_STATE_STOPPING) {
            result = true;
        }
        else {
            s_rec.state = REC_STATE_STOPPING;
            result = true;
            xTaskNotifyGive(s_rec.record_task);
        }
    }
    xSemaphoreGive(s_rec.state_lock);
    if (result) ESP_LOGI(TAG, "STOP requested");
    return result;
}


bool recorder_pause(void)
{
    if (s_rec.state_lock == NULL) return false;
    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);
    bool result = false;
    if (s_rec.initialized && !s_rec.shutdown) {
        if (s_rec.state == REC_STATE_PAUSED) {
            result = true;
        }
        else if (s_rec.state == REC_STATE_RECORDING) {
            s_rec.state = REC_STATE_PAUSED;
            result = true;
            xTaskNotifyGive(s_rec.record_task);
        }
    }
    xSemaphoreGive(s_rec.state_lock);
    if (result) ESP_LOGI(TAG, "PAUSE requested");
    return result;
}


bool recorder_resume(void)
{
    if (s_rec.state_lock == NULL) return false;
    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);
    bool result = false;
    if (s_rec.initialized && !s_rec.shutdown) {
        if (s_rec.state == REC_STATE_RECORDING) {
            result = true;
        }
        else if (s_rec.state == REC_STATE_PAUSED) {
            s_rec.state = REC_STATE_RECORDING;
            result = true;
            xTaskNotifyGive(s_rec.record_task);
        }
    }
    xSemaphoreGive(s_rec.state_lock);
    if (result) ESP_LOGI(TAG, "RESUME requested");
    return result;
}


bool recorder_is_recording(void)
{
    return recorder_get_state() == REC_STATE_RECORDING && s_rec.initialized;
}


bool recorder_is_paused(void)
{
    return recorder_get_state() == REC_STATE_PAUSED && s_rec.initialized;
}


// Задача захвата звука
static void audio_capture_task(void *pvParameters)
{
    (void)pvParameters;

    int32_t mic_buffer[MIC_DMA_BUF_MAX_SAMPLES];

    while (!recorder_is_shutting_down()) {
        size_t samples_read = mic_read_samples(mic_buffer, s_rec.cfg.mic_cfg.dma_buf_samples, RECORDER_MIC_READ_TIMEOUT_MS);

        if (recorder_is_shutting_down()) break;
        if (samples_read == 0) continue;

        size_t bytes = recorder_pack_samples(mic_buffer, samples_read);
        if (bytes == 0) continue;

        recorder_push_audio((uint8_t *)mic_buffer, bytes);
    }

    recorder_task_finished();
}


// Задача записи данных в WAV
static void audio_record_task(void *pvParameters)
{
    (void)pvParameters;

    uint8_t wav_buffer[RECORDER_FILE_BUF_SIZE];
    bool file_open = false;
    bool write_failed = false;

    while (1) {
        rec_state_t state = recorder_get_state();

        // Если файл ещё не открыт, обрабатываем запуск записи
        if (!file_open) {
            if (state == REC_STATE_IDLE) {
                if (recorder_is_shutting_down()) break;

                ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
                continue;
            }

            // Запись остановлена до открытия файла
            if (state == REC_STATE_STOPPING && rb_readable(&s_rec.rbuf) == 0) {
                recorder_set_state(REC_STATE_IDLE);
                if (recorder_is_shutting_down()) break;
                continue;
            }

            if (!recorder_open_file()) {
                recorder_set_state(REC_STATE_IDLE);
                recorder_discard_buffer(wav_buffer, sizeof(wav_buffer));

                if (recorder_is_shutting_down()) break;
                continue;
            }

            file_open = true;
            write_failed = false;
            ESP_LOGI(TAG, "Recording started");
        }

        // Читаем очередную порцию данных из кольцевого буфера
        size_t bytes_read = rb_read(&s_rec.rbuf, wav_buffer, sizeof(wav_buffer));

        if (bytes_read > 0) {
            if (!write_failed && !wav_write(wav_buffer, bytes_read)) {
                ESP_LOGE(TAG, "WAV write failed");
                write_failed = true;
                recorder_set_state(REC_STATE_STOPPING);
            }
            continue;
        }

        // Буфер пуст — проверяем, нужно ли закрывать файл
        state = recorder_get_state();

        if (state == REC_STATE_STOPPING) {
            recorder_close_file();
            file_open = false;
            recorder_set_state(REC_STATE_IDLE);

            if (recorder_is_shutting_down()) break;
            continue;
        }

        // RECORDING или PAUSED: ждём новых данных либо команды
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
    }

    // На случай выхода из задачи при открытом файле
    if (file_open) recorder_close_file();

    recorder_set_state(REC_STATE_IDLE);
    recorder_task_finished();
}


// Инициализация рекордера
static rec_err_t recorder_init(const rec_cfg_t *cfg)
{
    if (s_rec.initialized) return REC_ERR_ALREADY_INIT;
    if (cfg == NULL || cfg->file_path == NULL || cfg->file_path[0] == '\0' ||
        cfg->capture_stack_size == 0 || cfg->record_stack_size == 0) {
        return REC_ERR_ARG;
    }

    memset(&s_rec, 0, sizeof(s_rec));

    s_rec.cfg = *cfg;
    s_rec.state = REC_STATE_IDLE;

    s_rec.state_lock = xSemaphoreCreateMutex();
    s_rec.tasks_done = xSemaphoreCreateCounting(2, 0);

    if (s_rec.state_lock == NULL || s_rec.tasks_done == NULL) {
        recorder_release_resources();
        return REC_ERR_SYNC;
    }

    if (!rb_create(&s_rec.rbuf, &s_rec.cfg.rb_cfg)) {
        recorder_release_resources();
        return REC_ERR_RING_BUF;
    }
    s_rec.rb_created = true;

    if (!mic_create(&s_rec.cfg.mic_cfg)) {
        recorder_release_resources();
        return REC_ERR_MIC;
    }
    s_rec.mic_created = true;

    // Сначала создаём задачу записи, затем задачу захвата
    if (create_audio_record_task() != pdPASS) {
        recorder_release_resources();
        return REC_ERR_TASK_CREATE;
    }

    if (create_audio_capture_task() != pdPASS) {
        recorder_request_shutdown();
        xSemaphoreTake(s_rec.tasks_done, portMAX_DELAY);
        s_rec.record_task = NULL;

        recorder_release_resources();
        return REC_ERR_TASK_CREATE;
    }

    s_rec.initialized = true;
    return REC_OK;
}


// Удаление рекордера
static rec_err_t recorder_deinit(void)
{
    if (!s_rec.initialized) return REC_ERR_NOT_INIT;

    // Просим задачи завершить работу
    recorder_request_shutdown();

    // Ждём завершения обеих задач
    xSemaphoreTake(s_rec.tasks_done, portMAX_DELAY);
    xSemaphoreTake(s_rec.tasks_done, portMAX_DELAY);

    s_rec.capture_task = NULL;
    s_rec.record_task = NULL;

    recorder_release_resources();
    return REC_OK;
}


// Остановка задач перед освобождением ресурсов
static void recorder_request_shutdown(void)
{
    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);

    s_rec.shutdown = true;
    s_rec.initialized = false;
    s_rec.state = REC_STATE_STOPPING;

    if (s_rec.record_task != NULL) {
        xTaskNotifyGive(s_rec.record_task);
    }

    xSemaphoreGive(s_rec.state_lock);
}


// Освобождение ресурсов
static void recorder_release_resources(void)
{
    if (s_rec.mic_created) {
        mic_delete();
        s_rec.mic_created = false;
    }

    if (s_rec.rb_created) {
        rb_delete(&s_rec.rbuf);
        s_rec.rb_created = false;
    }

    if (s_rec.tasks_done != NULL) {
        vSemaphoreDelete(s_rec.tasks_done);
    }

    if (s_rec.state_lock != NULL) {
        vSemaphoreDelete(s_rec.state_lock);
    }

    memset(&s_rec, 0, sizeof(s_rec));
}


// Создание задач
static BaseType_t create_audio_capture_task(void)
{
    return xTaskCreate(audio_capture_task, "audio_capture", s_rec.cfg.capture_stack_size, NULL,
                       s_rec.cfg.capture_priority, &s_rec.capture_task);
}

static BaseType_t create_audio_record_task(void)
{
    return xTaskCreate(audio_record_task, "audio_record", s_rec.cfg.record_stack_size, NULL,
                       s_rec.cfg.record_priority, &s_rec.record_task);
}


// Получение текущего состояния
static rec_state_t recorder_get_state(void)
{
    if (s_rec.state_lock == NULL) return REC_STATE_IDLE;

    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);
    rec_state_t state = s_rec.state;
    xSemaphoreGive(s_rec.state_lock);

    return state;
}


// Проверка запроса на завершение
static bool recorder_is_shutting_down(void)
{
    if (s_rec.state_lock == NULL) return true;

    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);
    bool shutdown = s_rec.shutdown;
    xSemaphoreGive(s_rec.state_lock);

    return shutdown;
}


// Изменение состояния из внутренних задач
static void recorder_set_state(rec_state_t state)
{
    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);
    s_rec.state = state;
    xSemaphoreGive(s_rec.state_lock);
}


// Преобразование сэмплов в формат WAV
// Результат записывается в начало исходного буфера
static size_t recorder_pack_samples(int32_t *samples, size_t count)
{
    uint8_t *packed = (uint8_t *)samples;
    uint8_t target_bits = s_rec.cfg.mic_cfg.target_bits;

    if (target_bits <= 16) {
        uint8_t shift = 16 - target_bits;

        for (size_t i = 0; i < count; i++) {
            uint32_t value = (uint32_t)samples[i] << shift;

            packed[i * 2]     = (uint8_t)value;
            packed[i * 2 + 1] = (uint8_t)(value >> 8);
        }

        return count * 2;
    }

    uint8_t shift = 24 - target_bits;

    for (size_t i = 0; i < count; i++) {
        uint32_t value = (uint32_t)samples[i] << shift;

        packed[i * 3]     = (uint8_t)value;
        packed[i * 3 + 1] = (uint8_t)(value >> 8);
        packed[i * 3 + 2] = (uint8_t)(value >> 16);
    }

    return count * 3;
}


// Передача аудиоданных в кольцевой буфер
static size_t recorder_push_audio(const uint8_t *data, size_t len)
{
    size_t written = 0;
    bool recording = false;

    xSemaphoreTake(s_rec.state_lock, portMAX_DELAY);

    if (s_rec.state == REC_STATE_RECORDING && !s_rec.shutdown) {
        recording = true;

        size_t bytes_per_sample = (s_rec.cfg.mic_cfg.target_bits <= 16) ? 2 : 3;
        size_t available = rb_writable(&s_rec.rbuf);
        size_t aligned_available = available - (available % bytes_per_sample);
        size_t to_write = (len < aligned_available) ? len : aligned_available;

        if (to_write > 0) {
            written = rb_write(&s_rec.rbuf, data, to_write);
            if (written > 0) xTaskNotifyGive(s_rec.record_task);
        }
    }

    xSemaphoreGive(s_rec.state_lock);

    if (recording && written < len) {
        ESP_LOGW(TAG, "Ring buffer overflow: lost %u bytes", (unsigned)(len - written));
    }

    return written;
}


// Очистка буфера после ошибки открытия файла
static void recorder_discard_buffer(uint8_t *buffer, size_t size)
{
    while (rb_read(&s_rec.rbuf, buffer, size) > 0) {
        // Отбрасываем оставшиеся данные
    }
}


// Открытие WAV-файла
static bool recorder_open_file(void)
{
    uint8_t bits = (s_rec.cfg.mic_cfg.target_bits <= 16) ? 16 : 24;

    return wav_open(s_rec.cfg.file_path, s_rec.cfg.mic_cfg.sample_rate, 1, bits);
}


// Закрытие WAV-файла
static void recorder_close_file(void)
{
    uint32_t bytes = wav_data_bytes();

    if (wav_close()) {
        ESP_LOGI(TAG, "Recording saved: %" PRIu32 " bytes", bytes);
    }
    else {
        ESP_LOGE(TAG, "Failed to close WAV file");
    }
}


// Завершение задачи и уведомление о выходе
static void recorder_task_finished(void)
{
    xSemaphoreGive(s_rec.tasks_done);
    vTaskDelete(NULL);
}


// Обработчики ошибок
static void rec_err_init_handler(rec_err_t err)
{
    switch (err) {
        case REC_OK:               ESP_LOGI(TAG, "Recorder initialized"); break;
        case REC_ERR_ALREADY_INIT: ESP_LOGW(TAG, "recorder: already initialized"); break;
        case REC_ERR_ARG:          ESP_LOGE(TAG, "recorder: invalid configuration"); break;
        case REC_ERR_RING_BUF:     ESP_LOGE(TAG, "recorder: ring buffer creation failed"); break;
        case REC_ERR_MIC:          ESP_LOGE(TAG, "recorder: microphone creation failed"); break;
        case REC_ERR_SYNC:         ESP_LOGE(TAG, "recorder: synchronization object creation failed"); break;
        case REC_ERR_TASK_CREATE:  ESP_LOGE(TAG, "recorder: task creation failed"); break;
        default:                   ESP_LOGE(TAG, "recorder: unknown error %d", (int)err); break;
    }
}

static void rec_err_delete_handler(rec_err_t err)
{
    switch (err) {
        case REC_OK:           ESP_LOGI(TAG, "Recorder deleted"); break;
        case REC_ERR_NOT_INIT: ESP_LOGW(TAG, "recorder: not initialized"); break;
        default:               ESP_LOGE(TAG, "recorder: delete error %d", (int)err); break;
    }
}