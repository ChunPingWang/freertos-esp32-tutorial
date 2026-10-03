# 第 5 章：互斥鎖與競爭條件

前面幾章都在講 RTOS 的好處。這一章要面對它帶來的最大麻煩：**共用資源**。
這是從超級迴圈轉到 RTOS 時最容易踩到的坑。

程式：[`examples/05_rtos_mutex/main/main.c`](../examples/05_rtos_mutex/main/main.c)

## 競爭條件

兩個任務各自執行 100,000 次 `counter++`，結果應該是 200,000。但 `counter++` 並不是一個動作，而是三個：

```c
uint32_t tmp = s_counter;   // 1. 讀
tmp++;                      // 2. 改
s_counter = tmp;            // 3. 寫
```

若兩個任務交錯執行：

```
任務 A              任務 B              s_counter
讀到 100                                  100
                    讀到 100              100
加 1 → 101
                    加 1 → 101
寫入 101                                  101
                    寫入 101              101   ← 加了兩次，只增加 1
```

一次更新就這樣遺失了。結果取決於兩個任務執行的時序，這就是**競爭條件（race condition）**。

在超級迴圈中這個問題不存在，因為工作不會交錯執行（除了 ISR）。
在單核 RTOS 上，它發生在任務剛好於「讀」與「寫」之間被搶佔時——機率低，所以更可怕：測試時一切正常，出貨後偶爾出錯。
在 ESP32 這樣的雙核晶片上，兩個任務可以**真正同時**執行，問題會大量出現。本範例就把兩個任務放在不同核心來突顯它。

## 互斥鎖（mutex）

```c
SemaphoreHandle_t mutex = xSemaphoreCreateMutex();

xSemaphoreTake(mutex, portMAX_DELAY);   // 上鎖：若別人持有，就阻塞等待
/* 臨界區段：同一時間只有一個任務能在這裡 */
xSemaphoreGive(mutex);                  // 解鎖
```

使用原則：

1. **誰上鎖誰解鎖**，而且每條執行路徑（包含錯誤處理的提早返回）都要解鎖。
2. **持有的時間越短越好**。不要在持有鎖的時候做耗時的事或呼叫 `vTaskDelay`。
3. **不可以在 ISR 中使用 mutex**。ISR 不能阻塞。
4. 所有存取該資源的地方都要上鎖，漏掉一處就等於沒有保護。

## 實驗

```bash
cd examples/05_rtos_mutex
idf.py set-target esp32
idf.py -p <你的序列埠> flash monitor
```

程式每回合會跑兩次：先不加鎖，再加鎖。不需要按任何按鈕。

### 預期觀察

輸出的形式如下（數值為示意，每次都會不同）：

```
no mutex:     expected=200000 actual=1xxxxx lost=xxxxx time=xx ms
with mutex:   expected=200000 actual=200000 lost=0 time=xxxx ms
```

- **不加鎖**：`actual` 小於 200,000，`lost` 每回合都不一樣。
- **加鎖**：`actual` 永遠等於 200,000。
- **加鎖的 `time` 明顯較長**（預期是數十倍以上）。正確性是有代價的：每次上鎖、解鎖都要進入核心，鎖被佔用時還會發生上下文切換。

> 若使用單核心晶片（如 ESP32-C3），不加鎖時 `lost` 可能很小甚至是 0。
> 這不代表程式是對的，只代表這次運氣好。競爭條件最危險的地方正在於此。

## Mutex 的進階議題

### 優先權反轉（priority inversion）

```
高優先權 H：        想拿鎖 ─── 等待 ──────────────────▶ 拿到鎖
中優先權 M：              ████████████████████ (與鎖無關，卻一直執行)
低優先權 L：  拿到鎖 █                          █ 解鎖
```

L 持有鎖時被 M 搶佔，H 在等 L 解鎖，結果變成 H 在等 M——高優先權任務被不相干的中優先權任務拖住了。

FreeRTOS 的 mutex 內建**優先權繼承（priority inheritance）**：H 等待 L 持有的鎖時，L 的優先權會暫時被提升到與 H 相同，使 M 無法插隊。
這也是 mutex 與 binary semaphore 的關鍵差異——**保護資源請用 mutex，不要用 binary semaphore**。

### 死結（deadlock）

```
任務 A：拿鎖 1 → 想拿鎖 2（被 B 持有）→ 等待
任務 B：拿鎖 2 → 想拿鎖 1（被 A 持有）→ 等待     兩者永遠互等
```

預防方法：

- 所有任務以**相同的順序**取得多個鎖。
- `xSemaphoreTake` 設定 timeout 而不是 `portMAX_DELAY`，逾時就放棄並回報錯誤。
- 盡量避免同時持有多個鎖。

### 更好的做法：不要共用

最可靠的同步方式是避免共用。常見的設計是讓**一個任務獨佔一項資源**，其他任務透過佇列對它發出請求：

```
task A ──┐
task B ──┼──▶ [ queue ] ──▶ display_task（唯一會碰螢幕的任務）
task C ──┘
```

這樣連 mutex 都不需要。第 4 章的範例就是這種風格。

### 極短的臨界區段

若只是要保護幾道指令（例如更新一個 64 位元變數），可以直接關閉中斷：

```c
static portMUX_TYPE s_lock = portMUX_INITIALIZER_UNLOCKED;

portENTER_CRITICAL(&s_lock);
s_counter++;
portEXIT_CRITICAL(&s_lock);
```

這在 ESP32 上是關中斷加上自旋鎖（spinlock），速度比 mutex 快得多，在 ISR 中也有對應的版本（`portENTER_CRITICAL_ISR`）。
但臨界區段內**絕對不能阻塞，也不能耗時**，因為期間所有中斷都被擋住。

## 動手改改看

1. 把 `worker_task` 的 mutex 換成上面的 `portENTER_CRITICAL`／`portEXIT_CRITICAL`，比較耗時。
2. 把 `CORE_B` 改成 `0`，讓兩個任務在同一顆核心上。觀察不加鎖時 `lost` 的變化，並思考為什麼。

## 與超級迴圈的對照

| | 超級迴圈 | FreeRTOS |
|---|---|---|
| 工作之間共用變數 | 安全 | 必須保護 |
| 與 ISR 共用變數 | 需要保護（關中斷） | 需要保護（臨界區段，或改用佇列） |
| 可能出現的錯誤 | — | 競爭條件、死結、優先權反轉 |

這是 RTOS 最主要的「成本」：它不是花在 CPU 或記憶體上，而是花在**你必須思考並行**。

## 小結

- 多個任務存取同一份資料（且至少一方會寫入）時，必須保護。
- 保護資源用 mutex；它有優先權繼承，但不能用在 ISR。
- 能用佇列傳遞資料、避免共用，是更好的設計。

---

下一章：[中斷與延遲處理](06-interrupts.md)
