# 第 4 章：佇列（Queue）

任務各自獨立之後，下一個問題是：它們要怎麼交換資料？

程式：[`examples/04_rtos_queue/main/main.c`](../examples/04_rtos_queue/main/main.c)

## 為什麼不直接用全域變數

在超級迴圈裡，用全域變數傳遞資料很安全，因為工作是一件做完才做下一件。
在 RTOS 裡，任務可能在任何地方被搶佔：

```c
// 任務 A                          // 任務 B
g_data.temperature = 25;
                          ← 在這裡被搶佔 →   讀到新的溫度 + 舊的濕度！
g_data.humidity = 60;
```

**佇列**是 FreeRTOS 提供的執行緒安全 FIFO，同時解決兩件事：

1. **資料傳遞**：傳送與接收都是不可分割的操作，不會讀到寫一半的資料。
2. **同步**：佇列空的時候，接收方可以阻塞等待，資料一到就被喚醒，不需要輪詢。

## 本範例的架構

```
sensor_task ──▶ [ sensor_q (8 格) ] ──▶ logger_task     量測資料
button_task ──▶ [ led_cmd_q (4 格) ] ──▶ led_task        改變閃爍速度的命令
```

沒有任何全域的資料變數，只有兩個佇列的 handle。

## API

```c
// 建立：8 個元素，每個元素 sizeof(sensor_msg_t) bytes
QueueHandle_t q = xQueueCreate(8, sizeof(sensor_msg_t));

// 傳送：佇列滿時最多等多久（0 = 不等，portMAX_DELAY = 一直等）
xQueueSend(q, &msg, 0);

// 接收：佇列空時最多等多久
xQueueReceive(q, &msg, portMAX_DELAY);
```

三個重點：

- **以複製的方式傳遞**。`xQueueSend` 會把 `msg` 的內容整份複製進佇列，所以傳送後區域變數可以立刻重複使用。大型資料（數百 bytes 以上）則建議傳指標，但此時記憶體的生命週期要自己管理。
- **回傳值要檢查**。佇列滿而逾時會回傳 `errQUEUE_FULL`（不等於 `pdTRUE`），資料並沒有送進去。
- **等待時間的單位是 tick**，請一律用 `pdMS_TO_TICKS()` 換算。

## 一個實用技巧：把 timeout 當成週期

`led_task` 需要同時做兩件事：定時切換 LED、隨時接收新命令。它只用了一個呼叫：

```c
if (xQueueReceive(s_led_cmd_q, &new_period_ms, pdMS_TO_TICKS(period_ms)) == pdTRUE) {
    period_ms = new_period_ms;          // 收到命令
} else {
    level = !level;                     // 逾時：該切換 LED 了
    gpio_set_level(LED_GPIO, level);
}
```

這比「`vTaskDelay` 之後再檢查有沒有命令」更好：命令一到任務立刻醒來，不必等延遲結束。

（收到命令的那一次會提早結束等待，使該次閃爍間隔略有變化。對 LED 無妨；若需要嚴格的週期，應改用第 7 章的軟體計時器。）

## 實驗

```bash
cd examples/04_rtos_queue
idf.py set-target esp32
idf.py -p <你的序列埠> flash monitor
```

1. 觀察 `seq=... value=...` 每 500 ms 印出一次。
2. 按 BOOT 按鈕，LED 的閃爍週期會在 500 → 250 → 100 → 1000 ms 之間循環。

### 預期觀察

- `in queue for ... us` 顯示資料在佇列中停留的時間，通常只有數十微秒：`sensor_task` 一送出，等在佇列上的 `logger_task`（優先權較高）立刻被喚醒。
- 按下按鈕後 LED 的節奏幾乎立刻改變，不必等目前的週期結束。

## 動手改改看

1. **製造佇列塞滿**：在 `logger_task` 的迴圈裡加入 `vTaskDelay(pdMS_TO_TICKS(2000))`，讓消費速度低於生產速度。預期：約 5 秒後開始出現 `sensor queue full, drop seq=...`。
   這顯示佇列也是一個**緩衝**：能吸收短暫的速度落差，但無法解決長期的生產大於消費。
2. **改變滿的時候的策略**：把 `sensor_task` 的 `xQueueSend(..., 0)` 改為 `portMAX_DELAY`。預期：不再丟資料，但 `sensor_task` 會被拖慢到與 `logger_task` 相同的速度（背壓）。丟棄還是等待，要依應用決定。
3. **多個生產者**：再建立一個 `sensor_task`（可用 `arg` 傳入不同的編號）。佇列天生支援多個傳送者與多個接收者。

## 與超級迴圈的對照

| | 超級迴圈 | FreeRTOS 佇列 |
|---|---|---|
| 傳遞資料 | 全域變數／旗標 | 佇列（複製、執行緒安全） |
| 等資料 | 每圈檢查旗標 | 阻塞在佇列上，資料到才醒 |
| 生產者與消費者速度不同 | 自己實作環形緩衝區 | 佇列本身就是緩衝區 |
| 模組間的耦合 | 透過共用變數，容易糾纏 | 只透過佇列介面，容易替換與測試 |

## 小結

- 佇列是任務之間傳遞資料的首選方式。
- 「阻塞等待資料」取代了超級迴圈的「輪詢旗標」。
- 設計時要決定：佇列長度、滿的時候丟棄還是等待。

---

下一章：[互斥鎖與競爭條件](05-mutex.md)
