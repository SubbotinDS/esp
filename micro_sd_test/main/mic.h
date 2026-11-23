// mic.h
#ifndef MIC_H
#define MIC_H

#include <stdbool.h>
#include <stddef.h>
#include "driver/gpio.h"

// Границы точности (в битах), которую поддерживает модуль. 
#define MIC_TARGET_BITS_MIN   8  // Минимум для разборчивого звука
#define MIC_TARGET_BITS_MAX   24 // 24 бита - максимум, который физически выдает INMP441.

// Верхняя граница размера одного DMA-блока (в сэмплах).
#define MIC_DMA_BUF_MAX_SAMPLES   512

typedef enum {
    MIC_OK = 0,
    MIC_ERR_ALREADY_INIT,   // модуль уже инициализирован
    MIC_ERR_ARG,            // невалидные аргументы
    MIC_ERR_I2S_CHANNEL,    // ошибка создания I2S-канала
    MIC_ERR_I2S_PINS,       // ошибка настройки пинов I2S
    MIC_ERR_I2S_ENABLE,     // ошибка включения канала
    MIC_ERR_NOT_INIT,       // работа с неинициализированным модулем
} mic_err_t;

typedef struct {
    gpio_num_t sck;
    gpio_num_t ws;
    gpio_num_t sd;
    uint32_t   sample_rate;      // частота дискретизации, Гц.
    uint8_t    dma_buf_count;    // количество DMA-блоков
    uint16_t   dma_buf_samples;  // количество сэмплов в одном DMA-блоке (не должно превышать MIC_RAW_MAX_SAMPLES)
    uint8_t    target_bits;      // количество значащих битов, которые хотим оставить из полного 24 битного семпла микрофона
} mic_cfg_t;

bool   mic_create(const mic_cfg_t *cfg);
bool   mic_delete();
size_t mic_read_samples(int32_t *out_buf, size_t samples_to_read, uint32_t timeout_ms); // кладет буфеер количество реально прочитанных семплов а также вощвращает это значение

#endif // MIC_H

