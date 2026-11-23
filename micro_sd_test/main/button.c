// button.c
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "button.h"

static const char *TAG = "BUTTON";


/* Внутреннее состояние одной кнопки */
typedef struct {
    bool         btn_created;
    btn_cfg_t    cfg;
    btn_state_t  state;
    uint32_t     press_duration_ms;
    uint32_t     press_start_time_ms;
} btn_t;

// параметры менеджера кнопок
static bool           s_initialized  = false;
static btn_t          s_btns[BTN_MAX_COUNT]; //массив кнопок
static btn_event_cb_t s_handler      = NULL;
static TaskHandle_t   s_btn_manager_task_handle  = NULL;


/* Прототипы */
static btn_err_t btn_manager_init(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size);
static btn_err_t btn_manager_deinit();
static btn_err_t btn_add(const btn_cfg_t *cfg);
static bool      btn_configure_gpio(const btn_cfg_t *cfg);

static bool      btn_any_active();
static void      btn_fsm(btn_t *btn, btn_id_t id);
static void      btn_manager_task(void *pvParameters);
static void      btn_cleanup();
static void      btn_reset_state();
static void      btn_err_init_handler(btn_err_t err);
static void      btn_err_deinit_handler(btn_err_t err);
static void      btn_err_add_handler(btn_err_t err, btn_id_t id);

// ISR — общий на все кнопки. Не разбирается, какая кнопка сработала: просто будит задачу. Работает или по нижнему или по верхнему фронту (в зависимости от того какой активный уровень у кнопки).
static void IRAM_ATTR btn_isr_handler(void *arg)
{
    if (s_btn_manager_task_handle == NULL) return;

    BaseType_t hp_woken = pdFALSE;
    vTaskNotifyGiveFromISR(s_btn_manager_task_handle, &hp_woken);
    portYIELD_FROM_ISR(hp_woken);
}



// Публичные функци
// назначение обработчика кнопок, создание задачи, взадание стека под нее и задание ей приоритета
bool btn_manager_create(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size) 
{
    btn_err_t err = btn_manager_init(handler, priority, stack_size);
    btn_err_init_handler(err);
    return (err == BTN_OK);
}

bool btn_manager_delete()
{
    btn_err_t err = btn_manager_deinit();
    btn_err_deinit_handler(err);
    return (err == BTN_OK);
}

bool btn_create(const btn_cfg_t *cfg)
{
    btn_err_t err = btn_add(cfg);
    btn_err_add_handler(err, cfg ? cfg->id : BTN_ID_INVALID);
    return (err == BTN_OK);
}

bool btn_is_pressed(btn_id_t id)
{
    if (id >= BTN_MAX_COUNT || !s_btns[id].btn_created) return false;
    return gpio_get_level(s_btns[id].cfg.gpio_num) == s_btns[id].cfg.active_level;
}




/* Внутренние функции*/
static btn_err_t btn_manager_init(btn_event_cb_t handler, uint8_t priority, uint16_t stack_size)
{
    if (s_initialized)   return BTN_ERR_ALREADY_INIT;
    if (handler == NULL) return BTN_ERR_NO_HANDLER;

    esp_err_t err = gpio_install_isr_service(0);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) return BTN_ERR_ISR_SERVICE;

    s_handler = handler;

    if (xTaskCreate(btn_manager_task, "btn_mgr", stack_size, NULL, priority, &s_btn_manager_task_handle) != pdPASS) {
        s_handler = NULL;
        return BTN_ERR_TASK_CREATE;
    }

    s_initialized = true;
    return BTN_OK;
}

static btn_err_t btn_manager_deinit(void)
{
    if (!s_initialized) return BTN_ERR_NOT_INIT;
    btn_cleanup();
    btn_reset_state();
    return BTN_OK;
}

static btn_err_t btn_add(const btn_cfg_t *cfg)
{
    if (!s_initialized)                 return BTN_ERR_NOT_INIT;
    if (cfg == NULL)                    return BTN_ERR_ARG;
    if (cfg->id >= BTN_MAX_COUNT)       return BTN_ERR_ID_INVALID;
    if (s_btns[cfg->id].btn_created)    return BTN_ERR_ID_IN_USE;
    if (!btn_configure_gpio(cfg))             return BTN_ERR_GPIO_CFG;
    if (gpio_isr_handler_add(cfg->gpio_num, btn_isr_handler, NULL) != ESP_OK) return BTN_ERR_ISR_HANDLER;

    s_btns[cfg->id] = (btn_t){
        .cfg         = *cfg,
        .state       = BUTTON_STATE_RELEASED,
        .btn_created = true,
    };

    return BTN_OK;
}

static bool btn_configure_gpio(const btn_cfg_t *cfg)
{
    const bool active_low = (cfg->active_level == BUTTON_ACTIVE_LOW);
    gpio_config_t io = {
        .pin_bit_mask = (1ULL << cfg->gpio_num),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = active_low ? GPIO_PULLUP_ENABLE    : GPIO_PULLUP_DISABLE,
        .pull_down_en = active_low ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type    = active_low ? GPIO_INTR_NEGEDGE     : GPIO_INTR_POSEDGE,
    };
    return (gpio_config(&io) == ESP_OK);
}


// Задача: один цикл на все кнопки
#define POLLING_TIME (10) // пока кнопка зажата(активна) проверяем состояние кнопки каждые t = POLLING_TIME (мс) для того чтобы поймать момент отпускания.   // можно попробовать заменить таймерами
#define DEBOUSE_TIME (10) // нажатие кнопки фиксируем по прерыванию (с прохождением антидребезга, считаем что время дребезга контактов = DEBOUSE_TIME (мс) )

static void btn_manager_task(void *pvParameters)
{
    while (1) {
        TickType_t sleep_ticks = btn_any_active() ? pdMS_TO_TICKS(POLLING_TIME) : portMAX_DELAY;
        bool woke_by_isr = (ulTaskNotifyTake(pdTRUE, sleep_ticks) > 0);

        if (woke_by_isr) {vTaskDelay(pdMS_TO_TICKS(DEBOUSE_TIME));}

        for (size_t i = 0; i < BTN_MAX_COUNT; i++) {
            if (s_btns[i].btn_created) {
                btn_fsm(&s_btns[i], (btn_id_t)i);
            }
        }
    }
}

static bool btn_any_active()
{
    for (size_t i = 0; i < BTN_MAX_COUNT; i++) {
        if (s_btns[i].btn_created && s_btns[i].state != BUTTON_STATE_RELEASED) {
            return true;
        }
    }
    return false;
}





// конечный аппарат состояний для кнопки (fsm - Finite State Machine)
static void btn_fsm(btn_t *btn, btn_id_t id)
{
    bool pressed = (gpio_get_level(btn->cfg.gpio_num) == btn->cfg.active_level);

    switch (btn->state) {
        case BUTTON_STATE_RELEASED:
            if (pressed) {
                btn->state               = BUTTON_STATE_PRESSED;
                btn->press_start_time_ms = esp_timer_get_time() / 1000;
                btn->press_duration_ms   = 0;
                s_handler(id, BUTTON_EVENT_PRESS, 0);
            }
            break;

        case BUTTON_STATE_PRESSED:
            btn->press_duration_ms = (esp_timer_get_time() / 1000) - btn->press_start_time_ms;
            if (!pressed) {
                btn->state = BUTTON_STATE_RELEASED;
                s_handler(id, BUTTON_EVENT_SHORT_CLICK, btn->press_duration_ms);
            }

            if (pressed && btn->press_duration_ms >= btn->cfg.long_press_ms) {
                btn->state = BUTTON_STATE_LONG_PRESSED;
                s_handler(id, BUTTON_EVENT_LONG_PRESS, btn->press_duration_ms);
            }
            break;

        case BUTTON_STATE_LONG_PRESSED:
            if (!pressed) {
                btn->state             = BUTTON_STATE_RELEASED;
                btn->press_duration_ms = (esp_timer_get_time() / 1000) - btn->press_start_time_ms;
                s_handler(id, BUTTON_EVENT_LONG_CLICK, btn->press_duration_ms);
            }
            break;
    }
}





/* Cleanup */
static void btn_cleanup(void)
{
    for (size_t i = 0; i < BTN_MAX_COUNT; i++) {
        if (s_btns[i].btn_created) {
            gpio_isr_handler_remove(s_btns[i].cfg.gpio_num);
            s_btns[i].btn_created = false;
        }
    }
    if (s_btn_manager_task_handle != NULL) {
        vTaskDelete(s_btn_manager_task_handle);
        s_btn_manager_task_handle = NULL;
    }
}

static void btn_reset_state(void)
{
    for (size_t i = 0; i < BTN_MAX_COUNT; i++) {
        s_btns[i].state               = BUTTON_STATE_RELEASED;
        s_btns[i].press_duration_ms   = 0;
        s_btns[i].press_start_time_ms = 0;
    }
    s_handler     = NULL;
    s_initialized = false;
}







static void btn_err_init_handler(btn_err_t err)
{
    switch (err) {
        case BTN_OK:               ESP_LOGI(TAG, "Manager initialized");                    break;
        case BTN_ERR_ALREADY_INIT: ESP_LOGW(TAG, "init: already initialized");              break;
        case BTN_ERR_NO_HANDLER:   ESP_LOGE(TAG, "init: handler == NULL");                  break;
        case BTN_ERR_ISR_SERVICE:  ESP_LOGE(TAG, "init: gpio_install_isr_service failed");  break;
        case BTN_ERR_TASK_CREATE:  ESP_LOGE(TAG, "init: task creation failed");             break;
        default:                   ESP_LOGE(TAG, "init: unknown error %d", (int)err);       break;
    }
}

static void btn_err_deinit_handler(btn_err_t err)
{
    switch (err) {
        case BTN_OK:           ESP_LOGI(TAG, "Manager deinitialized");           break;
        case BTN_ERR_NOT_INIT: ESP_LOGW(TAG, "deinit: not initialized");         break;
        default:               ESP_LOGE(TAG, "deinit: unknown error %d", (int)err); break;
    }
}

static void btn_err_add_handler(btn_err_t err, btn_id_t id)
{
    switch (err) {
        case BTN_OK:              ESP_LOGI(TAG, "Button added (id=%u)", id);                     break;
        case BTN_ERR_NOT_INIT:    ESP_LOGE(TAG, "add: manager not initialized");                 break;
        case BTN_ERR_ARG:         ESP_LOGE(TAG, "add: cfg == NULL");                             break;
        case BTN_ERR_ID_INVALID:  ESP_LOGE(TAG, "add: id %u >= BTN_MAX_COUNT(%u)", id, BTN_MAX_COUNT); break;
        case BTN_ERR_ID_IN_USE:   ESP_LOGE(TAG, "add: id %u already in use", id);                break;
        case BTN_ERR_GPIO_CFG:    ESP_LOGE(TAG, "add: gpio_config failed (id=%u)", id);          break;
        case BTN_ERR_ISR_HANDLER: ESP_LOGE(TAG, "add: gpio_isr_handler_add failed (id=%u)", id); break;
        default:                  ESP_LOGE(TAG, "add: unknown error %d", (int)err);              break;
    }
}