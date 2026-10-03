# 第 6 章：中斷與延遲處理

到目前為止，按鈕都是用輪詢（每 20 ms 看一次）偵測的。本章改用硬體中斷，並量測從「按下」到「任務開始處理」花了多少時間。

程式：[`examples/06_rtos_isr/main/main.c`](../examples/06_rtos_isr/main/main.c)

## 延遲中斷處理

ISR 執行時，同等級與較低等級的中斷都被擋住，排程器也無法運作，所以 ISR 必須盡可能短。標準做法是：

```
硬體事件 ──▶ ISR（極短）：記錄必要資訊，通知任務
                 │
                 ▼
            handler task（高優先權）：做真正的處理
```

這個模式稱為**延遲中斷處理（deferred interrupt handling）**。
它與超級迴圈的「ISR 設旗標，主迴圈檢查」很像，但有一個關鍵差異：

| | 超級迴圈 | FreeRTOS |
|---|---|---|
| ISR 之後，處理何時開始 | 等主迴圈輪到檢查旗標的那一行 | ISR 一結束，**立刻**切換到 handler task |
| 延遲取決於 | 主迴圈當時正在做的工作有多久 | 上下文切換時間（微秒等級） |

## ISR 的規則

```c
static void button_isr(void *arg)
{
    int64_t now = esp_timer_get_time();
    BaseType_t higher_prio_task_woken = pdFALSE;

    xQueueSendFromISR(s_button_q, &now, &higher_prio_task_woken);

    if (higher_prio_task_woken) {
        portYIELD_FROM_ISR();
    }
}
```

1. **只能呼叫 `FromISR` 結尾的 FreeRTOS API**。一般版本可能會阻塞，在 ISR 中呼叫會導致當機。
2. **不可以阻塞**：不能 `vTaskDelay`、不能拿 mutex。
3. **不要在 ISR 中 `printf` 或 `ESP_LOGx`**。
4. **`higher_prio_task_woken` 與 `portYIELD_FROM_ISR()`**：
   `xQueueSendFromISR` 若喚醒了一個比「目前被中斷的任務」優先權更高的任務，會把這個變數設為 `pdTRUE`。
   此時呼叫 `portYIELD_FROM_ISR()`，ISR 返回時就會直接切換到那個高優先權任務，而不是回到原本被中斷的任務。
   少了這一步，handler task 要等到下一次 tick 才會執行，延遲最多增加一個 tick。

> ESP-IDF 中，若註冊中斷時指定了 `ESP_INTR_FLAG_IRAM`（讓中斷在寫入 flash 期間仍可觸發），
> ISR 及其呼叫的所有函式都必須加上 `IRAM_ATTR` 放進 RAM。本範例未使用該旗標，因此不需要。

## 設定 GPIO 中斷

```c
gpio_config_t button_cfg = {
    .pin_bit_mask = 1ULL << BUTTON_GPIO,
    .mode = GPIO_MODE_INPUT,
    .pull_up_en = GPIO_PULLUP_ENABLE,
    .intr_type = GPIO_INTR_NEGEDGE,      // 下降緣：按下的瞬間
};
gpio_config(&button_cfg);

gpio_install_isr_service(0);                          // 安裝 GPIO 中斷服務（整個程式一次）
gpio_isr_handler_add(BUTTON_GPIO, button_isr, NULL);  // 為這支腳註冊 handler
```

注意 `app_main` 中的順序：**先建立佇列與任務，最後才開啟中斷**。否則 ISR 可能用到尚未建立的佇列。

## 去彈跳

機械按鈕按下時，接點會在數毫秒內彈跳多次，產生一連串的邊緣，每一個都會觸發中斷。
本範例在 handler task 中處理：200 ms 內只承認第一個事件。

```c
if (isr_time_us - last_accepted_us < DEBOUNCE_US) {
    continue;
}
```

去彈跳的邏輯放在任務而不是 ISR，正是「ISR 只做最少的事」的體現。

> 放開按鈕時的彈跳也可能產生下降緣。若按住超過 200 ms 才放開，偶爾會多算一次。
> 更完整的做法是在任務中延遲數十毫秒後再確認腳位電位（第 7 章的範例採用此法）。

## 實驗

```bash
cd examples/06_rtos_isr
idf.py set-target esp32
idf.py -p <你的序列埠> flash monitor
```

程式中有一個 `load_task`，每秒有 800 ms 在佔用 CPU，模擬系統很忙碌的情況。

1. 按 BOOT 按鈕多次，每次 LED 會切換，並印出延遲。
2. 記下延遲的大致範圍。

### 預期觀察

```
press #1: ISR -> task latency = xx us
```

- 延遲預期在**數十微秒**的等級，而且不論 `load_task` 是否正在運算，數值都差不多。
- 對照第 3 章每 20 ms 輪詢一次的做法（平均延遲 10 ms、最壞 20 ms），這是數百倍的改善，而且任務在沒有事件時完全不佔 CPU。

實測值請記錄下來，第 8 章會用到：

| 次數 | 延遲 (us) |
|---|---|
| 1 | |
| 2 | |
| 3 | |
| 4 | |
| 5 | |

## 動手改改看

1. **拿掉 `portYIELD_FROM_ISR()`**（把那個 `if` 區塊註解掉）。預期：延遲變大且不穩定，落在 0～1000 us 之間，因為要等下一次 tick 才切換任務。
2. **把 `handler_task` 的優先權改成 1**（與 `load_task` 相同）。預期：延遲明顯變大，因為它不再能搶佔 `load_task`，只能等時間片輪轉。
3. **把佇列改成 binary semaphore**：若不需要傳遞時間戳記，只需要「通知有事發生」，可用 `xSemaphoreCreateBinary()`、`xSemaphoreGiveFromISR()`、`xSemaphoreTake()`。下一章會介紹更輕量的任務通知。

## 小結

- ISR 保持極短，把工作交給高優先權任務。
- ISR 中只用 `FromISR` API，並記得 `portYIELD_FROM_ISR()`。
- RTOS 讓「中斷之後的處理」也能擁有接近中斷的反應速度，這是超級迴圈做不到的。

---

下一章：[軟體計時器與任務通知](07-timers-notifications.md)
