// button.c
#include "freertos/FreeRTOS.h"
//#include "freertos/task.h"
//#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#include "button.h"

static const char *TAG = "BUTTON";

static bool s_btn_initialized = false;
static btn_cfg_t         s_btn_cfg;
static SemaphoreHandle_t s_btn_semaphore       = NULL;
static TaskHandle_t      s_btn_task_handle     = NULL;
static uint32_t          s_press_duration_ms   = 0;
static uint32_t          s_press_start_time_ms = 0;

static uint32_t s_short_click_count = 0;

static btn_state_t s_btn_state = BUTTON_STATE_RELEASED;

// прототипы внутренних функций
static esp_err_t  gpio_cfg_init(const btn_cfg_t *cfg);
static BaseType_t create_btn_task();
static btn_err_t  btn_init(const btn_cfg_t *cfg);
static btn_err_t  btn_deinit();
static void btn_reset_resources();
static void btn_reset_state();
static void btn_cleanup();
static void btn_err_init_handler(btn_err_t err);
static void btn_err_delete_handler(btn_err_t err);



static void IRAM_ATTR btn_isr_handler(void *arg)
{
    if (s_btn_semaphore != NULL) {
        xSemaphoreGiveFromISR(s_btn_semaphore, NULL);
    }
}

bool btn_create(const btn_cfg_t *cfg)
{
    btn_err_t err = btn_init(cfg);
    btn_err_init_handler(err);
    return (err == BTN_OK);
}

bool btn_delete()
{
    btn_err_t err = btn_deinit();
    btn_err_delete_handler(err);
    return (err == BTN_OK);
}


static void set_btn_event_task(void *pvParameters)
{
    /* (void)pvParameters; */
    while (1) {
        switch (s_btn_state) {
            case BUTTON_STATE_RELEASED:
                {
                    if (xSemaphoreTake(s_btn_semaphore, portMAX_DELAY) != pdTRUE) break;

                    //антидребезг
                    vTaskDelay(pdMS_TO_TICKS(10));

                    if (btn_is_pressed()){
                        s_btn_state = BUTTON_STATE_PRESSED;
                        s_press_start_time_ms = esp_timer_get_time() / 1000;
                        s_press_duration_ms = 0;
                        s_btn_cfg.handler(BUTTON_EVENT_PRESS, s_press_duration_ms);
                        break;
                    }
                }
                break;

            case BUTTON_STATE_PRESSED:
                {
                    s_press_duration_ms = (esp_timer_get_time() / 1000) - s_press_start_time_ms;
                    if (!btn_is_pressed()) {
                        s_btn_state = BUTTON_STATE_RELEASED;
                        s_short_click_count++;
                        s_btn_cfg.handler(BUTTON_EVENT_SHORT_CLICK, s_press_duration_ms);
                        break;
                    }
                    
                    if (s_press_duration_ms >= s_btn_cfg.long_press_ms){
                        s_btn_state = BUTTON_STATE_LONG_PRESSED;
                        s_btn_cfg.handler(BUTTON_EVENT_LONG_PRESS, s_press_duration_ms);
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                break;

            case BUTTON_STATE_LONG_PRESSED:
                {
                    if (!btn_is_pressed()) {
                        s_btn_state = BUTTON_STATE_RELEASED;
                        s_press_duration_ms = (esp_timer_get_time() / 1000) - s_press_start_time_ms;
                        s_btn_cfg.handler(BUTTON_EVENT_LONG_CLICK, s_press_duration_ms);
                        break;
                    }
                    vTaskDelay(pdMS_TO_TICKS(10));
                }
                break;
                
            default: s_btn_state = BUTTON_STATE_RELEASED; break;
        }
    }
}


static btn_err_t btn_init(const btn_cfg_t *cfg)
{
	if (s_btn_initialized) return BTN_ERR_ALREADY_INIT;       // 1. Проверка, инициализирована ли уже кнопка
    if (cfg == NULL) return BTN_ERR_ARG;                      // 2. Проверка на ненулевой аргумент
    if (cfg->handler == NULL) return BTN_ERR_NO_HANDLER;      // 3. Проверка наличия обработчика кнопки

	s_btn_cfg = *cfg;
	if (gpio_cfg_init(&s_btn_cfg) != ESP_OK) return BTN_ERR_GPIO_CFG;  // 4. Настройка GPIO для кнопки
   
    s_btn_semaphore = xSemaphoreCreateBinary();                        // 5. Создаём бинарный семафор
    if (s_btn_semaphore == NULL) return BTN_ERR_SEMAPHORE;
	
	if (create_btn_task() != pdPASS) return BTN_ERR_TASK_CREATE;       // 6. Создаём задачу

	esp_err_t err_isr_service = gpio_install_isr_service(0);           // 7. Устанавливаем сервис для прерываний
	if (err_isr_service != ESP_OK && err_isr_service != ESP_ERR_INVALID_STATE) return BTN_ERR_ISR_SERVICE;

	esp_err_t err_isr_add = gpio_isr_handler_add(s_btn_cfg.gpio_num, btn_isr_handler, NULL); // 8. Добавляем обработчик прерываний
	if (err_isr_add != ESP_OK) return BTN_ERR_ISR_HANDLER;
	
	s_btn_initialized = true;
    return BTN_OK;
}

static btn_err_t btn_deinit()
{
    if (!s_btn_initialized) return BTN_ERR_NOT_INIT;
    gpio_isr_handler_remove(s_btn_cfg.gpio_num);
    btn_cleanup();
    return BTN_OK;
}


static esp_err_t gpio_cfg_init(const btn_cfg_t *cfg)
{
    gpio_config_t io_cfg = {
        .pin_bit_mask = (1ULL << cfg->gpio_num),
        .mode         = GPIO_MODE_INPUT,
        .pull_up_en   = (cfg->active_level == BUTTON_ACTIVE_LOW) ? GPIO_PULLUP_ENABLE : GPIO_PULLUP_DISABLE,
        .pull_down_en = (cfg->active_level == BUTTON_ACTIVE_LOW) ? GPIO_PULLDOWN_DISABLE : GPIO_PULLDOWN_ENABLE,
        .intr_type    = cfg->intr_type,
    };
    return gpio_config(&io_cfg);
}


static BaseType_t create_btn_task()
{
    return xTaskCreate(
        set_btn_event_task,           // 1. функция задачи
        "btn_evt_gen",                // 2. имя
        s_btn_cfg.task_stack_size,    // 3. стек
        NULL,                         // 4. параметр (не передаём)
        s_btn_cfg.task_priority,      // 5. приоритет
        &s_btn_task_handle            // 6. куда записать хэндл
    );
}

static void btn_cleanup()
{
    btn_reset_resources();
    btn_reset_state();
}

static void btn_reset_resources()
{
    if (s_btn_task_handle != NULL) { vTaskDelete(s_btn_task_handle); s_btn_task_handle = NULL; }
    if (s_btn_semaphore != NULL)   { vSemaphoreDelete(s_btn_semaphore); s_btn_semaphore = NULL; }
}

static void btn_reset_state()
{
    s_btn_state           = BUTTON_STATE_RELEASED;
    s_press_start_time_ms = 0;
    s_press_duration_ms   = 0;
    s_btn_initialized     = false;
}

bool btn_is_pressed()
{
    return (gpio_get_level(s_btn_cfg.gpio_num) == s_btn_cfg.active_level);
}


static void btn_err_init_handler(btn_err_t err)
{
	// Cleanup только для ошибок, возникших ПОСЛЕ создания ресурсов
    switch (err) {
        case BTN_ERR_TASK_CREATE:
        case BTN_ERR_ISR_SERVICE:
        case BTN_ERR_ISR_HANDLER: btn_cleanup(); break;
        default: break;
    }
	   
    switch (err) {
        case BTN_OK:               ESP_LOGI(TAG, "Button initialized on GPIO%d", s_btn_cfg.gpio_num); break;
        case BTN_ERR_ALREADY_INIT: ESP_LOGW(TAG, "btn_init: already initialized");                    break;
        case BTN_ERR_ARG:          ESP_LOGE(TAG, "btn_init: cfg == NULL");                            break;
        case BTN_ERR_NO_HANDLER:   ESP_LOGE(TAG, "btn_init: handler == NULL");                        break;
        case BTN_ERR_GPIO_CFG:     ESP_LOGE(TAG, "btn_init: gpio_config failed");                     break;
        case BTN_ERR_SEMAPHORE:    ESP_LOGE(TAG, "btn_init: semaphore creation failed");              break;
        case BTN_ERR_TASK_CREATE:  ESP_LOGE(TAG, "btn_init: task creation failed");                   break;
        case BTN_ERR_ISR_SERVICE:  ESP_LOGE(TAG, "btn_init: gpio_install_isr_service failed");        break;
        case BTN_ERR_ISR_HANDLER:  ESP_LOGE(TAG, "btn_init: gpio_isr_handler_add failed");            break;
        default:                   ESP_LOGE(TAG, "btn_init: unknown error %d", (int)err);             break;
    }
}

static void btn_err_delete_handler(btn_err_t err)
{
    switch (err) {
        case BTN_OK:           ESP_LOGI(TAG, "Button deleted on GPIO%d", s_btn_cfg.gpio_num); break;
        case BTN_ERR_NOT_INIT: ESP_LOGW(TAG, "btn_delete: not initialized");                  break;
        default:               ESP_LOGE(TAG, "btn_delete: unknown error %d", (int)err);       break;
    }
}
