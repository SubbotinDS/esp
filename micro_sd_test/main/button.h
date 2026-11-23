// button.h
#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"


typedef uint8_t btn_id_t;
#define BTN_ID_INVALID   0xFF  // заведомо невалидный id
#define BTN_MAX_COUNT    8     // максимальное поддерживаемое число кнопок


typedef enum {
    BUTTON_ACTIVE_LOW  = 0,
    BUTTON_ACTIVE_HIGH = 1
} btn_active_t;

typedef enum {
    BUTTON_STATE_RELEASED,
    BUTTON_STATE_PRESSED,
    BUTTON_STATE_LONG_PRESSED,
} btn_state_t;

typedef enum {
    BUTTON_EVENT_PRESS,       // кнопка нажата (сразу после антидребезга)
    BUTTON_EVENT_SHORT_CLICK, // отпущена раньше long_press_ms
    BUTTON_EVENT_LONG_PRESS,  // удержание достигло long_press_ms (кнопка ещё нажата)
    BUTTON_EVENT_LONG_CLICK,  // отпущена после long_press_ms
} btn_event_t;



typedef void (*btn_event_cb_t)(btn_id_t id, btn_event_t event, uint32_t duration_ms); // Колбэк (один на все кнопки)

// внешняя конфигурация одной кнопки
typedef struct {
    btn_id_t        id;
    gpio_num_t      gpio_num;
    btn_active_t    active_level;
    uint16_t        long_press_ms;
} btn_cfg_t;

typedef enum {
    BTN_OK = 0,
    BTN_ERR_ALREADY_INIT,   // менеджер кнопок уже инициализирован
    BTN_ERR_ARG,            // невалидные аргументы конфигурации
    BTN_ERR_NO_HANDLER,     // не задан обработчик событий кнопок
    BTN_ERR_GPIO_CFG,       // ошибка настройки GPIO
    BTN_ERR_ISR_SERVICE,    // ошибка установки сервиса прерываний
    BTN_ERR_ISR_HANDLER,    // ошибка регистрации обработчика прерываний
    BTN_ERR_TASK_CREATE,    // ошибка создания задачи
    BTN_ERR_ID_INVALID,     // невалидный ID кнопки
    BTN_ERR_ID_IN_USE,      // кнопка с таким ID уже добавлена
    BTN_ERR_GPIO_IN_USE,    // GPIO уже используется другой кнопкой
    BTN_ERR_NOT_INIT,       // работа c кнопкой при неинициализированным менеджере кнпок
} btn_err_t;

bool     btn_manager_create(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size);
bool     btn_manager_delete(void);
bool     btn_create(const btn_cfg_t *cfg);


bool     btn_is_pressed(btn_id_t id); // не использую?

#endif // BUTTON_H


