// ring_buffer.c
#include <string.h>
#include <stdlib.h>              // free
#include "esp_log.h"
#include "esp_heap_caps.h" // проверить нужно ли
#include "ring_buffer.h"
//#include "esp_attr.h"     // для __sync_synchronize() на некоторых сборках

static const char *TAG = "RING_BUF";

// прототипы
static  rb_err_t rb_init(ring_buf_t *rb, const rb_cfg_t *cfg);
static uint32_t rb_mem_to_malloc_caps(rb_mem_t mem);
static void rb_reset_state(ring_buf_t *rb); // ВНИМАНИЕ: оба потока должны быть остановлены!
static void rb_reset_stats(ring_buf_t *rb);
static void rb_err_handler(rb_err_t err);




bool rb_create(ring_buf_t *rb, const rb_cfg_t *cfg)
{
    rb_err_t err = rb_init(rb, cfg);
    rb_err_handler(err);
    return (err == RB_OK);
}

bool rb_delete(ring_buf_t *rb)
{
    if (rb == NULL || rb->buf == NULL) return false;
    free(rb->buf);
    rb->buf  = NULL;
    rb->size = 0;
    rb->mask = 0;

    rb_reset_stats(rb);
    rb_reset_state(rb);
    return true;
}



size_t rb_write(ring_buf_t *rb, const uint8_t *data, size_t len)
{
    if (len <= 0) return 0;

    size_t tail     = rb->tail;   
    size_t head     = rb->head;
    size_t used     = tail - head;                          // сколько байт сейчас используется в буфере
    size_t free     = rb->size - used;                      // сколько байт доступно для записи
    size_t to_write = (len < free) ? len : free;            // сколько из пришедших байт запишется
    size_t overflow = (to_write < len)? len - to_write : 0; // сколько из пришедших байт потеряется
    size_t idx      = tail & rb->mask;                      // индекс в массиве, с которого начинаем писать
    size_t to_wrap   = rb->size - idx;                      // сколько байт до зацикливания от позиции начала записи

    if (to_write <= to_wrap) {
        memcpy(&rb->buf[idx], &data[0],       to_write);
    } else {
        memcpy(&rb->buf[idx], &data[0],       to_wrap);
        memcpy(&rb->buf[0],   &data[to_wrap], to_write-to_wrap);
    }

    __sync_synchronize(); // Барьер: данные записаны в память ДО того, как обновится tail.
    rb->tail = tail + to_write;

    size_t new_used = used + to_write;
    if (new_used > rb->max_fill) { rb->max_fill = new_used;} // обновляем пиковое заполнение за все время
    rb->overflow_bytes += overflow;                          // какие потери (сколько не записали из того, что хотели записать) за все время

    return to_write;
}


size_t rb_read(ring_buf_t *rb, uint8_t *dst, size_t max_len)
{
    if (max_len <= 0) return 0;

    size_t head    = rb->head;
    size_t tail    = rb->tail;
    size_t used    = tail - head;
    size_t to_read = (max_len < used) ? max_len : used; // сколько из хранящихся байт прочтется

    if (to_read == 0) return 0; // нечего читать (буфер пуст)

    size_t idx     = head & rb->mask;  // индекс в массиве, с которого начинаем читать
    size_t to_wrap = rb->size - idx;   // сколько байт до зацикливания от позиции начала чтения

    if (to_read <= to_wrap) {
        memcpy(&dst[0], &rb->buf[idx], to_read);
    } else {
        memcpy(&dst[0],     &rb->buf[idx], to_wrap);
        memcpy(&dst[to_wrap], &rb->buf[0], to_read-to_wrap);
    }

    // Барьер: данные прочитаны ДО того, как обновится head.
    __sync_synchronize();
    rb->head = head + to_read;

    return to_read;
}








size_t rb_readable(const ring_buf_t *rb) {return rb->tail - rb->head;}
size_t rb_writable(const ring_buf_t *rb) {return rb->size - (rb->tail - rb->head);}

float rb_fill_ratio(const ring_buf_t *rb)
{
    size_t used = rb->tail - rb->head;
    return (float)used / (float)rb->size;
}











static rb_err_t rb_init(ring_buf_t *rb, const rb_cfg_t *cfg)
{
    // 1. Проверки аргументов
    if (rb == NULL || cfg == NULL)          return RB_ERR_ARG;
    if (cfg->size == 0)                     return RB_ERR_ARG;
    if ((cfg->size & (cfg->size - 1)) != 0) return RB_ERR_SIZE;

    // 2. Регион памяти
    uint32_t caps = rb_mem_to_malloc_caps(cfg->mem);
    if (caps == 0) return RB_ERR_MEM_REGION;

    // 3. Выделяем память
    uint8_t *storage = heap_caps_malloc(cfg->size, caps);
    if (storage == NULL) return RB_ERR_MEMORY_ALLOC;

    // 4. Заполняем структуру буфера
    rb->buf            = storage;
    rb->size           = cfg->size;
    rb->mask           = cfg->size - 1;
    rb_reset_state(rb);
    rb_reset_stats(rb);
    return RB_OK;
}

static uint32_t rb_mem_to_malloc_caps(rb_mem_t mem)
{
    switch (mem) {
        case RB_MEM_PSRAM: return MALLOC_CAP_SPIRAM;
        case RB_MEM_SRAM:  return MALLOC_CAP_INTERNAL;
        case RB_MEM_DMA:   return MALLOC_CAP_DMA;
        case RB_MEM_ANY:   return MALLOC_CAP_DEFAULT;
        default:           return 0;
    }
}

static void rb_reset_state(ring_buf_t *rb) // ВНИМАНИЕ: оба потока должны быть остановлены!
{
    rb->head = 0;
    rb->tail = 0;
}
static void rb_reset_stats(ring_buf_t *rb)
{
    rb->overflow_bytes = 0;
    rb->max_fill       = 0;
}



static void rb_err_handler(rb_err_t err)
{
    switch (err) {
        case RB_OK:               ESP_LOGI(TAG, "Ring buffer initialized");             break;
        case RB_ERR_MEMORY_ALLOC: ESP_LOGE(TAG, "Memory allocation failed");            break;
        case RB_ERR_ARG:          ESP_LOGE(TAG, "rb_init: NULL или size == 0");         break;
        case RB_ERR_SIZE:         ESP_LOGE(TAG, "rb_init: size не степень двойки");     break;
        case RB_ERR_MEM_REGION:   ESP_LOGE(TAG, "Unknown memory region");               break;
        default:                  ESP_LOGE(TAG, "rb_init: unknown error %d", (int)err); break;
    }
}