/*
 * 範例 01：阻塞式超級迴圈 (blocking superloop)
 *
 * 三件工作全部寫在同一個 while(1) 裡，並且用「忙碌等待」的 delay：
 *   1. LED 每 500 ms 亮／滅
 *   2. 讀取感測器（模擬：需要 300 ms）
 *   3. 檢查按鈕
 *
 * 觀察重點：一圈迴圈約 1300 ms，按鈕每圈只被檢查一次，
 *           短按幾乎一定會被漏掉。
 */
#include <stdio.h>
#include <stdint.h>
#include <inttypes.h>
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_rom_sys.h"

#define LED_GPIO     GPIO_NUM_2   /* 板載 LED，若開發板沒有請外接 LED + 330Ω */
#define BUTTON_GPIO  GPIO_NUM_0   /* BOOT 按鈕，按下為低電位 */

/* 忙碌等待：CPU 空轉、完全不讓出控制權，等同裸機上的 delay() */
static void delay_ms(uint32_t ms)
{
    esp_rom_delay_us(ms * 1000);
}

static void board_init(void)
{
    gpio_reset_pin(LED_GPIO);
    gpio_set_direction(LED_GPIO, GPIO_MODE_OUTPUT);

    gpio_reset_pin(BUTTON_GPIO);
    gpio_set_direction(BUTTON_GPIO, GPIO_MODE_INPUT);
    gpio_set_pull_mode(BUTTON_GPIO, GPIO_PULLUP_ONLY);
}

/* 模擬一顆很慢的感測器（例如 DHT22、或需要等待轉換完成的 ADC） */
static int read_sensor_blocking(void)
{
    static int fake_value = 20;

    delay_ms(300);
    fake_value = (fake_value + 1) % 100;
    return fake_value;
}

void app_main(void)
{
    uint32_t press_count = 0;

    board_init();
    printf("\n=== 01 superloop (blocking) ===\n");

    while (1) {
        int64_t loop_start = esp_timer_get_time();

        /* 工作 1：閃爍 LED */
        gpio_set_level(LED_GPIO, 1);
        delay_ms(500);
        gpio_set_level(LED_GPIO, 0);
        delay_ms(500);

        /* 工作 2：讀取感測器 */
        int value = read_sensor_blocking();

        /* 工作 3：檢查按鈕（每圈只看這一瞬間！） */
        if (gpio_get_level(BUTTON_GPIO) == 0) {
            press_count++;
            printf("button pressed, count=%" PRIu32 "\n", press_count);
        }

        int64_t loop_ms = (esp_timer_get_time() - loop_start) / 1000;
        printf("sensor=%d, loop time=%lld ms\n", value, (long long)loop_ms);
    }
}
