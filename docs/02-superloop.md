# 第 2 章：超級迴圈實作

本章用兩個範例體驗超級迴圈：先看最直覺的寫法會遇到什麼問題，再看標準的改良方式，以及改良之後仍然存在的極限。

三件工作貫穿整份教程：

1. LED 每 500 ms 切換
2. 讀取感測器（模擬為耗時 300 ms 的動作）
3. 偵測按鈕

## 範例 01：阻塞式超級迴圈

程式：[`examples/01_superloop_blocking/main/main.c`](../examples/01_superloop_blocking/main/main.c)

### 程式重點

```c
while (1) {
    gpio_set_level(LED_GPIO, 1);
    delay_ms(500);
    gpio_set_level(LED_GPIO, 0);
    delay_ms(500);

    int value = read_sensor_blocking();      // 300 ms

    if (gpio_get_level(BUTTON_GPIO) == 0) {  // 每圈只看這一瞬間
        ...
    }
}
```

這是初學者最自然的寫法：想等 500 ms 就呼叫 `delay`。這裡的 `delay_ms()` 用的是 `esp_rom_delay_us()`，它讓 CPU 原地空轉，與裸機上的 delay 行為相同。

### 實驗

```bash
cd examples/01_superloop_blocking
idf.py set-target esp32
idf.py -p <你的序列埠> flash monitor
```

1. 觀察終端機，每圈會印出 `loop time`。
2. **快速按一下** BOOT 按鈕（按下立刻放開），重複幾次。
3. **按住** BOOT 按鈕 2 秒以上再放開。

### 預期觀察

- `loop time` 約為 1300 ms（500 + 500 + 300）。
- 短按幾乎都不會被偵測到：一圈 1300 ms 之中，程式只在最後一瞬間看按鈕一眼。
- 按住才偵測得到，而且從按下到印出訊息，最久要等 1.3 秒。

### 問題出在哪

`delay` 期間 CPU 明明沒事做，卻不能去做別的工作。
時間被「等待」佔住了，而等待本身沒有產出。

## 範例 02：非阻塞式超級迴圈

程式：[`examples/02_superloop_nonblocking/main/main.c`](../examples/02_superloop_nonblocking/main/main.c)

### 程式重點

解法是把「等待」改成「檢查時間到了沒」：

```c
static void led_job(uint32_t now)
{
    static uint32_t last_toggle;
    static int level;

    if (now - last_toggle >= LED_PERIOD_MS) {   // 時間到了才做事
        last_toggle = now;
        level = !level;
        gpio_set_level(LED_GPIO, level);
    }
    // 時間沒到：立刻返回，把 CPU 讓給下一件工作
}
```

主迴圈變得非常快：

```c
while (1) {
    uint32_t now = millis();
    led_job(now);
    sensor_job(now);
    button_job(now);
}
```

注意每個 job 都要用 `static` 變數**自己記住狀態**（上次執行的時間、LED 目前的電位、按鈕去彈跳進行到哪裡）。
每個 job 實際上是一個小型**狀態機**。這是超級迴圈的標準寫法。

> `now - last_toggle` 使用無號數相減，即使 `millis()` 溢位歸零，算出來的時間差仍然正確。
> 請避免寫成 `now >= last_toggle + PERIOD`，溢位時會出錯。

### 實驗 A：一切順利的情況

確認 `main.c` 中 `SIMULATE_SLOW_SENSOR` 為 `0`，燒錄後：

1. 快速按 BOOT 按鈕數次。
2. 觀察每 5 秒印出的 `[report]`。

**預期觀察**

- 每次短按都會被偵測到。
- `loops` 的數字非常大（每 5 秒數十萬圈以上），`worst loop` 通常只有數毫秒以內（最慢的一圈多半是在 `printf`）。

### 實驗 B：出現一件「真的很慢」的工作

把 `SIMULATE_SLOW_SENSOR` 改為 `1`，重新燒錄：

1. 再次快速按 BOOT 按鈕多次。
2. 觀察 `[report]`。

**預期觀察**

- `worst loop` 變成約 300,000 us。
- 部分短按又開始漏掉（按下與放開都落在那 300 ms 之內時）。
- 仔細看 LED，會發現閃爍節奏偶爾不均勻。

### 這說明了什麼

非阻塞寫法解決的是「等待」，但解決不了「真的需要 CPU 時間的工作」。
若感測器讀取、資料運算、畫面更新本身就要 300 ms，在超級迴圈中只有兩條路：

1. **手動拆解**：把 300 ms 的工作拆成 30 個 10 ms 的步驟，寫成更複雜的狀態機。能不能拆、好不好拆，取決於那件工作的性質（第三方函式庫的函式通常拆不了）。
2. **移到中斷裡做**：只適用於極短的工作。

而 RTOS 提供了第三條路：**讓排程器替你切**。下一章就來實作。

## 補充：為什麼要關閉看門狗？

兩個超級迴圈範例的 `sdkconfig.defaults` 都有這一行：

```
CONFIG_ESP_TASK_WDT_INIT=n
```

ESP-IDF 預設啟用**任務看門狗（Task Watchdog）**，它會監看 idle task 是否有機會執行。
超級迴圈永遠不讓出 CPU，idle task 永遠輪不到，看門狗每 5 秒就會印出一次警告。

這個警告本身就是很好的提示：**在 RTOS 的世界裡，「霸佔 CPU 不放」被視為一種錯誤**。
此處為了重現裸機超級迴圈的行為，才刻意把它關掉。

## 小結

| | 阻塞式 | 非阻塞式 |
|---|---|---|
| 寫法 | 直覺，照順序寫 | 每件工作改寫成狀態機 |
| 等待時 CPU 能否做別的事 | 不能 | 能 |
| 遇到耗時運算 | 全部卡住 | 仍然全部卡住 |
| 程式可讀性 | 高 | 隨工作數量增加而下降 |

## 思考題

1. 範例 01 中，若把「檢查按鈕」在迴圈裡多放幾次（每個 delay 之後各放一次），能改善多少？這種做法的缺點是什麼？
2. 範例 02 的 `button_job` 需要幾個 `static` 變數？若有 5 顆按鈕，你會如何改寫？
3. 若範例 02 的慢速感測器無法拆解，而你又不能使用 RTOS，還有什麼辦法讓按鈕不漏掉？（提示：中斷。第 8 章的範例 08 就是這樣做的，但它仍有極限。）

---

下一章：[任務與排程](03-tasks.md)
