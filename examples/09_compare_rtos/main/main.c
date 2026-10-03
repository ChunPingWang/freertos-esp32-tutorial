/*
 * 範例 09：總結比較 —— FreeRTOS 版
 *
 * 與範例 08 (超級迴圈版) 是「同一個應用」的兩種寫法，功能與量測方式完全相同：
 *   - LED：每 100 ms 切換一次，量測「實際間隔與 100 ms 的誤差」
 *   - 重運算：每 1 秒佔用 CPU 200 ms
 *   - 按鈕：由 ISR 記下按下的瞬間，量測「按下 -> 任務開始處理」的延遲
 *   - 每 5 秒印出統計
 *
 * 所有任務都綁在核心 0（與範例 08 的主迴圈相同），
 * 確保差異來自「架構」，而不是多用了一顆核心。
 */
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LED_GPIO          GPIO_NUM_2
#define BUTTON_GPIO       GPIO_NUM_0
#define DEMO_CORE         0

#define LED_PERIOD_MS     100
#define HEAVY_PERIOD_MS   1000
#define HEAVY_WORK_MS     200
#define REPORT_PERIOD_MS  5000
#define DEBOUNCE_US       200000

/* 優先權：越需要即時反應的越高 */
#define PRIO_BUTTON  4
#define PRIO_LED     3
#define PRIO_REPORT  2
#define PRIO_HEAVY   1

static const char *TAG = "rtos";

static QueueHandle_t s_button_q;

/* 統計資料：各欄位只有一個任務會寫入，且為 32 位元，report_task 直接讀取即可 */
static volatile uint32_t s_led_error_max_us;
static volatile uint32_t s_button_count;
static volatile uint32_t s_latency_last_us;
static volatile uint32_t s_latency_max_us;

static uint32_t micros(void)
{
    return (uint32_t)esp_timer_get_time();
}

static void button_isr(void *arg)
{
    static uint32_t last_us;
    uint32_t now = micros();
    BaseType_t higher_prio_task_woken = pdFALSE;

    if (now - last_us < DEBOUNCE_US) {
        return;
    }
    last_us = now;

    xQueueSendFromISR(s_button_q, &now, &higher_prio_task_woken);
    if (higher_prio_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void button_task(void *arg)
{
    uint32_t press_time_us;

    while (1) {
        xQueueReceive(s_button_q, &press_time_us, portMAX_DELAY);
        uint32_t latency = micros() - press_time_us;

        s_button_count++;
        s_latency_last_us = latency;
        if (latency > s_latency_max_us) {
            s_latency_max_us = latency;
        }
    }
}

static void led_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t prev_toggle_us = micros();
    int level = 0;

    while (1) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(LED_PERIOD_MS));

        uint32_t now = micros();
        level = !level;
        gpio_set_level(LED_GPIO, level);

        int32_t error = (int32_t)(now - prev_toggle_us) - LED_PERIOD_MS * 1000;
        uint32_t abs_error = error < 0 ? -error : error;
        if (abs_error > s_led_error_max_us) {
            s_led_error_max_us = abs_error;
        }
        prev_toggle_us = now;
    }
}

static void heavy_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    volatile uint32_t x = 1;

    while (1) {
        uint32_t start = micros();
        while (micros() - start < HEAVY_WORK_MS * 1000) {
            x = x * 1664525u + 1013904223u;
        }
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(HEAVY_PERIOD_MS));
    }
}

static void report_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(REPORT_PERIOD_MS));
        ESP_LOGI(TAG, "LED error max=%" PRIu32 " us | button count=%" PRIu32
                 " latency last=%" PRIu32 " us max=%" PRIu32 " us",
                 s_led_error_max_us, s_button_count,
                 s_latency_last_us, s_latency_max_us);
        s_led_error_max_us = 0;
    }
}

static void board_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    gpio_config_t button_cfg = {
        .pin_bit_mask = 1ULL << BUTTON_GPIO,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_NEGEDGE,
    };
    gpio_config(&button_cfg);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 09 compare: FreeRTOS ===");

    s_button_q = xQueueCreate(10, sizeof(uint32_t));
    configASSERT(s_button_q);

    board_init();

    xTaskCreatePinnedToCore(button_task, "button", 4096, NULL, PRIO_BUTTON, NULL, DEMO_CORE);
    xTaskCreatePinnedToCore(led_task,    "led",    4096, NULL, PRIO_LED,    NULL, DEMO_CORE);
    xTaskCreatePinnedToCore(report_task, "report", 4096, NULL, PRIO_REPORT, NULL, DEMO_CORE);
    xTaskCreatePinnedToCore(heavy_task,  "heavy",  4096, NULL, PRIO_HEAVY,  NULL, DEMO_CORE);

    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);
}
