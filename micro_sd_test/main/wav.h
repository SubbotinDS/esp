// wav.h
#ifndef WAV_H
#define WAV_H

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>

typedef enum {
    WAV_OK = 0,
    WAV_ERR_ALREADY_OPEN,
    WAV_ERR_ARG,
    WAV_ERR_OPEN,
    WAV_ERR_HEADER_WRITE,
    WAV_ERR_WRITE,
    WAV_ERR_SEEK,
    WAV_ERR_HEADER_UPDATE,
    WAV_ERR_CLOSE,
    WAV_ERR_NOT_OPEN,
} wav_err_t;

bool     wav_open(const char *path, uint32_t sample_rate, uint16_t channels, uint8_t bits_per_sample);
bool     wav_write(const void *data, size_t len);
bool     wav_close();
//bool     wav_delete(/* сюда нужно передавать название файла который хочешь удалить */); // пока не нужно делать реализацию этого

uint32_t wav_data_bytes();


#endif // WAV_H