# 第 0 章：環境建置與硬體

## 硬體

### 最低需求

- ESP32 開發板一塊（ESP32-DevKitC、NodeMCU-32S、DOIT DevKit V1 等皆可）
- 可傳輸資料的 USB 線（有些線只能充電）

所有範例只使用兩個接腳：

| 用途 | 預設接腳 | 說明 |
|---|---|---|
| LED | GPIO2 | 多數 ESP32 開發板的板載藍色 LED |
| 按鈕 | GPIO0 | 板上標示 `BOOT`（或 `IO0`）的按鈕，按下時為低電位 |

### 如果你的板子不一樣

| 狀況 | 處理方式 |
|---|---|
| 板上沒有可控制的 LED（例如官方 ESP32-DevKitC V4 只有電源燈） | 外接：GPIO2 → 330Ω 電阻 → LED 長腳，LED 短腳 → GND |
| ESP32-S3 / C3 / C6 等板子，板載的是 RGB（WS2812）LED | 這種 LED 無法用單純的高低電位控制，請外接一顆普通 LED，並修改 `LED_GPIO` |
| ESP32-C3 系列 | BOOT 按鈕通常在 GPIO9，請修改 `BUTTON_GPIO`；燒錄時 `set-target` 改為 `esp32c3` |

外接 LED 接線：

```
ESP32 GPIO2 ──[330Ω]──▶|── GND
                      LED
```

> **關於 GPIO0**：它是啟動模式的選擇腳。「按住 BOOT 再上電或重置」會進入燒錄模式而不執行程式，
> 所以請在程式開始執行之後才按按鈕。正常使用下不會有問題。

## 安裝 ESP-IDF（macOS）

以下為手動安裝方式。也可以改用 VS Code 的 **ESP-IDF 擴充套件**，由它引導完成安裝。

```bash
# 1. 必要工具
brew install cmake ninja dfu-util python3

# 2. 下載 ESP-IDF（本教程以 v5.x 撰寫，這裡取 v5.5 發行分支）
mkdir -p ~/esp
cd ~/esp
git clone -b release/v5.5 --recursive https://github.com/espressif/esp-idf.git

# 3. 安裝 ESP32 的編譯工具鏈（約需數 GB 空間，視網路需要一段時間）
cd ~/esp/esp-idf
./install.sh esp32          # 其他晶片可寫成 ./install.sh esp32,esp32c3,esp32s3

# 4. 載入環境變數（每次開新的終端機都要執行）
. ~/esp/esp-idf/export.sh
```

可以在 `~/.zshrc` 加一個別名，之後輸入 `get_idf` 即可：

```bash
alias get_idf='. $HOME/esp/esp-idf/export.sh'
```

確認安裝成功：

```bash
idf.py --version
```

Windows 與 Linux 的安裝方式請參考官方文件：
<https://docs.espressif.com/projects/esp-idf/en/stable/esp32/get-started/>

## 找到序列埠

接上開發板後執行：

```bash
ls /dev/cu.*
```

會多出類似 `/dev/cu.usbserial-0001`、`/dev/cu.SLAB_USBtoUART` 或 `/dev/cu.wchusbserial1420` 的裝置，那就是你的板子。
若沒有出現：

1. 換一條 USB 線（最常見的原因）。
2. 依板上的 USB 轉序列晶片安裝驅動程式（CP210x 或 CH340／CH9102）。

## 編譯、燒錄、監看

每個範例都是獨立的 ESP-IDF 專案，流程相同：

```bash
cd examples/01_superloop_blocking

idf.py set-target esp32                          # 選擇晶片，只需做一次
idf.py build                                     # 編譯
idf.py -p /dev/cu.usbserial-0001 flash monitor   # 燒錄並開啟監看
```

- 離開監看畫面：`Ctrl + ]`
- 在監看畫面中重置開發板：`Ctrl + T` 再按 `Ctrl + R`
- 若燒錄時卡在 `Connecting....`：按住 BOOT 按鈕直到開始寫入再放開

第一次編譯會把整個 ESP-IDF 編過一遍，需要幾分鐘；之後只會重新編譯有改動的部分。

## 專案結構說明

```
01_superloop_blocking/
├── CMakeLists.txt        專案層級的建置設定
├── sdkconfig.defaults    本範例需要的非預設組態
└── main/
    ├── CMakeLists.txt    元件層級的建置設定
    └── main.c            程式碼，進入點是 app_main()
```

`sdkconfig.defaults` 值得留意，本教程用它做兩件事：

| 設定 | 用在 | 原因 |
|---|---|---|
| `CONFIG_ESP_TASK_WDT_INIT=n` | 超級迴圈範例 | 超級迴圈不讓出 CPU，會觸發任務看門狗警告（詳見第 2 章） |
| `CONFIG_FREERTOS_HZ=1000` | FreeRTOS 範例 | 把系統 tick 從 10 ms 改為 1 ms，延遲與量測更精細 |

> 若你修改了 `sdkconfig.defaults`，需刪除專案目錄下的 `sdkconfig` 檔案後重新編譯才會生效。

## 一個必須先知道的事實

在 ESP-IDF 中，**FreeRTOS 永遠都在執行**——`app_main()` 本身就是由一個名為 `main` 的任務呼叫的。
因此本教程的「超級迴圈」範例，嚴格來說是「在單一任務裡寫超級迴圈，完全不使用任何 RTOS 功能」。

這對學習沒有影響：程式的結構、遇到的問題、反應時間的特性，都與在沒有作業系統的微控制器（裸機）上寫超級迴圈相同。
差別只在於背景仍有系統任務存在，這點會在第 2 章與第 9 章說明。

---

下一章：[超級迴圈 vs RTOS：核心概念](01-concepts.md)
