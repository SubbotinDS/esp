// config.c — сборка конфигураций из настроек
#include "config.h"

btn_cfg_t config_button(btn_event_cb_t handler)
{
    btn_cfg_t cfg = {
        .gpio_num        = BUTTON_GPIO_PIN,
        .intr_type       = BUTTON_INTR_TYPE,
        .active_level    = BUTTON_ACTIVE_LEVEL,
        .long_press_ms   = BUTTON_LONG_PRESS_MS,
        .task_priority   = BUTTON_TASK_PRIORITY,
        .task_stack_size = BUTTON_TASK_STACK_SIZE,
        .handler         = handler,
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


rec_cfg_t config_recorder()
{
    rec_cfg_t cfg = {
        .rb_cfg = config_ring_buffer(),
        .task_priority   = RECORDER_TASK_PRIORITY,
        .task_stack_size = RECORDER_TASK_STACK_SIZE,
    };
    return cfg;
}