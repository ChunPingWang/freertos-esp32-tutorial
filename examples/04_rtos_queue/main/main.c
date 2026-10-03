/*
 * 範例 04：佇列 (queue) —— 任務之間安全地傳遞資料
 *
 *   sensor_task --(sensor_q)--> logger_task      傳遞量測資料
 *   button_task --(led_cmd_q)--> led_task        傳遞「改變閃爍速度」的命令
 *
 * 觀察重點：
 *   - 任務之間沒有共用任何全域變數，只透過佇列溝通。
 *   - led_task 用 xQueueReceive 的 timeout 同時當作「閃爍週期」與「等命令」。
 */
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_random.h"
#include "esp_log.h"

#define LED_GPIO     GPIO_NUM_2
#define BUTTON_GPIO  GPIO_NUM_0

static const char *TAG = "ex04";

typedef struct {
    uint32_t seq;
    int      value;
    int64_t  timestamp_us;
} sensor_msg_t;

static QueueHandle_t s_sensor_q;
static QueueHandle_t s_led_cmd_q;

static void board_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

/* 生產者：每 500 ms 產生一筆資料 */
static void sensor_task(void *arg)
{
    sensor_msg_t msg = { 0 };

    while (1) {
        msg.seq++;
        msg.value = (int)(esp_random() % 100);
        msg.timestamp_us = esp_timer_get_time();

        /* 佇列是「複製」資料進去，msg 之後可以放心重複使用 */
        if (xQueueSend(s_sensor_q, &msg, 0) != pdTRUE) {
            ESP_LOGW(TAG, "sensor queue full, drop seq=%" PRIu32, msg.seq);
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

/* 消費者：沒資料時睡在佇列上，完全不佔 CPU */
static void logger_task(void *arg)
{
    sensor_msg_t msg;

    while (1) {
        if (xQueueReceive(s_sensor_q, &msg, portMAX_DELAY) == pdTRUE) {
            int64_t age_us = esp_timer_get_time() - msg.timestamp_us;
            ESP_LOGI(TAG, "seq=%" PRIu32 " value=%d (in queue for %lld us)",
                     msg.seq, msg.value, (long long)age_us);
        }
    }
}

/* 按一下按鈕，就送出下一個閃爍週期 */
static void button_task(void *arg)
{
    static const uint32_t periods_ms[] = { 500, 250, 100, 1000 };
    const size_t count = sizeof(periods_ms) / sizeof(periods_ms[0]);
    size_t index = 0;
    int last_level = 1;

    while (1) {
        int level = gpio_get_level(BUTTON_GPIO);
        if (level == 0 && last_level == 1) {
            index = (index + 1) % count;
            xQueueSend(s_led_cmd_q, &periods_ms[index], 0);
        }
        last_level = level;
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

static void led_task(void *arg)
{
    uint32_t period_ms = 500;
    uint32_t new_period_ms;
    int level = 0;

    while (1) {
        /* 等命令，最多等一個閃爍週期：
         *   收到命令 -> 更新週期
         *   逾時     -> 該切換 LED 了 */
        if (xQueueReceive(s_led_cmd_q, &new_period_ms,
                          pdMS_TO_TICKS(period_ms)) == pdTRUE) {
            period_ms = new_period_ms;
            ESP_LOGI(TAG, "LED period -> %" PRIu32 " ms", period_ms);
        } else {
            level = !level;
            gpio_set_level(LED_GPIO, level);
        }
    }
}

void app_main(void)
{
    board_init();
    ESP_LOGI(TAG, "=== 04 FreeRTOS queue ===");

    /* 參數：佇列長度、每個元素的大小 */
    s_sensor_q  = xQueueCreate(8, sizeof(sensor_msg_t));
    s_led_cmd_q = xQueueCreate(4, sizeof(uint32_t));
    configASSERT(s_sensor_q && s_led_cmd_q);

    xTaskCreate(button_task, "button", 4096, NULL, 3, NULL);
    xTaskCreate(led_task,    "led",    4096, NULL, 2, NULL);
    xTaskCreate(logger_task, "logger", 4096, NULL, 2, NULL);
    xTaskCreate(sensor_task, "sensor", 4096, NULL, 1, NULL);
}
