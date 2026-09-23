// button.h
#ifndef BUTTON_H
#define BUTTON_H

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"
#include "driver/gpio.h"   // для gpio_int_type_t

typedef enum {
    BUTTON_ACTIVE_LOW = 0,
    BUTTON_ACTIVE_HIGH = 1
} btn_active_t;

// задаваемые пользователем параметры
#define BUTTON_GPIO_PIN         10
#define BUTTON_LONG_PRESS_MS    1500
#define BUTTON_INTR_TYPE        GPIO_INTR_NEGEDGE
#define BUTTON_ACTIVE_LEVEL     BUTTON_ACTIVE_LOW
#define BUTTON_TASK_PRIORITY    5                       // возможно уйдем от FreeRTOS, если перейти на прерывание + опрос состояния по таймеру
#define BUTTON_TASK_STACK_SIZE  4096                    // возможно уйдем от FreeRTOS, если перейти на прерывание + опрос состояния по таймеру

typedef enum {
    BUTTON_STATE_RELEASED,        // отпущена
    BUTTON_STATE_PRESSED,         // нажата, ещё не долго
    BUTTON_STATE_LONG_PRESSED,    // нажата долго
} btn_state_t;
typedef enum {
    BUTTON_EVENT_PRESS,            // кнопку нажали
    BUTTON_EVENT_SHORT_CLICK,      // кнопку нажали и отпустили
    BUTTON_EVENT_LONG_PRESS,       // кнопку нажали и держат
    BUTTON_EVENT_LONG_CLICK,       // кнопку нажали, держали нажатой а потом отпустили
} btn_event_t;

typedef struct {
    uint8_t gpio_num;             // номера GPIO на ESP32 могут быть от 0 до 45 (умещается с запасом в 1 байт(0...255))
    gpio_int_type_t intr_type;    // тип прерывания (например, по заднему фронту)
    btn_active_t active_level;    // активный уровень (например, если нажали на кнопку на пине будет 0, это активный уровень кнопки)
    uint16_t long_press_ms;       // 0…65 535 с запасом хватает на 65 секунд. в коде будет использоваться порог в 1,5 или 2 максимум секунды
    uint8_t task_priority;        // приоритет задачи обработки кнопки (обычно 5)                                                               // возможно уйдем от FreeRTOS, если перейти на прерывание + опрос состояния по таймеру
    uint16_t task_stack_size;     // размер стека задачи (байт), например 4096                                                                  // возможно уйдем от FreeRTOS, если перейти на прерывание + опрос состояния по таймеру
} btn_cfg_t;


// Коды ошибок кнопки
typedef enum {
    BTN_OK = 0,
    BTN_ERR_ALREADY_INIT,  // модуль уже инициализирован
    BTN_ERR_ARG,           // конфигурация равна NULL или невалидные параметры
    BTN_ERR_GPIO_CFG,      // gpio_config() упал
    BTN_ERR_SEMAPHORE,     // не удалось создать семафор
    BTN_ERR_TASK_CREATE,   // xTaskCreate() упал
    BTN_ERR_ISR_SERVICE,   // gpio_install_isr_service() упал
    BTN_ERR_ISR_HANDLER,   // gpio_isr_handler_add() упал
    BTN_ERR_NOT_INIT,      // удаление кнопки вызвано раньше инициализации
} btn_err_t;


btn_err_t btn_create(const btn_cfg_t *cfg);
btn_err_t btn_delete(void);

//void btn_event_handler(btn_event_t event, uint32_t duration_ms);

bool        btn_is_pressed(void);

#endif // BUTTON_H