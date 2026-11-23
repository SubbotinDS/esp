// mic.c
#include "freertos/FreeRTOS.h"
#include "driver/i2s_std.h"
#include "esp_log.h"

#include "mic.h"

static const char *TAG = "MIC";

static bool              s_mic_initialized = false;
static mic_cfg_t         s_mic_cfg;
static i2s_chan_handle_t s_mic_rx          = NULL;

// прототипы внутренних функций
static mic_err_t mic_init(const mic_cfg_t *cfg);
static mic_err_t mic_deinit(void);
static void mic_reset_state(void);
static void mic_cleanup(void);
static void mic_err_init_handler(mic_err_t err);
static void mic_err_delete_handler(mic_err_t err);
static mic_err_t mic_validate_cfg(const mic_cfg_t *cfg);
static mic_err_t mic_create_i2s_channel(const mic_cfg_t *cfg);
static mic_err_t mic_configure_std_mode(const mic_cfg_t *cfg);
static mic_err_t mic_enable_channel(void);


// основные функции
bool mic_create(const mic_cfg_t *cfg)
{
    mic_err_t err = mic_init(cfg);
    mic_err_init_handler(err);
    return (err == MIC_OK);
}

bool mic_delete(void)
{
    mic_err_t err = mic_deinit();
    mic_err_delete_handler(err);
    return (err == MIC_OK);
}

size_t mic_read_samples(int32_t *out_buf, size_t samples_to_read, uint32_t timeout_ms)
{
    if (s_mic_rx == NULL || out_buf == NULL || samples_to_read == 0) return 0;

    if (samples_to_read > s_mic_cfg.dma_buf_samples){
        samples_to_read = s_mic_cfg.dma_buf_samples;
    }
    //количество семплов в штуках
    
    size_t bytes_read = 0;
    esp_err_t err = i2s_channel_read(s_mic_rx,
                                     out_buf,
                                     samples_to_read * sizeof(int32_t), // количество семплов в байтах
                                     &bytes_read,
                                     pdMS_TO_TICKS(timeout_ms));
    if (err != ESP_OK || bytes_read == 0) return 0;

    size_t valid_samples = bytes_read / sizeof(int32_t); // количество семплов в штуках

    uint8_t bit_shift = 32 - s_mic_cfg.target_bits; 
    for (size_t i = 0; i < valid_samples; i++) {
        out_buf[i] = out_buf[i] >> bit_shift; 
    }
    return valid_samples;
}











// вспомогательные функции
static mic_err_t mic_init(const mic_cfg_t *cfg)
{
    mic_err_t err = mic_validate_cfg(cfg);
    if (err != MIC_OK) return err;

    s_mic_cfg = *cfg;

    err = mic_create_i2s_channel(cfg);
    if (err != MIC_OK) return err;

    err = mic_configure_std_mode(cfg);
    if (err != MIC_OK) return err;

    err = mic_enable_channel();
    if (err != MIC_OK) return err;

    s_mic_initialized = true;
    return MIC_OK;
}

static mic_err_t mic_validate_cfg(const mic_cfg_t *cfg)
{
    if (s_mic_initialized) return MIC_ERR_ALREADY_INIT;
    if ((cfg == NULL)                                     ||
        (cfg->sample_rate == 0)                           ||
        (cfg->dma_buf_samples == 0)                       ||
        (cfg->dma_buf_samples > MIC_DMA_BUF_MAX_SAMPLES)  ||
        (cfg->target_bits < MIC_TARGET_BITS_MIN)          ||
        (cfg->target_bits > MIC_TARGET_BITS_MAX)) {
        return MIC_ERR_ARG;
    }
    return MIC_OK;
}

static mic_err_t mic_create_i2s_channel(const mic_cfg_t *cfg)
{
    i2s_chan_config_t chan_cfg   = I2S_CHANNEL_DEFAULT_CONFIG(I2S_NUM_0, I2S_ROLE_MASTER);
    chan_cfg.dma_desc_num        = cfg->dma_buf_count;
    chan_cfg.dma_frame_num       = cfg->dma_buf_samples;
    chan_cfg.auto_clear_after_cb = true;
    if (i2s_new_channel(&chan_cfg, NULL, &s_mic_rx) != ESP_OK) return MIC_ERR_I2S_CHANNEL;
    return MIC_OK;
}

#define I2S_STD_GPIO_CFG(sck_pin, ws_pin, sd_pin) { \
    .mclk = I2S_GPIO_UNUSED, \
    .bclk = (sck_pin), \
    .ws   = (ws_pin), \
    .dout = I2S_GPIO_UNUSED, \
    .din  = (sd_pin), \
    .invert_flags = { .mclk_inv = false, .bclk_inv = false, .ws_inv = false } \
}

static mic_err_t mic_configure_std_mode(const mic_cfg_t *cfg)
{
    i2s_std_slot_config_t slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(I2S_DATA_BIT_WIDTH_32BIT, I2S_SLOT_MODE_MONO);
    slot_cfg.slot_mask = I2S_STD_SLOT_LEFT;

    i2s_std_config_t std_cfg = {
        .clk_cfg  = I2S_STD_CLK_DEFAULT_CONFIG(cfg->sample_rate),
        .slot_cfg = slot_cfg,
        .gpio_cfg = I2S_STD_GPIO_CFG(cfg->sck, cfg->ws, cfg->sd),
    };

    if (i2s_channel_init_std_mode(s_mic_rx, &std_cfg) != ESP_OK) return MIC_ERR_I2S_PINS;
    return MIC_OK;
}

static mic_err_t mic_enable_channel(void)
{
    if (i2s_channel_enable(s_mic_rx) != ESP_OK) return MIC_ERR_I2S_ENABLE;
    return MIC_OK;
}


static mic_err_t mic_deinit()
{
    if (!s_mic_initialized) return MIC_ERR_NOT_INIT;
    mic_cleanup();
    mic_reset_state();
    return MIC_OK;
}

static void mic_cleanup()
{
    if (s_mic_rx != NULL) {
        i2s_del_channel(s_mic_rx);
        s_mic_rx = NULL;
    }
}

static void mic_reset_state(){s_mic_initialized = false;}





static void mic_err_init_handler(mic_err_t err)
{
    switch (err) {
        case MIC_ERR_I2S_PINS:     // Канал создан, но настройка провалилась
        case MIC_ERR_I2S_ENABLE:   // Канал создан и настроен, но не включился
            mic_cleanup(); break;
        default: break; // Если упало на проверке аргументов или создании канала, чистить нечего
    }
       
    switch (err) {
        case MIC_OK:               ESP_LOGI(TAG, "Mic initialized");                  break;
        case MIC_ERR_ALREADY_INIT: ESP_LOGW(TAG, "mic_init: already initialized");    break;
        case MIC_ERR_ARG:          ESP_LOGE(TAG, "mic_init: invalid argument");       break;
        case MIC_ERR_I2S_CHANNEL:  ESP_LOGE(TAG, "mic_init: i2s_new_channel failed"); break;
        case MIC_ERR_I2S_PINS:     ESP_LOGE(TAG, "mic_init: i2s init pins failed");   break;
        case MIC_ERR_I2S_ENABLE:   ESP_LOGE(TAG, "mic_init: i2s enable failed");      break;
        default:                   ESP_LOGE(TAG, "mic_init: unknown error %d", (int)err); break;
    }
}

static void mic_err_delete_handler(mic_err_t err)
{
    switch (err) {
        case MIC_OK:           ESP_LOGI(TAG, "Mic deleted");                  break;
        case MIC_ERR_NOT_INIT: ESP_LOGW(TAG, "mic_delete: not initialized");  break;
        default:               ESP_LOGE(TAG, "mic_delete: unknown error %d", (int)err); break;
    }
}