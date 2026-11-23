// config.c — сборка конфигураций из настроек
#include "config.h"

btn_cfg_t config_button(btn_id_t id, gpio_num_t gpio)
{
    btn_cfg_t cfg = {
        .id            = id,
        .gpio_num      = gpio,
        .active_level  = BUTTON_ACTIVE_LEVEL,
        .long_press_ms = BUTTON_LONG_PRESS_MS,
    };
    return cfg;
}

rb_cfg_t config_ring_buffer()
{
    rb_cfg_t cfg = {
        .size = RING_BUFFER_SIZE,
        .mem  = RB_MEM_PSRAM,
    };
    return cfg;
}

mic_cfg_t config_mic()
{
    mic_cfg_t cfg = {
        .sck               = MIC_GPIO_SCK,
        .ws                = MIC_GPIO_WS,
        .sd                = MIC_GPIO_SD,
        .sample_rate       = MIC_SAMPLE_RATE,
        .dma_buf_count     = MIC_DMA_BUF_COUNT,
        .dma_buf_samples   = MIC_DMA_BUF_SAMPLES,
        .target_bits       = MIC_TARGET_BITS,
    };
    return cfg;
}

rec_cfg_t config_recorder(void)
{
    rec_cfg_t cfg = {
        .rb_cfg             = config_ring_buffer(),
        .mic_cfg            = config_mic(),
        .file_path          = "/sdcard/record.wav",
        .capture_priority   = RECORDER_CAPTURE_PRIORITY,
        .capture_stack_size = RECORDER_CAPTURE_STACK_SIZE,
        .record_priority    = RECORDER_RECORD_PRIORITY,
        .record_stack_size  = RECORDER_RECORD_STACK_SIZE,
    };
    return cfg;
}

