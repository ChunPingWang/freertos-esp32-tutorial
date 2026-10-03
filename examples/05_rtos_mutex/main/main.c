/*
 * 範例 05：競爭條件 (race condition) 與互斥鎖 (mutex)
 *
 * 兩個任務各自對同一個全域計數器加 1，共 ITERATIONS 次。
 * 每一回合先「不加鎖」跑一次，再「加鎖」跑一次，並印出結果。
 *
 * 觀察重點：
 *   - 不加鎖：結果小於 2 * ITERATIONS，而且每次都不一樣（更新遺失）。
 *   - 加鎖  ：結果永遠正確，但花的時間明顯變長（同步是有成本的）。
 */
#include <stdbool.h>
#include <stdint.h>
#include <inttypes.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "esp_log.h"

#define ITERATIONS  100000

/* 雙核晶片上讓兩個任務在不同核心「真正同時」執行，最容易出現競爭 */
#define CORE_A  0
#if CONFIG_FREERTOS_UNICORE
#define CORE_B  0
#else
#define CORE_B  1
#endif

static const char *TAG = "ex05";

static volatile uint32_t s_counter;
static bool s_use_mutex;
static SemaphoreHandle_t s_mutex;
static SemaphoreHandle_t s_done;

static void worker_task(void *arg)
{
    for (int i = 0; i < ITERATIONS; i++) {
        if (s_use_mutex) {
            xSemaphoreTake(s_mutex, portMAX_DELAY);
        }

        /* 臨界區段：「讀 -> 改 -> 寫」三個步驟並不是一個不可分割的動作 */
        uint32_t tmp = s_counter;
        tmp++;
        s_counter = tmp;

        if (s_use_mutex) {
            xSemaphoreGive(s_mutex);
        }
    }

    xSemaphoreGive(s_done);
    vTaskDelete(NULL);   /* 任務函式不可以直接 return，必須刪除自己 */
}

static void run_round(bool use_mutex)
{
    /* 與 main task 同優先權，建立時不會立刻搶走 CPU，兩個 worker 幾乎同時起跑 */
    UBaseType_t prio = uxTaskPriorityGet(NULL);

    s_counter = 0;
    s_use_mutex = use_mutex;

    int64_t start = esp_timer_get_time();
    xTaskCreatePinnedToCore(worker_task, "workerA", 4096, NULL, prio, NULL, CORE_A);
    xTaskCreatePinnedToCore(worker_task, "workerB", 4096, NULL, prio, NULL, CORE_B);

    /* 等兩個 worker 都做完 */
    xSemaphoreTake(s_done, portMAX_DELAY);
    xSemaphoreTake(s_done, portMAX_DELAY);
    int64_t elapsed_ms = (esp_timer_get_time() - start) / 1000;

    uint32_t expected = 2 * ITERATIONS;
    uint32_t actual = s_counter;
    ESP_LOGI(TAG, "%-13s expected=%" PRIu32 " actual=%" PRIu32 " lost=%" PRIu32 " time=%lld ms",
             use_mutex ? "with mutex:" : "no mutex:",
             expected, actual, expected - actual, (long long)elapsed_ms);
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 05 race condition & mutex ===");

    s_mutex = xSemaphoreCreateMutex();
    s_done  = xSemaphoreCreateCounting(2, 0);
    configASSERT(s_mutex && s_done);

    while (1) {
        run_round(false);
        vTaskDelay(pdMS_TO_TICKS(500));
        run_round(true);
        vTaskDelay(pdMS_TO_TICKS(3000));
    }
}
