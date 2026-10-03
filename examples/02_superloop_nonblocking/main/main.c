/*
 * 範例 02：非阻塞式超級迴圈 (non-blocking superloop)
 *
 * 把每件工作改寫成「看一下時間到了沒，沒到就立刻返回」的狀態機，
 * 也就是 Arduino 世界常說的 millis() 寫法。
 *
 * 觀察重點：
 *   - SIMULATE_SLOW_SENSOR = 0：迴圈每秒可跑數十萬圈，按鈕反應很好。
 *   - SIMULATE_SLOW_SENSOR = 1：只要有一件工作本身就很花時間，
 *     所有工作還是會一起被拖慢。
 */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define LED_GPIO     GPIO_NUM_2
#define BUTTON_GPIO  GPIO_NUM_0

#define SIMULATE_SLOW_SENSOR  0   /* 改成 1 後重新燒錄，觀察差異 */

#define LED_PERIOD_MS     500
#define SENSOR_PERIOD_MS  1000
#define DEBOUNCE_MS       30
#define REPORT_PERIOD_MS  5000

static uint32_t millis(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static void board_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

/* 工作 1：LED，時間到才動作 */
static void led_job(uint32_t now)
{
    static uint32_t last_toggle;
    static int level;

    if (now - last_toggle >= LED_PERIOD_MS) {
        last_toggle = now;
        level = !level;
        gpio_set_level(LED_GPIO, level);
    }
}

/* 工作 2：感測器 */
static void sensor_job(uint32_t now)
{
    static uint32_t last_read;
    static int fake_value = 20;

    if (now - last_read >= SENSOR_PERIOD_MS) {
        last_read = now;
#if SIMULATE_SLOW_SENSOR
        esp_rom_delay_us(300 * 1000);   /* 這 300 ms 內，其他工作全部停擺 */
#endif
        fake_value = (fake_value + 1) % 100;
        printf("sensor=%d\n", fake_value);
    }
}

/* 工作 3：按鈕，含去彈跳的小狀態機 */
static void button_job(uint32_t now)
{
    static int last_raw = 1;
    static int stable = 1;
    static uint32_t last_change;
    static uint32_t press_count;

    int raw = gpio_get_level(BUTTON_GPIO);
    if (raw != last_raw) {
        last_raw = raw;
        last_change = now;
    }

    if (raw != stable && now - last_change >= DEBOUNCE_MS) {
        stable = raw;
        if (stable == 0) {
            press_count++;
            printf("button pressed, count=%" PRIu32 "\n", press_count);
        }
    }
}

void app_main(void)
{
    uint32_t loop_count = 0;
    uint32_t max_loop_us = 0;
    uint32_t last_report = 0;

    board_init();
    printf("\n=== 02 superloop (non-blocking), slow sensor=%d ===\n",
           SIMULATE_SLOW_SENSOR);

    while (1) {
        int64_t loop_start = esp_timer_get_time();
        uint32_t now = millis();

        led_job(now);
        sensor_job(now);
        button_job(now);

        /* 統計：迴圈圈數與最慢的一圈 */
        uint32_t loop_us = (uint32_t)(esp_timer_get_time() - loop_start);
        if (loop_us > max_loop_us) {
            max_loop_us = loop_us;
        }
        loop_count++;

        if (now - last_report >= REPORT_PERIOD_MS) {
            printf("[report] loops=%" PRIu32 ", worst loop=%" PRIu32 " us\n",
                   loop_count, max_loop_us);
            last_report = now;
            loop_count = 0;
            max_loop_us = 0;
        }
    }
}
