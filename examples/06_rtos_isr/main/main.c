/*
 * 範例 06：中斷 + 延遲處理 (deferred interrupt handling)
 *
 * 按鈕改用 GPIO 中斷偵測。ISR 只做兩件事：記下時間、把時間丟進佇列。
 * 真正的處理（去彈跳、切換 LED、印訊息）交給高優先權的 handler_task。
 *
 * 另外有一個低優先權的 load_task 幾乎一直佔著 CPU，
 * 用來證明：不論系統多忙，按鈕的反應時間都是微秒等級。
 */
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/queue.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"

#define LED_GPIO     GPIO_NUM_2
#define BUTTON_GPIO  GPIO_NUM_0
#define DEMO_CORE    0
#define DEBOUNCE_US  200000

static const char *TAG = "ex06";

static QueueHandle_t s_button_q;

/*
 * ISR 的規則：
 *   - 越短越好，不可以阻塞（不能 vTaskDelay、不能拿 mutex、不要 printf / ESP_LOG）
 *   - 只能呼叫名稱以 FromISR 結尾的 FreeRTOS API
 */
static void button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    BaseType_t higher_prio_task_woken = pdFALSE;

    xQueueSendFromISR(s_button_q, &now, &higher_prio_task_woken);

    /* 若喚醒了比「被中斷的任務」更高優先權的任務，離開 ISR 時立刻切換過去 */
    if (higher_prio_task_woken) {
        portYIELD_FROM_ISR();
    }
}

static void handler_task(void *arg)
{
    int64_t isr_time_us;
    int64_t last_accepted_us = 0;
    uint32_t press_count = 0;
    int level = 0;

    while (1) {
        xQueueReceive(s_button_q, &isr_time_us, portMAX_DELAY);
        int64_t latency_us = esp_timer_get_time() - isr_time_us;

        /* 去彈跳：機械按鈕一次按壓會產生多個邊緣，200 ms 內只認第一個 */
        if (isr_time_us - last_accepted_us < DEBOUNCE_US) {
            continue;
        }
        last_accepted_us = isr_time_us;

        level = !level;
        gpio_set_level(LED_GPIO, level);
        press_count++;
        ESP_LOGI(TAG, "press #%" PRIu32 ": ISR -> task latency = %lld us",
                 press_count, (long long)latency_us);
    }
}

/* 背景負載：每秒有 800 ms 在空轉，留 200 ms 讓 idle task 餵看門狗 */
static void load_task(void *arg)
{
    volatile uint32_t x = 1;

    while (1) {
        int64_t end = esp_timer_get_time() + 800 * 1000;
        while (esp_timer_get_time() < end) {
            x = x * 1664525u + 1013904223u;
        }
        vTaskDelay(pdMS_TO_TICKS(200));
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
        .intr_type = GPIO_INTR_NEGEDGE,   /* 按下 = 高 -> 低 */
    };
    gpio_config(&button_cfg);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 06 interrupt + deferred handling ===");

    s_button_q = xQueueCreate(10, sizeof(int64_t));
    configASSERT(s_button_q);

    board_init();

    xTaskCreatePinnedToCore(handler_task, "handler", 4096, NULL, 5, NULL, DEMO_CORE);
    xTaskCreatePinnedToCore(load_task,    "load",    2048, NULL, 1, NULL, DEMO_CORE);

    /* 佇列與任務都就緒後才開啟中斷，避免 ISR 用到還沒建立的物件 */
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);
}
