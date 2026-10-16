//config.h для того, что меняет разработчик
#ifndef CONFIG_H
#define CONFIG_H

#include "button.h"
#include "ring_buffer.h"
#include "recorder.h"
#include "driver/gpio.h"

// ─── Ring buffer ─────────────────────────────────────
#define RECORDER_RING_SIZE_KB  (512) // наверно по хорошему надо вынести в Kconfig.projbuild
#define RING_BUFFER_SIZE  (RECORDER_RING_SIZE_KB * 1024)  // значение в байтах, 512 КБ

// ─── Button ──────────────────────────────────────────
#define RECORDER_BUTTON_GPIO            (10) // наверно по хорошему надо вынести в Kconfig.projbuild
#define RECORDER_BUTTON_LONG_PRESS_MS (1500) // наверно по хорошему надо вынести в Kconfig.projbuild

#define BUTTON_GPIO_PIN          RECORDER_BUTTON_GPIO
#define BUTTON_LONG_PRESS_MS     RECORDER_BUTTON_LONG_PRESS_MS
#define BUTTON_TASK_PRIORITY     5
#define BUTTON_TASK_STACK_SIZE   4096

#define BUTTON_INTR_TYPE         GPIO_INTR_NEGEDGE
#define BUTTON_ACTIVE_LEVEL      BUTTON_ACTIVE_LOW

// ─── Recorder ────────────────────────────────────────
#define RECORDER_TASK_PRIORITY     5
#define RECORDER_TASK_STACK_SIZE   2048


btn_cfg_t config_button(btn_event_cb_t handler);
rb_cfg_t  config_ring_buffer();
rec_cfg_t config_recorder();

#endif