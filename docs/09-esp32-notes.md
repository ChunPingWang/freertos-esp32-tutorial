# 第 9 章：ESP32 的特殊之處與常見陷阱

ESP-IDF 使用的 FreeRTOS 經過修改，與一般教科書或其他微控制器上的 FreeRTOS 有幾處不同。本章整理這些差異與實務上最常遇到的問題。

## 與標準 FreeRTOS 的差異

| 項目 | 標準 FreeRTOS | ESP-IDF |
|---|---|---|
| 核心數 | 單核 | 支援雙核對稱多工（SMP） |
| 堆疊大小的單位 | word（4 bytes） | **byte** |
| 建立任務 | `xTaskCreate` | 另有 `xTaskCreatePinnedToCore` 可指定核心 |
| 啟動排程器 | 自己呼叫 `vTaskStartScheduler()` | 系統已啟動，**不可以**再呼叫 |
| 程式進入點 | `main()` | `app_main()`，由 `main` 任務（優先權 1）呼叫 |
| 臨界區段 | `taskENTER_CRITICAL()` | 需傳入自旋鎖：`portENTER_CRITICAL(&lock)` |
| Tick 頻率 | 自訂 | 預設 100 Hz，可由 `CONFIG_FREERTOS_HZ` 調整 |

## 雙核心

ESP32、ESP32-S3 有兩顆核心；ESP32-C3、C6、S2 等為單核。

- `xTaskCreate()` 建立的任務**不綁定核心**，排程器會把它放到任一顆有空的核心上執行。
- `xTaskCreatePinnedToCore(..., core_id)` 把任務固定在某一顆核心。
- 排程規則變成：每顆核心各自執行「可以在該核心上執行的任務」中優先權最高的一個。

雙核帶來的影響：

1. **兩個任務會真正同時執行**。單核時「不會被搶佔所以安全」的推論不再成立（第 5 章）。
2. **只關閉中斷無法保護共用資料**，因為另一顆核心仍在執行。這就是 ESP-IDF 的臨界區段需要自旋鎖的原因。
3. Wi-Fi 與藍牙的協定任務預設在核心 0。運算量大的應用任務常會放在核心 1，以免影響無線通訊。

本教程的範例大多把任務綁在核心 0，是為了讓行為等同於傳統的單核 RTOS，方便與超級迴圈比較。實際專案中通常不必刻意綁定。

## 看門狗

ESP-IDF 預設啟用兩種看門狗：

| 看門狗 | 監看對象 | 觸發時 |
|---|---|---|
| 任務看門狗（Task WDT） | 各核心的 idle task 是否在 5 秒內執行過 | 印出警告（預設不重啟） |
| 中斷看門狗（Interrupt WDT） | 中斷是否被關閉太久 | 系統 panic 並重啟 |

看到下面這類訊息：

```
E (xxxx) task_wdt: Task watchdog got triggered. The following tasks/users did not reset the watchdog in time:
E (xxxx) task_wdt:  - IDLE0 (CPU 0)
```

代表有任務霸佔 CPU、沒有進入 Blocked。解法是在該任務的迴圈中加入 `vTaskDelay()` 或其他阻塞呼叫，**而不是關閉看門狗**。
（本教程的超級迴圈範例是為了示範才關閉它。）

注意 `vTaskDelay(0)` 與 `taskYIELD()` 只會讓給**同優先權或更高優先權**的任務，idle task（優先權 0）仍然輪不到。

## 常見陷阱

### 1. 堆疊溢位

**症狀**：隨機當機、`Guru Meditation Error`、或出現 `***ERROR*** A stack overflow in task xxx has been detected.`

**原因與對策**：

- 堆疊配置太小。`printf`、`ESP_LOGx`、浮點數格式化都很吃堆疊。
- 在任務中宣告大型區域陣列（例如 `char buf[2048];`）。改用 `static`、全域變數或 `malloc`。
- 用 `uxTaskGetStackHighWaterMark()` 實測（第 7 章）。

### 2. `vTaskDelay` 的參數是 tick，不是毫秒

```c
vTaskDelay(100);                  // 100 個 tick：預設組態下是 1000 ms！
vTaskDelay(pdMS_TO_TICKS(100));   // 100 ms
```

另外，在預設 100 Hz 的 tick 下，`pdMS_TO_TICKS(5)` 會算出 0，等於沒有延遲。

### 3. 任務函式 return

任務函式結束而沒有呼叫 `vTaskDelete(NULL)`，系統會直接中止。

### 4. 在 ISR 中呼叫非 `FromISR` 的 API

症狀通常是立即當機。ISR 中也不要呼叫 `printf`、`ESP_LOGx`、`malloc`。
需要在 ISR 中輸出除錯訊息時，可用 `ESP_DRAM_LOGI()` 或 `esp_rom_printf()`。

### 5. 把區域變數的指標傳給任務

```c
void app_main(void)
{
    int param = 42;
    xTaskCreate(my_task, "t", 4096, &param, 5, NULL);
}   // app_main 返回後 param 就不存在了，my_task 讀到的是垃圾值
```

傳給任務的指標必須指向 `static`、全域或動態配置的記憶體。

### 6. 在 handle 還沒有值的時候使用它

先開啟中斷才建立佇列，或 ISR 用到尚未賦值的 task handle，都會當機。
原則：**先建立所有核心物件，再建立任務，最後才開啟中斷。**

### 7. 優先權設得太高

ESP-IDF 的系統任務有其優先權（例如 `esp_timer` 為 22、Wi-Fi 為 23）。
應用任務一般使用 1～10 即可；設得比系統任務高且長時間執行，會影響無線通訊與計時功能。

### 8. 高優先權任務忙碌等待

```c
while (!data_ready) { }   // 高優先權任務這樣寫，所有較低優先權的任務都會被餓死
```

這等於把超級迴圈的習慣帶進 RTOS。請改用佇列、號誌或任務通知來等待。

## 除錯工具

| 工具 | 用途 |
|---|---|
| `idf.py monitor` | 當機時自動把位址解碼成檔名與行號 |
| `uxTaskGetStackHighWaterMark()` | 檢查堆疊餘量 |
| `esp_get_free_heap_size()`、`esp_get_minimum_free_heap_size()` | 檢查 heap |
| `vTaskList()` | 列出所有任務的狀態、優先權、堆疊餘量。需啟用 `CONFIG_FREERTOS_USE_TRACE_FACILITY` 與 `CONFIG_FREERTOS_USE_STATS_FORMATTING_FUNCTIONS` |
| `vTaskGetRunTimeStats()` | 各任務佔用的 CPU 比例。需啟用 `CONFIG_FREERTOS_GENERATE_RUN_TIME_STATS` |
| `idf.py menuconfig` | 調整上述所有組態 |
| `idf.py size` | 檢視韌體各區段的大小 |

## 從超級迴圈轉換到 RTOS 的檢查清單

把既有的超級迴圈程式改寫為 FreeRTOS 時，可以依序檢查：

1. **劃分任務**：哪些工作在時序上彼此獨立？不必每件小事都變成任務；時序需求相近的工作可以放在同一個任務裡。
2. **決定優先權**：越短、越急的越高；耗時運算放最低。
3. **找出所有共用資料**：原本的全域變數，現在會被哪些任務存取？優先改用佇列傳遞，其次才是 mutex。
4. **把輪詢改成阻塞**：每一個「檢查旗標」都可以換成等待佇列、號誌或通知。
5. **把 `delay` 改成 `vTaskDelay`／`vTaskDelayUntil`**：確認沒有任何任務在忙碌等待。
6. **檢查 ISR**：改用 `FromISR` API，並把處理邏輯移到任務。
7. **實測堆疊**：以 high water mark 調整每個任務的堆疊大小。

## 延伸閱讀

- FreeRTOS 官方文件與免費書籍《Mastering the FreeRTOS Real Time Kernel》：<https://www.freertos.org/Documentation/>
- ESP-IDF 的 FreeRTOS 說明：<https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/system/freertos_idf.html>
- 本教程未涵蓋、值得接著學習的主題：event group、stream buffer／message buffer、tickless idle 與低功耗、`esp_timer` 高精度計時器、搭配 Wi-Fi 的事件迴圈（`esp_event`）。

---

回到[目錄](../README.md)
