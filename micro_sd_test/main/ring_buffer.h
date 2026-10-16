// ring_buffer.h
#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>

/**
 * Кольцевой буфер для одного писателя и одного читателя.
 * НЕ потокобезопасен для нескольких писателей/читателей!
 */
typedef struct {
    uint8_t          *buf;              // указатель на буфер (массив байтов), хранящийся в PSRAM, SRAM, стеке, куче, где угодно
    size_t            size;             // размер буфера в байтах (нужна степень двойки (2, 4, 8, ..., 512×1024 = 2^19) ДОЛЖЕН БЫТЬ МЕНЬШЕ 2ГБ
    size_t            mask;             // size - 1
    volatile size_t   head;             // монотонный счетчик прочитанных байт за все время
    volatile size_t   tail;             // монотонный счетчик записанных байт за все время

    // статистика (для отладки)
    volatile size_t   overflow_bytes;   // сколько байт не влезло
    volatile size_t   max_fill;         // максимум заполнения за все время
} ring_buf_t;                           // ← переименовано из rb_cfg_t


typedef enum {
    RB_MEM_PSRAM,      // внешняя PSRAM (большие буферы)
    RB_MEM_SRAM,       // внутренняя SRAM (быстрая, мало)
    RB_MEM_DMA,        // SRAM с доступом для DMA
    RB_MEM_ANY,        // любой доступный регион
} rb_mem_t;


typedef enum {
    RB_OK = 0,
    RB_ERR_MEMORY_ALLOC, // ошибка выделения памяти под буфер
    RB_ERR_ARG,          // нулевой размер буфера
    RB_ERR_SIZE,         // размер буфера не равен степени двойки
    RB_ERR_MEM_REGION,   // неизвестный регион памяти
    RB_ERR_NOT_INIT,     // работа с неинициализированным буфером
} rb_err_t;


typedef struct {
    size_t     size;        // размер в байтах (степень двойки)
    rb_mem_t   mem;         // регион памяти
} rb_cfg_t;


bool     rb_create(ring_buf_t *rb, const rb_cfg_t *cfg);
bool     rb_delete(ring_buf_t *rb);

size_t   rb_write(ring_buf_t *rb, const uint8_t *data, size_t len);
size_t   rb_read(ring_buf_t *rb, uint8_t *data, size_t max_len);


size_t   rb_readable(const ring_buf_t *rb);
size_t   rb_writable(const ring_buf_t *rb);
// void     rb_reset_stats(ring_buf_t *rb);
// void     rb_reset_state(ring_buf_t *rb); // ВНИМАНИЕ: вызывать только когда оба потока гарантированно остановлены.


float    rb_fill_ratio(const ring_buf_t *rb); // (для мониторинга).

#endif // RING_BUFFER_H