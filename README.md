# ESP32 + FreeRTOS 教程：從超級迴圈到即時作業系統

這份教程用「同一組工作、兩種寫法」的方式，帶你理解 **無窮迴圈（superloop）架構** 與 **FreeRTOS** 的差異，並在 ESP32 上實際量測出來。

- 開發框架：ESP-IDF v5.x（內建 FreeRTOS，使用原生 API）
- 硬體需求：一塊 ESP32 開發板 + USB 線。所有實驗只用到板上的 LED 與 BOOT 按鈕
- 程式語言：C

> **目前狀態**：文章與程式碼已完成，但**尚未以 ESP-IDF 實際編譯，也尚未上板測試**。
> 文中的輸出與數字皆為依原理推估的「預期結果」，實測數值請填入[第 8 章](docs/08-comparison.md)的紀錄表。

## 章節

| 章 | 文章 | 範例 | 主題 |
|---|---|---|---|
| 0 | [環境建置與硬體](docs/00-setup.md) | — | 安裝 ESP-IDF、接線、燒錄流程 |
| 1 | [超級迴圈 vs RTOS：核心概念](docs/01-concepts.md) | — | 兩種架構的原理、優缺點、選擇時機 |
| 2 | [超級迴圈實作](docs/02-superloop.md) | `01` `02` | 阻塞式、非阻塞式（狀態機）寫法與其極限 |
| 3 | [任務與排程](docs/03-tasks.md) | `03` | 任務、優先權、搶佔、`vTaskDelay` |
| 4 | [佇列](docs/04-queue.md) | `04` | 任務間傳遞資料 |
| 5 | [互斥鎖與競爭條件](docs/05-mutex.md) | `05` | 共用資源、mutex、優先權反轉 |
| 6 | [中斷與延遲處理](docs/06-interrupts.md) | `06` | ISR、`FromISR` API、反應時間量測 |
| 7 | [軟體計時器與任務通知](docs/07-timers-notifications.md) | `07` | timer、notification、堆疊監控 |
| 8 | [總結比較：實測兩種架構](docs/08-comparison.md) | `08` `09` | 同一應用兩種寫法的量化比較 |
| 9 | [ESP32 的特殊之處與常見陷阱](docs/09-esp32-notes.md) | — | 雙核、看門狗、堆疊單位、除錯技巧 |

建議依序閱讀。若時間有限，讀第 1 章後直接跳第 8 章，就能得到「兩種架構差在哪」的完整答案。

## 快速開始

```bash
# 1. 載入 ESP-IDF 環境（每開一個新終端機都要做一次，安裝方式見第 0 章）
. ~/esp/esp-idf/export.sh

# 2. 進入任一範例
cd examples/01_superloop_blocking

# 3. 選擇晶片（每個範例只需做一次）
idf.py set-target esp32

# 4. 編譯、燒錄、開啟序列埠監看（Ctrl+] 離開）
idf.py -p /dev/cu.usbserial-XXXX flash monitor
```

## 接腳

所有範例開頭都有這兩行，若你的開發板不同請自行修改：

```c
#define LED_GPIO     GPIO_NUM_2   // 板載 LED
#define BUTTON_GPIO  GPIO_NUM_0   // BOOT 按鈕，按下為低電位
```

各種開發板的對應方式見[第 0 章](docs/00-setup.md#硬體)。

## 目錄結構

```
.
├── README.md
├── docs/                 教學文章
└── examples/             每個子目錄都是一個獨立的 ESP-IDF 專案
    ├── 01_superloop_blocking/
    ├── 02_superloop_nonblocking/
    ├── 03_rtos_tasks/
    ├── 04_rtos_queue/
    ├── 05_rtos_mutex/
    ├── 06_rtos_isr/
    ├── 07_rtos_timer_notify/
    ├── 08_compare_superloop/
    └── 09_compare_rtos/
```
