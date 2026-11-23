#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "driver/gpio.h"

#define BTN_ID_INVALID   0xFF
#define BTN_MAX_COUNT    8     // Максимальное количество кнопок

// ID кнопки используется для обращения к ней в менеджере (для максимального количества кнопок 8 шт достаточно 1 байтаы)
typedef uint8_t btn_id_t;


// Активный логический уровень GPIO при нажатии кнопки
typedef enum {
    BUTTON_ACTIVE_LOW = 0,     // Нажатие соответствует логическому 0
    BUTTON_ACTIVE_HIGH         // Нажатие соответствует логической 1
} btn_active_level_t;


// Состояния конечного автомата одной кнопки
typedef enum {
    BUTTON_STATE_RELEASED,     // Кнопка отпущена
    BUTTON_STATE_PRESSED,      // Кнопка нажата, ожидание отпускания или LONG_PRESS
    BUTTON_STATE_LONG_PRESSED  // Достигнуто время длительного нажатия
} btn_state_t;


// События кнопки
typedef enum {
    BUTTON_EVENT_PRESS,        // Зафиксировано нажатие
    BUTTON_EVENT_SHORT_CLICK,  // Кнопка отпущена до достижения long_press_ms
    BUTTON_EVENT_LONG_PRESS,   // Достигнуто время long_press_ms, кнопка ещё удерживается
    BUTTON_EVENT_LONG_CLICK    // Кнопка отпущена после длительного удержания
} btn_event_t;


// Конфигурация одной кнопки
typedef struct {
    btn_id_t           id;             // Уникальный ID кнопки
    gpio_num_t         gpio_num;       // Номер GPIO
    btn_active_level_t active_level;   // Активный уровень GPIO
    uint16_t           long_press_ms;  // Время определения длительного нажатия
} btn_cfg_t;


// Результаты создания менеджера и добавления кнопок
typedef enum {
    BTN_OK = 0,
    BTN_ERR_ALREADY_INIT,   // Менеджер кнопок уже инициализирован
    BTN_ERR_ARG,            // Некорректные аргументы конфигурации
    BTN_ERR_NO_HANDLER,     // Не задан обработчик событий кнопок
    BTN_ERR_GPIO_CFG,       // Ошибка настройки GPIO
    BTN_ERR_ISR_SERVICE,    // Ошибка установки сервиса прерываний
    BTN_ERR_ISR_HANDLER,    // Ошибка регистрации обработчика прерываний
    BTN_ERR_TASK_CREATE,    // Ошибка создания задачи менеджера
    BTN_ERR_ID_INVALID,     // Некорректный ID кнопки
    BTN_ERR_ID_IN_USE,      // Кнопка с таким ID уже создана
    BTN_ERR_GPIO_IN_USE,    // GPIO уже назначен другой кнопке
    BTN_ERR_NOT_INIT        // Менеджер кнопок ещё не создан
} btn_err_t;


// Callback, через который приложение получает события всех кнопок
typedef void (*btn_event_cb_t)(btn_id_t id, btn_event_t event, uint32_t duration_ms);


// Публичный интерфейс
bool btn_manager_create(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size);
bool btn_create(const btn_cfg_t *cfg);

#endif // BUTTON_H