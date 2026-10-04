# 第 10 章：實機驗證紀錄（安裝的軟體與測試方法）

本章記錄這份教程「實際在硬體上跑過一遍」的完整過程：安裝了哪些軟體、用什麼方法建置與燒錄、如何擷取序列輸出，以及九個範例的實測結果。照著做可以重現同樣的驗證。

## 驗證環境

| 項目 | 內容 |
|---|---|
| 作業系統 | Linux（Fedora 44，kernel 7.1.x） |
| 開發板 | ESP32-D0WD rev v1.0（classic 雙核、240 MHz） |
| USB 轉序列晶片 | CH340（`QinHeng Electronics`） |
| 序列埠 | `/dev/ttyUSB0`（權限 `root:dialout 660`，使用者在 `dialout` 群組，燒錄免 `sudo`） |
| 驗證日期 | 2026-10-04 |
| 專案版本 | commit `d6a6084` |

確認晶片與通訊（燒錄前的煙霧測試）：

```bash
esptool --port /dev/ttyUSB0 chip-id
# Detecting chip type... ESP32
# Chip type: ESP32-D0WD (revision v1.0)   MAC: 9c:9c:1f:24:a7:80
```

## 安裝的軟體

| 軟體 | 版本 | 用途 | 取得方式 |
|---|---|---|---|
| ESP-IDF | v5.5（`release/v5.5` 分支） | 開發框架（含 FreeRTOS、建置系統、燒錄工具） | `git clone -b release/v5.5 --recursive` 到 `~/esp/esp-idf` |
| Xtensa 工具鏈 | `xtensa-esp-elf` 14.2.0、`xtensa-esp-elf-gdb` 17.1 | 交叉編譯 / 除錯 | `install.sh esp32` 自動下載到 `~/.espressif` |
| esptool | 4.12.0（IDF venv 內）/ 5.4.0（獨立 venv） | 燒錄與晶片辨識 | 隨 ESP-IDF 安裝；另裝獨立版做煙霧測試 |
| CMake / Ninja | 4.4.0 / 1.13.2 | 建置後端 | 系統既有 |
| uv | 0.12.23 | 取得獨立的 Python 3.12 | `pip install uv` |
| Python | 3.12.15（經 uv 安裝） | **ESP-IDF 的啟動直譯器** | `uv python install 3.12` |

### 關於 Python 版本（重要陷阱）

本機系統 Python 是 **3.14**，而 **ESP-IDF v5.5 不支援 3.14**，直接安裝會在相依套件階段失敗。
解法：用 `uv` 取得一個獨立的 **Python 3.12**，並在載入 ESP-IDF 前把它放到 `PATH` 最前面，
讓 `install.sh` / `export.sh` 的 `detect_python.sh` 選到 3.12：

```bash
pip install uv
uv python install 3.12
# 建立墊片，使 python / python3 都指向 3.12
mkdir -p /path/pybin
ln -sf "$(uv python find 3.12)" /path/pybin/python
ln -sf "$(uv python find 3.12)" /path/pybin/python3
export PATH="/path/pybin:$PATH"
```

安裝 ESP-IDF 工具鏈（只裝 esp32 目標）：

```bash
cd ~/esp/esp-idf
./install.sh esp32          # 會偵測到 python3 == 3.12.15 並通過相容性檢查
```

> 若你的系統 Python 本來就是 3.9–3.12，可略過 uv，直接照[第 0 章](00-setup.md)安裝即可。

## 載入環境

每開一個新終端機都要先載入 ESP-IDF 環境（記得墊片要在 `PATH` 前面）：

```bash
export IDF_PATH=~/esp/esp-idf
export PATH="/path/pybin:$PATH"     # 僅在系統 python 為 3.13+ 時需要
. "$IDF_PATH/export.sh"
idf.py --version                    # ESP-IDF v5.5
```

## 建置方法

每個 `examples/NN_*` 都是獨立的 ESP-IDF 專案，流程相同：

```bash
cd examples/03_rtos_tasks
idf.py set-target esp32     # 選晶片，每個專案只需一次
idf.py build               # 首次會把整個 IDF 編一遍（數分鐘）
```

九個範例全部建置通過，產物大小：

| 範例 | bin 大小 | 範例 | bin 大小 |
|---|---|---|---|
| 01_superloop_blocking | 0x287e0 | 06_rtos_isr | 0x2a2a0 |
| 02_superloop_nonblocking | 0x28810 | 07_rtos_timer_notify | 0x2ab90 |
| 03_rtos_tasks | 0x296b0 | 08_compare_superloop | 0x29420 |
| 04_rtos_queue | 0x29860 | 09_compare_rtos | 0x2a480 |
| 05_rtos_mutex | 0x27c50 | | |

（app 分區為 1 MB，各範例僅用約 16%，餘裕充足。）

## 燒錄與擷取輸出的方法

### 方法 A：官方互動式（教學推薦）

```bash
idf.py -p /dev/ttyUSB0 flash monitor
# 離開監看：Ctrl + ]
```

### 方法 B：非互動式擷取（本次驗證用，適合自動化）

`idf.py ... flash` 燒完即退出，再用一支小腳本以 DTR/RTS 重置開發板並擷取固定秒數的序列輸出：

```python
# capture.py：讀取序列埠 N 秒並印出
import sys, time, serial
ser = serial.Serial(sys.argv[1], 115200, timeout=0.2)
ser.setDTR(False); ser.setRTS(True); time.sleep(0.1)   # 觸發一次重置
ser.setRTS(False); time.sleep(0.1)
end = time.monotonic() + float(sys.argv[2])
while time.monotonic() < end:
    data = ser.read(4096)
    if data: sys.stdout.buffer.write(data); sys.stdout.flush()
```

```bash
idf.py -p /dev/ttyUSB0 flash
python capture.py /dev/ttyUSB0 14
```

> 注意：06、08、09 需要實體按下 BOOT 按鈕才會出現按鈕延遲／計數的數字；
> 本次自動擷取未按按鈕，故這些範例的 `button count=0`，其餘自動輸出均正確。

## 實測結果

全部九個範例皆成功燒錄並執行，行為與各章描述一致：

| # | 範例 | 實機輸出重點 | 驗證的概念 |
|---|---|---|---|
| 01 | superloop_blocking | `sensor=.. loop time=1300 ms` | 阻塞式迴圈被重運算卡住 1.3 s，按鈕反應遲鈍 |
| 02 | superloop_nonblocking | `loops≈1.45M, worst loop=53 us` | 非阻塞狀態機，迴圈緊湊、最差單圈僅數十 µs |
| 03 | rtos_tasks | `sensor` 每 1 s 穩定遞增 | 任務、`vTaskDelay`、搶佔式排程 |
| 04 | rtos_queue | `seq=N value=X (in queue for 20 us)` | 任務間用佇列傳資料，延遲約 20 µs |
| 05 | rtos_mutex | 無鎖 `lost=52902`／有鎖 `lost=0` | 競爭條件造成資料遺失，mutex 完全消除 |
| 06 | rtos_isr | 啟動正常，等待中斷 | ISR + 延遲處理（按鈕延遲需實體按鍵觀察） |
| 07 | rtos_timer_notify | `heartbeat` 每 2 s、堆疊/heap 監看每 5 s | 軟體計時器、任務通知、堆疊餘量監控 |
| 08 | compare_superloop | **`LED error max≈171993 us`** | 超級迴圈在負載下 LED 時序抖動達 ~172 ms |
| 09 | compare_rtos | **`LED error max=2 us`** | 同一應用改用 FreeRTOS，時序誤差降到 ~2 µs |

**核心結論在實機得到驗證**：第 8 章與第 9 章做同一件事，超級迴圈的 LED 時序誤差高達約
**172 ms**，改用 FreeRTOS 後降到約 **2 µs**——相差約五個數量級，正是本教程的主旨。

05 的實測一例：

```
no mutex:     expected=200000 actual=147098 lost=52902 time=27 ms
with mutex:   expected=200000 actual=200000 lost=0     time=3789 ms
```

---

上一章：[ESP32 的特殊之處與常見陷阱](09-esp32-notes.md)
