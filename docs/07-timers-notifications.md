# 第 7 章：軟體計時器與任務通知

本章介紹兩個讓程式更精簡的工具，並示範如何監控任務的堆疊用量。

程式：[`examples/07_rtos_timer_notify/main/main.c`](../examples/07_rtos_timer_notify/main/main.c)

範例做的是一個「樓梯間感應燈」：按下按鈕 LED 亮起，3 秒後自動熄滅；3 秒內再按則重新計時。

## 軟體計時器

「過一段時間後做某件事」或「每隔一段時間做某件事」，如果為每一件都建立一個任務，太浪費記憶體。
軟體計時器讓你只註冊一個 callback：

```c
// 參數：名稱、週期、是否自動重複、timer ID、callback
s_off_timer = xTimerCreate("off", pdMS_TO_TICKS(3000), pdFALSE, NULL, off_timer_cb);  // one-shot
s_hb_timer  = xTimerCreate("hb",  pdMS_TO_TICKS(2000), pdTRUE,  NULL, heartbeat_cb);  // auto-reload

xTimerStart(s_hb_timer, portMAX_DELAY);
xTimerReset(s_off_timer, portMAX_DELAY);   // 沒在跑就啟動；在跑就重新計時
```

| 種類 | 行為 | 用途 |
|---|---|---|
| one-shot | 到期執行一次 callback 後停止 | 逾時處理、延後執行 |
| auto-reload | 到期執行 callback 後自動重新開始 | 週期性工作 |

### 它是怎麼運作的

所有軟體計時器的 callback，都在同一個由 FreeRTOS 建立的 **timer service task** 中執行。
`xTimerStart`、`xTimerReset` 等 API 其實是把命令送進一個佇列給這個任務（所以它們有一個「等待時間」參數）。

由此得到 callback 的規則：

1. **必須很快結束，不可以阻塞**（不能 `vTaskDelay`、不要長時間等鎖）。一個 callback 卡住，所有計時器都會延遲。
2. callback 使用的是 timer service task 的堆疊。ESP-IDF 預設只有 2048 bytes，本範例因為在 callback 中呼叫 `ESP_LOGI`，在 `sdkconfig.defaults` 中把它加大了。
3. 精確度是 tick 等級，並受 timer service task 的優先權影響。需要微秒級精確度請改用 ESP-IDF 的 `esp_timer` 或硬體計時器。

「重新計時」的需求用 `xTimerReset()` 一行就能完成。若在超級迴圈中實作，需要自己維護「到期時間」變數並在主迴圈中不斷比對。

## 任務通知

每個任務都內建一個 32 位元的通知值。當「一個 ISR 或任務只想喚醒某個特定任務」時，可以不必建立佇列或號誌：

```c
// ISR 端
vTaskNotifyGiveFromISR(s_button_task, &higher_prio_task_woken);

// 任務端：睡到被通知為止
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
```

- `xTaskNotifyGive()`／`vTaskNotifyGiveFromISR()`：把目標任務的通知值加 1，並喚醒它。
- `ulTaskNotifyTake(pdTRUE, timeout)`：等到通知值不為 0，回傳該值；`pdTRUE` 表示返回前把它歸零（行為類似 binary semaphore），`pdFALSE` 表示只減 1（類似 counting semaphore）。

### 該用哪一種？

| 機制 | 適用情況 | 限制 |
|---|---|---|
| 任務通知 | 喚醒**一個已知的**任務，最快、不需額外記憶體 | 必須知道對方的 task handle；只能有一個接收者 |
| Binary semaphore | 通知「有事發生」，接收者不固定 | 不帶資料 |
| 佇列 | 需要傳遞資料，或需要緩衝多筆事件 | 佔較多記憶體 |
| Mutex | 保護共用資源 | 不可用於 ISR |

## 去彈跳的另一種寫法

```c
ulTaskNotifyTake(pdTRUE, portMAX_DELAY);     // 被 ISR 喚醒

vTaskDelay(pdMS_TO_TICKS(30));               // 等彈跳結束
if (gpio_get_level(BUTTON_GPIO) != 0) {      // 確認仍是按下狀態
    continue;
}
...
ulTaskNotifyTake(pdTRUE, 0);                 // 清掉彈跳期間累積的通知
```

「等 30 ms 再確認」用三行就寫完了。在任務裡，等待可以直接照順序寫出來；
同樣的邏輯在超級迴圈裡，是第 2 章 `button_job` 那個需要四個 `static` 變數的狀態機。
**這是 RTOS 在程式可讀性上最直接的好處。**

## 監控堆疊與記憶體

每個任務的堆疊大小是建立時自己指定的。配太大浪費 RAM，配太小會堆疊溢位（通常是莫名其妙的當機）。
FreeRTOS 提供了量測工具：

```c
uxTaskGetStackHighWaterMark(task_handle);   // 該任務啟動以來，堆疊「最少曾經剩下多少」
esp_get_free_heap_size();                   // 目前 heap 剩餘
```

「high water mark」是歷史最低剩餘量，在 ESP-IDF 中單位是 bytes。
實務做法：開發時先給寬鬆的堆疊（例如 4096），讓程式把所有功能都跑過一輪，觀察 high water mark，再把堆疊調整為「實際最大用量 + 適當餘裕」。

## 實驗

```bash
cd examples/07_rtos_timer_notify
idf.py set-target esp32
idf.py -p <你的序列埠> flash monitor
```

1. 按一下 BOOT 按鈕 → LED 亮起，3 秒後熄滅並印出 `timeout -> LED off`。
2. LED 亮著的時候，每隔約 2 秒再按一次 → LED 持續亮著，直到最後一次按下的 3 秒後才熄滅。
3. 觀察每 2 秒的 `heartbeat` 與每 5 秒的堆疊報告。

### 預期觀察

- `heartbeat` 的時間戳記間隔穩定為 2000 ms，不受按鈕操作影響。
- 堆疊報告中，`button` 與 `monitor` 的最小剩餘量都還有相當的空間（配置 4096，預期剩餘在 2000 bytes 以上）。請記下實際數字，並思考堆疊可以縮小到多少。

## 動手改改看

1. **在 callback 中阻塞**：在 `off_timer_cb` 中加入 `vTaskDelay(pdMS_TO_TICKS(1500))`，然後按一下按鈕。預期：LED 熄滅前後，`heartbeat` 會延遲出現。這說明了為什麼 callback 不能阻塞。
2. **把 `button_task` 的堆疊從 4096 改成 1024**。預期：呼叫 `ESP_LOGI` 時發生堆疊溢位，系統印出錯誤訊息並重新啟動。試完記得改回來。
3. **計算按了幾次**：把 `ulTaskNotifyTake` 的第一個參數改為 `pdFALSE`，觀察行為有何不同。

## 小結

- 軟體計時器：不需要專屬任務的定時動作；callback 要短、不可阻塞。
- 任務通知：喚醒特定任務最輕量的方法。
- 用 `uxTaskGetStackHighWaterMark` 以實測決定堆疊大小。

---

下一章：[總結比較：實測兩種架構](08-comparison.md)
