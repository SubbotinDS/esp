// button.c
#include <stddef.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_attr.h"

#include "button.h"

static const char *TAG = "BUTTON";

#define BUTTON_POLLING_TIME_MS    10  // Интервал проверки удерживаемых кнопок
#define BUTTON_DEBOUNCE_TIME_MS   10  // Задержка для подавления дребезга контактов


// Внутреннее состояние одной кнопки
typedef struct {
    bool created;
    btn_cfg_t cfg;
    btn_state_t state;
    uint32_t press_duration_ms;
    uint32_t press_start_time_ms;
} btn_t;


// Общие данные менеджера кнопок
static btn_t s_buttons[BTN_MAX_COUNT];               // Массив зарегистрированных кнопок
static btn_event_cb_t s_event_handler = NULL;        // Обработчик событий приложения
static TaskHandle_t s_manager_task_handle = NULL;    // Дескриптор задачи менеджера


// Прототипы внутренних функций
static btn_err_t btn_manager_init(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size);
static btn_err_t btn_add(const btn_cfg_t *cfg);
static bool btn_configure_gpio(const btn_cfg_t *cfg);

static bool btn_any_active(void);
static void btn_fsm(btn_t *btn, btn_id_t id);
static void btn_manager_task(void *pvParameters);

static void btn_err_init_handler(btn_err_t err);
static void btn_err_add_handler(btn_err_t err, btn_id_t id);


// Общий ISR для всех кнопок: только отправляет уведомление задаче менеджера
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    (void)arg;

    if (s_manager_task_handle == NULL) return;

    BaseType_t higher_priority_task_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_manager_task_handle, &higher_priority_task_woken);
    portYIELD_FROM_ISR(higher_priority_task_woken);
}


// Публичные функции

// Создание менеджера и назначение обработчика событий приложения
bool btn_manager_create(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size)
{
    btn_err_t err = btn_manager_init(handler, priority, stack_size);
    btn_err_init_handler(err);
    return err == BTN_OK;
}


// Создание и регистрация отдельной кнопки
bool btn_create(const btn_cfg_t *cfg)
{
    btn_err_t err = btn_add(cfg);
    btn_err_add_handler(err, cfg ? cfg->id : BTN_ID_INVALID);
    return err == BTN_OK;
}


// Создание менеджера кнопок
static btn_err_t btn_manager_init(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size)
{
    if (s_manager_task_handle != NULL) return BTN_ERR_ALREADY_INIT;
    if (handler == NULL) return BTN_ERR_NO_HANDLER;

    // Установка глобального сервиса GPIO-прерываний
    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return BTN_ERR_ISR_SERVICE;

    s_event_handler = handler;

    // Создание единственной задачи, обслуживающей все кнопки
    if (xTaskCreate(btn_manager_task, "btn_mgr", stack_size, NULL, priority, &s_manager_task_handle) != pdPASS) {
        s_event_handler = NULL;
        s_manager_task_handle = NULL;

        // Откат установки сервиса, если он был создан этой функцией
        if (err == ESP_OK) gpio_uninstall_isr_service();

        return BTN_ERR_TASK_CREATE;
    }

    return BTN_OK;
}


// Добавление кнопки в менеджер
static btn_err_t btn_add(const btn_cfg_t *cfg)
{
    if (s_manager_task_handle == NULL) return BTN_ERR_NOT_INIT;
    if (cfg == NULL) return BTN_ERR_ARG;
    if (cfg->id >= BTN_MAX_COUNT) return BTN_ERR_ID_INVALID;
    if (s_buttons[cfg->id].created) return BTN_ERR_ID_IN_USE;
    if (cfg->active_level != BUTTON_ACTIVE_LOW && cfg->active_level != BUTTON_ACTIVE_HIGH) return BTN_ERR_ARG;

    // Проверка, что один GPIO не назначен нескольким кнопкам
    for (size_t i = 0; i < BTN_MAX_COUNT; i++) {
        if (s_buttons[i].created && s_buttons[i].cfg.gpio_num == cfg->gpio_num) return BTN_ERR_GPIO_IN_USE;
    }

    if (!btn_configure_gpio(cfg)) return BTN_ERR_GPIO_CFG;
    if (gpio_isr_handler_add(cfg->gpio_num, btn_isr_handler, NULL) != ESP_OK) return BTN_ERR_ISR_HANDLER;

    // Сохранение конфигурации и начального состояния кнопки
    s_buttons[cfg->id] = (btn_t){
        .created = true,
        .cfg = *cfg,
        .state = BUTTON_STATE_RELEASED
    };

    // Проверка кнопки после регистрации, даже если она уже нажата
    xTaskNotifyGive(s_manager_task_handle);

    return BTN_OK;
}


// Настройка GPIO с учётом активного уровня кнопки
static bool btn_configure_gpio(const btn_cfg_t *cfg)
{
    bool active_low = cfg->active_level == BUTTON_ACTIVE_LOW;

    gpio_config_t io = {
        .pin_bit_mask = (1ULL << cfg->gpio_num),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = active_low ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type = active_low ? GPIO_INTR_NEGEDGE : GPIO_INTR_POSEDGE
    };

    return gpio_config(&io) == ESP_OK;
}


// Проверка, удерживается ли хотя бы одна зарегистрированная кнопка
static bool btn_any_active(void)
{
    for (size_t i = 0; i < BTN_MAX_COUNT; i++) {
        if (s_buttons[i].created && s_buttons[i].state != BUTTON_STATE_RELEASED) return true;
    }

    return false;
}


// Задача менеджера: последовательно обрабатывает все зарегистрированные кнопки
static void btn_manager_task(void *pvParameters)
{
    (void)pvParameters;

    while (1) {
        // При удержании кнопки используем периодическую проверку, иначе ждём ISR
        TickType_t wait_time = btn_any_active() ? pdMS_TO_TICKS(BUTTON_POLLING_TIME_MS) : portMAX_DELAY;
        bool woke_by_isr = ulTaskNotifyTake(pdTRUE, wait_time) > 0;

        // Задержка после прерывания для подавления дребезга контактов
        if (woke_by_isr) vTaskDelay(pdMS_TO_TICKS(BUTTON_DEBOUNCE_TIME_MS));

        // Обработка каждой зарегистрированной кнопки
        for (size_t i = 0; i < BTN_MAX_COUNT; i++) {
            if (s_buttons[i].created) btn_fsm(&s_buttons[i], (btn_id_t)i);
        }
    }
}


// Конечный автомат одной кнопки
static void btn_fsm(btn_t *btn, btn_id_t id)
{
    bool pressed = gpio_get_level(btn->cfg.gpio_num) == btn->cfg.active_level;
    uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);

    switch (btn->state) {

        case BUTTON_STATE_RELEASED:
            if (pressed) {
                btn->state = BUTTON_STATE_PRESSED;
                btn->press_start_time_ms = now_ms;
                btn->press_duration_ms = 0;
                s_event_handler(id, BUTTON_EVENT_PRESS, 0);
            }
            break;

        case BUTTON_STATE_PRESSED:
            btn->press_duration_ms = now_ms - btn->press_start_time_ms;

            if (!pressed) {
                btn->state = BUTTON_STATE_RELEASED;
                s_event_handler(id, BUTTON_EVENT_SHORT_CLICK, btn->press_duration_ms);
            }
            else if (btn->press_duration_ms >= btn->cfg.long_press_ms) {
                btn->state = BUTTON_STATE_LONG_PRESSED;
                s_event_handler(id, BUTTON_EVENT_LONG_PRESS, btn->press_duration_ms);
            }
            break;

        case BUTTON_STATE_LONG_PRESSED:
            if (!pressed) {
                btn->state = BUTTON_STATE_RELEASED;
                btn->press_duration_ms = now_ms - btn->press_start_time_ms;
                s_event_handler(id, BUTTON_EVENT_LONG_CLICK, btn->press_duration_ms);
            }
            break;
    }
}


// Обработка результата создания менеджера
static void btn_err_init_handler(btn_err_t err)
{
    switch (err) {
        case BTN_OK:               ESP_LOGI(TAG, "Manager initialized"); break;
        case BTN_ERR_ALREADY_INIT: ESP_LOGW(TAG, "Manager already initialized"); break;
        case BTN_ERR_NO_HANDLER:   ESP_LOGE(TAG, "Manager: handler == NULL"); break;
        case BTN_ERR_ISR_SERVICE:  ESP_LOGE(TAG, "Manager: ISR service installation failed"); break;
        case BTN_ERR_TASK_CREATE:  ESP_LOGE(TAG, "Manager: task creation failed"); break;
        default:                   ESP_LOGE(TAG, "Manager: unknown error %d", (int)err); break;
    }
}


// Обработка результата создания кнопки
static void btn_err_add_handler(btn_err_t err, btn_id_t id)
{
    switch (err) {
        case BTN_OK:              ESP_LOGI(TAG, "Button added (id=%u)", (unsigned)id); break;
        case BTN_ERR_NOT_INIT:    ESP_LOGE(TAG, "Button: manager not initialized"); break;
        case BTN_ERR_ARG:         ESP_LOGE(TAG, "Button: invalid configuration"); break;
        case BTN_ERR_ID_INVALID:  ESP_LOGE(TAG, "Button: invalid id %u", (unsigned)id); break;
        case BTN_ERR_ID_IN_USE:   ESP_LOGE(TAG, "Button: id %u already in use", (unsigned)id); break;
        case BTN_ERR_GPIO_IN_USE: ESP_LOGE(TAG, "Button: GPIO already in use (id=%u)", (unsigned)id); break;
        case BTN_ERR_GPIO_CFG:    ESP_LOGE(TAG, "Button: GPIO configuration failed (id=%u)", (unsigned)id); break;
        case BTN_ERR_ISR_HANDLER: ESP_LOGE(TAG, "Button: ISR handler registration failed (id=%u)", (unsigned)id); break;
        default:                  ESP_LOGE(TAG, "Button: unknown error %d", (int)err); break;
    }
}