# 第 3 章：任務與排程

把第 2 章的三件工作改寫成三個 FreeRTOS 任務，親眼看到「搶佔」如何解決超級迴圈的問題。

程式：[`examples/03_rtos_tasks/main/main.c`](../examples/03_rtos_tasks/main/main.c)

## 任務長什麼樣子

```c
static void led_task(void *arg)
{
    // 初始化（只執行一次）
    while (1) {
        // 做事
        // 呼叫會阻塞的 API，把 CPU 讓出來
    }
    // 永遠不可以執行到這裡（不能 return）
}
```

任務函式有三個規則：

1. 簽名固定為 `void func(void *arg)`。
2. 通常是無窮迴圈；若要結束，必須呼叫 `vTaskDelete(NULL)` 刪除自己，**不可以 `return`**。
3. 迴圈中一定要有會讓任務進入 Blocked 的呼叫，否則低優先權的任務永遠沒機會執行。

## 建立任務

```c
xTaskCreatePinnedToCore(
    button_task,   // 任務函式
    "button",      // 名稱（除錯用）
    4096,          // 堆疊大小，ESP-IDF 中單位是 bytes
    NULL,          // 傳給任務函式的參數
    3,             // 優先權，數字越大越優先
    NULL,          // 用來接收 task handle，不需要可填 NULL
    0);            // 綁定在哪一顆核心
```

- 標準 FreeRTOS 的 API 是 `xTaskCreate()`（沒有最後一個參數）。ESP32 多數型號是雙核心，ESP-IDF 因此多了 `xTaskCreatePinnedToCore()`。
- **本範例刻意把三個任務綁在同一顆核心**。若不綁定，重運算可能在核心 1 執行、按鈕在核心 0 執行，各跑各的，就看不出搶佔的效果了。
- 堆疊大小：原版 FreeRTOS 以「word」為單位，ESP-IDF 以「byte」為單位。會呼叫 `printf` 或 `ESP_LOGx` 的任務建議至少 3～4 KB。如何確認夠不夠用，第 7 章會示範。

## 優先權設計

| 任務 | 優先權 | 理由 |
|---|---|---|
| `button_task` | 3 | 使用者操作需要最快的反應，而且每次執行時間極短 |
| `led_task` | 2 | 需要規律，但晚一點點無妨 |
| `sensor_task` | 1 | 耗時長、不急，適合放在最低 |

一個實用的原則：**執行時間越短、對時間越敏感的任務，優先權越高**。
耗時的運算放低優先權，它會自動使用「別人不用的 CPU 時間」。

## 兩種延遲

```c
vTaskDelay(pdMS_TO_TICKS(700));                       // 從現在起，睡 700 ms
vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(500));      // 睡到「上次喚醒時間 + 500 ms」
```

差別在於週期是否會累積誤差：

```
vTaskDelay(500)：週期 = 工作時間 + 500 ms，工作時間不固定，週期就跟著飄
|工作|──── 500 ────|工作..|──── 500 ────|工作|

vTaskDelayUntil(500)：週期固定 500 ms，不受工作時間影響
|工作|── 等 ──|工作..|─ 等 ─|工作|── 等 ──|
|←── 500 ──→|←── 500 ──→|←── 500 ──→|
```

需要固定週期（取樣、控制迴路、閃爍）時用 `vTaskDelayUntil`；只是「休息一下」用 `vTaskDelay`。

`pdMS_TO_TICKS()` 會把毫秒換算成 tick 數。本範例把 tick 設為 1000 Hz，因此 1 tick = 1 ms。

## 實驗

```bash
cd examples/03_rtos_tasks
idf.py set-target esp32
idf.py -p <你的序列埠> flash monitor
```

1. 觀察 LED 的閃爍節奏。
2. 快速短按 BOOT 按鈕多次。
3. 留意每行 log 最前面的時間戳記（單位 ms）。

### 預期觀察

- LED 穩定地每 500 ms 切換一次。
- 每一次短按都會被偵測到——即使 `sensor_task` 正在進行 300 ms 的運算。
- `sensor=...` 每秒出現一次。

與範例 02 的實驗 B 對照：同樣有一件「無法拆解的 300 ms 工作」，這次其他工作完全不受影響。**我們沒有改寫那件慢工作，只是把它放進一個低優先權的任務。**

## 發生了什麼事

```
時間 (ms)   0        100       200       300       400       500
button(3)  ▌   ▌   ▌   ▌   ▌   ▌   ▌   ▌   ▌   ▌   ▌   ▌   ▌    每 20 ms 醒來一下
led(2)     ▌                                                 ▌   每 500 ms 醒來一下
sensor(1)  ███ ███ ███ ███ ███ ███ ███ ██                        被切成許多段，總計 300 ms
idle(0)                                  ░░░░░░░░░░░░░░░░░░░░    沒人要用 CPU 時才執行
```

1. `sensor_task` 開始運算。它是最低優先權，但此刻其他任務都在 Blocked，所以由它執行。
2. 20 ms 到，tick 中斷發生，排程器發現 `button_task` 的延遲到期、變成 Ready，而且優先權較高 → **搶佔**。
3. `button_task` 讀一下 GPIO（數微秒），再次呼叫 `vTaskDelay` 進入 Blocked。
4. 排程器把 CPU 還給 `sensor_task`，它從被打斷的那一道指令繼續執行，完全不知道自己被暫停過。

`burn_cpu_ms()` 裡面沒有任何「讓出 CPU」的程式碼。這就是搶佔式排程與合作式排程的差別。

## app_main 可以返回

```c
void app_main(void)
{
    xTaskCreatePinnedToCore(...);
    // 沒有 while(1)
}
```

`app_main` 是由 ESP-IDF 的 `main` 任務（優先權 1）呼叫的。它返回之後，`main` 任務會自行刪除，其他任務繼續執行。

## 動手改改看

1. **把優先權反過來**：`sensor_task` 設為 3、`button_task` 設為 1。預期：運算的 300 ms 內按鈕沒有反應，行為退化得像超級迴圈。優先權設錯，RTOS 也救不了你。
2. **拿掉 `sensor_task` 的 `vTaskDelay`**：預期：5 秒後出現 task watchdog 警告，因為 idle task 被餓死了。
3. **把 `led_task` 的 `vTaskDelayUntil` 改為 `vTaskDelay`**：此例中 LED 的工作時間極短，肉眼看不出差異；但若在迴圈中加入 `burn_cpu_ms(100)`，週期就會變成 600 ms。

## 小結

- 任務 = 獨立的無窮迴圈 + 自己的堆疊 + 一個優先權。
- 排程器永遠執行最高優先權的 Ready 任務；任務阻塞時把 CPU 讓出來。
- 耗時的工作不需要拆解，放在低優先權即可。
- 新的責任：優先權與堆疊大小要自己設計。

---

下一章：[佇列](04-queue.md)
