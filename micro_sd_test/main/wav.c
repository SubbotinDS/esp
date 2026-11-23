// wav.c
#include <stdio.h>
#include <string.h>

#include "esp_log.h"
#include "wav.h"

static const char *TAG = "WAV_WRITER";


#pragma pack(push, 1)
typedef struct {
    char     riff[4];
    uint32_t chunk_size;
    char     wave[4];
    char     fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t num_channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char     data[4];
    uint32_t data_size;
} wav_header_t;
#pragma pack(pop)
_Static_assert(sizeof(wav_header_t) == 44, "WAV header must be 44 bytes");


static bool          s_is_open     = false;
static FILE         *s_file        = NULL;
static wav_header_t  s_header;
static uint32_t      s_data_bytes  = 0; // счетчик записанных аудиобайт
static bool          s_io_ok       = false;

// прототипы
static void      wav_header_init(wav_header_t *h, uint32_t sr, uint16_t ch, uint8_t bps);
static wav_err_t wav_init(const char *path, uint32_t sr, uint16_t ch, uint8_t bps);
static wav_err_t wav_deinit(void);
static void      wav_open_err_handler(wav_err_t err, const char *path);
static void      wav_close_err_handler(wav_err_t err);


// внешние функции
bool wav_open(const char *path, uint32_t sample_rate, uint16_t channels, uint8_t bits_per_sample)
{
    wav_err_t err = wav_init(path, sample_rate, channels, bits_per_sample);
    wav_open_err_handler(err, path);
    return (err == WAV_OK);
}

bool wav_close(void)
{
    wav_err_t err = wav_deinit();
    wav_close_err_handler(err);
    return (err == WAV_OK);
}

bool wav_write(const void *data, size_t len)
{
    if (!s_is_open) return false;
    if (!s_io_ok)   return false;
    if (data == NULL || len == 0) return false;

    if (fwrite(data, 1, len, s_file) != len) {
        s_io_ok = false;
        ESP_LOGE(TAG, "write: fwrite failed");
        return false;
    }
    s_data_bytes += (uint32_t)len;
    return true;
}


// вспомогательные функции
static void wav_header_init(wav_header_t *h, uint32_t sample_rate, uint16_t channels, uint8_t bits_per_sample)
{
    const uint16_t block_align = channels * (bits_per_sample / 8);

    *h = (wav_header_t){
        .riff            = {'R','I','F','F'},         // Содержит символы "RIFF" в ASCII кодировке 0x52494646. Является началом RIFF-цепочки.
        .chunk_size      = 0,                         // Это оставшийся размер цепочки, начиная с этой позиции. Иначе говоря, это размер файла минус 8, то есть, исключены поля chunkId и chunkSize.
        .wave            = {'W','A','V','E'},         // Содержит символы "WAVE" 0x57415645
        .fmt             = {'f','m','t',' '},         // Содержит символы "fmt " 0x666d7420
        .fmt_size        = 16,                        // 16 для формата PCM. Это оставшийся размер подцепочки, начиная с этой позиции.
        .audio_format    = 1,                         // Аудио формат, список допустипых форматов. Для PCM = 1 (то есть, Линейное квантование). Значения, отличающиеся от 1, обозначают некоторый формат сжатия.
        .num_channels    = channels,                  // Количество каналов. Моно = 1, Стерео = 2 и т.д.
        .sample_rate     = sample_rate,               // Частота дискретизации. 8000 Гц, 44100 Гц и т.д.
        .byte_rate       = sample_rate * block_align, // Количество байт, переданных за секунду воспроизведения.
        .block_align     = block_align,               // Количество байт для одного сэмпла, включая все каналы.
        .bits_per_sample = bits_per_sample,           // Количество бит в сэмпле. Так называемая "глубина" или точность звучания. 8 бит, 16 бит и т.д.
        .data            = {'d','a','t','a'},         // Содержит символы "data" 0x64617461
        .data_size       = 0,                         // Количество байт в области данных.
    };
}

static wav_err_t wav_init(const char *path, uint32_t rate, uint16_t channels, uint8_t bits_per_sample)
{
    if (s_is_open) return WAV_ERR_ALREADY_OPEN;
    if (path == NULL || channels == 0 || bits_per_sample == 0 || bits_per_sample % 8 != 0) return WAV_ERR_ARG;

    s_file = fopen(path, "wb");
    if (s_file == NULL) return WAV_ERR_OPEN;

    wav_header_init(&s_header, rate, channels, bits_per_sample);
    s_data_bytes = 0;

    if (fwrite(&s_header, sizeof(s_header), 1, s_file) != 1) return WAV_ERR_HEADER_WRITE;

    s_io_ok   = true;
    s_is_open = true;
    return WAV_OK;
}

static wav_err_t wav_deinit(void)
{
    if (!s_is_open) return WAV_ERR_NOT_OPEN;

    s_header.chunk_size = s_data_bytes + 36;
    s_header.data_size  = s_data_bytes;

    wav_err_t err = WAV_OK;

    if (fseek(s_file, 0, SEEK_SET) != 0) {
        err = WAV_ERR_SEEK;
    } else if (fwrite(&s_header, sizeof(s_header), 1, s_file) != 1) {
        err = WAV_ERR_HEADER_UPDATE;
    }

    if (fclose(s_file) != 0) {
        err = WAV_ERR_CLOSE;
    }

    s_file       = NULL;
    s_is_open    = false;
    s_io_ok      = false;
    s_data_bytes = 0;
    return err;
}

uint32_t wav_data_bytes()
{
    return s_data_bytes;
}


static void wav_open_err_handler(wav_err_t err, const char *path)
{
    switch (err) {
        case WAV_ERR_HEADER_WRITE:
            fclose(s_file);
            s_file = NULL;
            break;
        default:
            break;
    }

    switch (err) {
        case WAV_OK:               ESP_LOGI(TAG, "Opened '%s'", path);              break;
        case WAV_ERR_ALREADY_OPEN: ESP_LOGW(TAG, "open: already open");             break;
        case WAV_ERR_ARG:          ESP_LOGE(TAG, "open: invalid arguments");        break;
        case WAV_ERR_OPEN:         ESP_LOGE(TAG, "open: fopen('%s') failed", path); break;
        case WAV_ERR_HEADER_WRITE: ESP_LOGE(TAG, "open: header write failed");      break;
        default:                   ESP_LOGE(TAG, "open: unknown error %d", (int)err); break;
    }
}

static void wav_close_err_handler(wav_err_t err)
{
    switch (err) {
        case WAV_OK:                ESP_LOGI(TAG, "Closed");                        break;
        case WAV_ERR_NOT_OPEN:      ESP_LOGW(TAG, "close: not open");               break;
        case WAV_ERR_SEEK:          ESP_LOGE(TAG, "close: fseek failed");           break;
        case WAV_ERR_HEADER_UPDATE: ESP_LOGE(TAG, "close: header update failed");   break;
        case WAV_ERR_CLOSE:         ESP_LOGE(TAG, "close: fclose failed");          break;
        default:                    ESP_LOGE(TAG, "close: unknown error %d", (int)err); break;
    }
}