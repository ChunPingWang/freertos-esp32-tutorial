/*
 * 範例 08：總結比較 —— 超級迴圈版
 *
 * 與範例 09 (FreeRTOS 版) 是「同一個應用」的兩種寫法，功能與量測方式完全相同：
 *   - LED：每 100 ms 切換一次，量測「實際間隔與 100 ms 的誤差」
 *   - 重運算：每 1 秒佔用 CPU 200 ms
 *   - 按鈕：由 ISR 記下按下的瞬間，量測「按下 -> 主程式開始處理」的延遲
 *   - 每 5 秒印出統計
 *
 * 這是實務上最常見的超級迴圈形式（前景／背景架構）：
 * ISR 只設旗標，主迴圈輪詢旗標。
 */
#include <stdbool.h>
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_timer.h"

#define LED_GPIO          GPIO_NUM_2
#define BUTTON_GPIO       GPIO_NUM_0

#define LED_PERIOD_US     100000
#define HEAVY_PERIOD_US   1000000
#define HEAVY_WORK_MS     200
#define REPORT_PERIOD_US  5000000
#define DEBOUNCE_US       200000

/* ISR 與主迴圈共用的變數：用 32 位元，讀寫各只需一道指令，不會讀到「寫一半」的值 */
static volatile bool s_button_flag;
static volatile uint32_t s_button_time_us;

/* 統計資料 */
static uint32_t s_led_error_max_us;
static uint32_t s_button_count;
static uint32_t s_latency_last_us;
static uint32_t s_latency_max_us;
static uint32_t s_loop_max_us;

/* 32 位元的微秒計數約 71 分鐘溢位一次，但用無號數相減求「時間差」仍然正確 */
static uint32_t micros(void)
{
    return (uint32_t)esp_timer_get_time();
}

static void button_isr(void *arg)
{
    static uint32_t last_us;
    uint32_t now = micros();

    if (now - last_us < DEBOUNCE_US) {
        return;
    }
    last_us = now;
    s_button_time_us = now;
    s_button_flag = true;
}

static void burn_cpu_ms(uint32_t ms)
{
    uint32_t start = micros();
    volatile uint32_t x = 1;

    while (micros() - start < ms * 1000) {
        x = x * 1664525u + 1013904223u;
    }
}

static void led_job(void)
{
    static bool started;
    static uint32_t next_us;
    static uint32_t prev_toggle_us;
    static int level;
    uint32_t now = micros();

    if (!started) {
        started = true;
        next_us = now + LED_PERIOD_US;
        prev_toggle_us = now;
        return;
    }

    if ((int32_t)(now - next_us) >= 0) {
        level = !level;
        gpio_set_level(LED_GPIO, level);

        int32_t error = (int32_t)(now - prev_toggle_us) - LED_PERIOD_US;
        uint32_t abs_error = error < 0 ? -error : error;
        if (abs_error > s_led_error_max_us) {
            s_led_error_max_us = abs_error;
        }
        prev_toggle_us = now;
        next_us += LED_PERIOD_US;
    }
}

static void heavy_job(void)
{
    static uint32_t last_us;
    uint32_t now = micros();

    if (now - last_us >= HEAVY_PERIOD_US) {
        last_us = now;
        burn_cpu_ms(HEAVY_WORK_MS);
    }
}

static void button_job(void)
{
    if (s_button_flag) {
        s_button_flag = false;
        uint32_t latency = micros() - s_button_time_us;

        s_button_count++;
        s_latency_last_us = latency;
        if (latency > s_latency_max_us) {
            s_latency_max_us = latency;
        }
    }
}

static void report_job(void)
{
    static uint32_t last_us;
    uint32_t now = micros();

    if (now - last_us >= REPORT_PERIOD_US) {
        last_us = now;
        printf("[superloop] LED error max=%" PRIu32 " us | button count=%" PRIu32
               " latency last=%" PRIu32 " us max=%" PRIu32 " us | loop max=%" PRIu32 " us\n",
               s_led_error_max_us, s_button_count,
               s_latency_last_us, s_latency_max_us, s_loop_max_us);
        s_led_error_max_us = 0;
        s_loop_max_us = 0;
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
    gpio_install_isr_service(0);
    gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);
}

void app_main(void)
{
    board_init();
    printf("\n=== 08 compare: superloop ===\n");

    while (1) {
        uint32_t loop_start = micros();

        led_job();
        heavy_job();
        button_job();
        report_job();

        uint32_t loop_us = micros() - loop_start;
        if (loop_us > s_loop_max_us) {
            s_loop_max_us = loop_us;
        }
    }
}
