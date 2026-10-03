/*
 * 範例 07：軟體計時器 (software timer) 與任務通知 (task notification)
 *
 * 做一個「樓梯間感應燈」：
 *   - 按下按鈕 -> LED 亮起，3 秒後自動熄滅
 *   - 3 秒內再按一次 -> 重新計時
 *
 * 用到的機制：
 *   - 任務通知：ISR 直接喚醒 button_task，比佇列／號誌更輕量
 *   - one-shot 計時器：負責 3 秒後關燈
 *   - auto-reload 計時器：每 2 秒印一次 heartbeat
 *   - monitor_task：示範如何檢查各任務的堆疊餘量與 heap
 */
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_log.h"

#define LED_GPIO      GPIO_NUM_2
#define BUTTON_GPIO   GPIO_NUM_0
#define LED_ON_MS     3000
#define HEARTBEAT_MS  2000

static const char *TAG = "ex07";

static TaskHandle_t s_button_task;
static TaskHandle_t s_monitor_task;
static TimerHandle_t s_off_timer;
static TimerHandle_t s_heartbeat_timer;

static void button_isr(void *arg)
{
    BaseType_t higher_prio_task_woken = pdFALSE;

    vTaskNotifyGiveFromISR(s_button_task, &higher_prio_task_woken);
    if (higher_prio_task_woken) {
        portYIELD_FROM_ISR();
    }
}

/*
 * 計時器 callback 都在同一個 timer service task 裡執行，
 * 所以必須很快結束、不可以阻塞，否則會拖累所有計時器。
 */
static void off_timer_cb(TimerHandle_t timer)
{
    gpio_set_level(LED_GPIO, 0);
    ESP_LOGI(TAG, "timeout -> LED off");
}

static void heartbeat_cb(TimerHandle_t timer)
{
    static uint32_t count;

    count++;
    ESP_LOGI(TAG, "heartbeat %" PRIu32, count);
}

static void button_task(void *arg)
{
    while (1) {
        /* 睡到 ISR 通知為止；pdTRUE 表示醒來時把通知計數歸零 */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

        /* 去彈跳：等 30 ms 後確認按鈕仍然是按下的 */
        vTaskDelay(pdMS_TO_TICKS(30));
        if (gpio_get_level(BUTTON_GPIO) != 0) {
            continue;
        }

        gpio_set_level(LED_GPIO, 1);
        /* xTimerReset：計時器沒在跑就啟動，已經在跑就重新計時 */
        xTimerReset(s_off_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "button -> LED on for %d ms", LED_ON_MS);

        /* 丟掉這 30 ms 內彈跳累積的通知 */
        ulTaskNotifyTake(pdTRUE, 0);
    }
}

static void monitor_task(void *arg)
{
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        /* high water mark = 任務啟動以來堆疊「最少還剩多少」(bytes)，接近 0 就危險了 */
        ESP_LOGI(TAG, "stack free min: button=%u monitor=%u bytes, heap free=%" PRIu32 " bytes",
                 (unsigned)uxTaskGetStackHighWaterMark(s_button_task),
                 (unsigned)uxTaskGetStackHighWaterMark(s_monitor_task),
                 esp_get_free_heap_size());
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
    ESP_LOGI(TAG, "=== 07 software timer & task notification ===");

    board_init();

    /* 參數：名稱、週期、是否 auto-reload、timer ID、callback */
    s_off_timer = xTimerCreate("off", pdMS_TO_TICKS(LED_ON_MS), pdFALSE, NULL, off_timer_cb);
    s_heartbeat_timer = xTimerCreate("hb", pdMS_TO_TICKS(HEARTBEAT_MS), pdTRUE, NULL, heartbeat_cb);
    configASSERT(s_off_timer && s_heartbeat_timer);
    xTimerStart(s_heartbeat_timer, portMAX_DELAY);

    xTaskCreate(button_task,  "button",  4096, NULL, 5, &s_button_task);
    xTaskCreate(monitor_task, "monitor", 4096, NULL, 1, &s_monitor_task);

    /* s_button_task 有值之後才開啟中斷 */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);
}
