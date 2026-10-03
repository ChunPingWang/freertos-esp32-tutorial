/*
 * 範例 03：FreeRTOS 任務 (task)
 *
 * 與範例 01 相同的三件工作，各自變成一個獨立的任務：
 *   button_task  優先權 3：每 20 ms 檢查按鈕
 *   led_task     優先權 2：每 500 ms 切換 LED
 *   sensor_task  優先權 1：每秒做一次 300 ms 的「重運算」
 *
 * 觀察重點：sensor_task 佔用 CPU 的 300 ms 內，LED 與按鈕完全不受影響，
 *           因為排程器會讓高優先權任務「搶佔」低優先權任務。
 */
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LED_GPIO     GPIO_NUM_2
#define BUTTON_GPIO  GPIO_NUM_0

/* 全部任務綁在同一顆核心，才能清楚看到「搶佔」而不是「雙核各跑各的」 */
#define DEMO_CORE    0

static const char *TAG = "ex03";

static void board_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

/* 佔用 CPU 指定的時間，模擬濾波、加密、畫面更新等重運算 */
static void burn_cpu_ms(uint32_t ms)
{
    int64_t end = esp_timer_get_time() + (int64_t)ms * 1000;
    volatile uint32_t x = 1;

    while (esp_timer_get_time() < end) {
        x = x * 1664525u + 1013904223u;
    }
}

static void led_task(void *arg)
{
    TickType_t last_wake = xTaskGetTickCount();
    int level = 0;

    while (1) {
        level = !level;
        gpio_set_level(LED_GPIO, level);
        /* vTaskDelayUntil：以「上次喚醒時間」為基準，週期不會累積誤差 */
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));
    }
}

static void sensor_task(void *arg)
{
    int fake_value = 20;

    while (1) {
        burn_cpu_ms(300);
        fake_value = (fake_value + 1) % 100;
        ESP_LOGI(TAG, "sensor=%d", fake_value);
        /* vTaskDelay：任務進入 Blocked 狀態，CPU 讓給別人 */
        vTaskDelay(pdMS_TO_TICKS(700));
    }
}

static void button_task(void *arg)
{
    int last_level = 1;
    uint32_t press_count = 0;

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);
        if (level == 0 && last_level == 1) {
            press_count++;
            ESP_LOGI(TAG, "button pressed, count=%" PRIu32, press_count);
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(20));   /* 20 ms 取樣同時兼具去彈跳效果 */
    }
}

void app_main(void)
{
    board_init();
    ESP_LOGI(TAG, "=== 03 FreeRTOS tasks ===");

    /* 參數：任務函式、名稱、堆疊大小 (bytes)、參數、優先權、handle、核心 */
    xTaskCreatePinnedToCore(button_task, "button", 4096, NULL, 3, NULL, DEMO_CORE);
    xTaskCreatePinnedToCore(led_task,    "led",    2048, NULL, 2, NULL, DEMO_CORE);
    xTaskCreatePinnedToCore(sensor_task, "sensor", 4096, NULL, 1, NULL, DEMO_CORE);

    /* app_main 可以直接返回，ESP-IDF 會自動刪除 main task */
}
