# open_code — ESP32-S3 桌面式音乐播放器

> 作者：**[@HuangJZSir](https://github.com/HuangJZSir)**
> · 反馈与商业授权：[开 Issue](https://github.com/HuangJZSir/open_code/issues)

一个跑在 ESP32-S3 + 800×480 RGB 触摸屏上的「类手机桌面」固件：开机进入带图标的桌面，
点图标进入 APP。目前完整可用的是**音乐播放器**（读 SD 卡里的 mp3 / flac / wav / m4a / aac），
另有**设置**、**关于**两个页面，并板载一个 Web 控制台。

> 许可证：**PolyForm Noncommercial License 1.0.0** —— 源码可见，允许非商业使用，
> **禁止任何商业用途**。详见 [许可证](#许可证) 与 [`LICENSE`](LICENSE)。

## 功能

- 桌面启动器：4×2 圆角图标网格，触摸进入 APP；视频 / 图片 / 文件 / 时钟 / 计算器为占位页
- 音乐播放器
  - 支持 `mp3` `flac` `wav` `mp4`(m4a) `aac` `ts`
  - 按文件夹扫描 SD 卡曲库，曲目列表滑屏滚动，当前曲高亮
  - 播放 / 暂停 / 上一首 / 下一首 / 循环 / 随机 / 进度拖动 / 音量
  - 歌曲名走 GB2312 一级字库中文显示；封面区目前是渐变占位块（不解析内嵌封面图）
- 设置：Wi-Fi（STA + AP 并存）、背光亮度、音量、时间
- 时间：SNTP 校时，断网时用本地 RTC 计时
- Web 控制台：手机连上设备热点后，浏览器访问 `http://192.168.4.1` 即可看状态、管理曲库、上传 / 删除文件
- 中文界面：16 / 24 / 32 px ASCII 位图字 + 24 px GB2312 一级字库，全部为静态位图，运行时不依赖文件系统

## 硬件

| 项 | 值 |
|---|---|
| MCU | ESP32-S3，16MB Flash + 8MB Octal PSRAM（N16R8） |
| 屏幕 | 5 寸 800×480 RGB 并口屏，16bpp |
| 触摸 | 电容触摸（GT911 / FT62XX / CST3240，见 `components/lvgl_touch`） |
| 音频 | ES8311 codec + I2S |
| 存储 | SD 卡（sdmmc 4 线） |

**引脚定义唯一来源是 [`components/lvgl_esp32_drivers/include/bsp_board.h`](components/lvgl_esp32_drivers/include/bsp_board.h)**，
移植到别的板子时只改这一个文件，不要在业务代码里写死引脚号。

## 目录结构

```
main/
  main.c            启动流程
  app_audio.c       I2S + ES8311 输出通路
  app_player.c      解码 / 播放线程 / 曲库扫描状态机
  app_store.c       曲库索引与文件操作
  app_httpd.c       Web 服务与 REST API
  app_wifi.c        STA + AP 双模、SNTP
  app_settings.c    NVS 持久化配置
  app_time.c        RTC / 时钟
  app_bus.c         跨模块事件总线
  app_cast.c        JPEG 投屏接收与解码显示
  ui/               launcher / 音乐 / 设置 / 导航 / 主题 / 图标位图
  fonts/            位图字体（由 tools/gen_font.ps1 生成）
components/         第三方组件（LVGL、屏幕驱动、预编译音频解码库），见 THIRD_PARTY_NOTICES.md
tools/
  gen_font.ps1      由系统字体生成 LVGL 位图字体
  gen_icons.ps1     程序化生成 48×48 UI 图标
  check_encoding.ps1 检查源文件编码（UTF-8 无 BOM / 无全角符号）
  send_screen.ps1   把 PC 屏幕推送到设备（Windows）
  send_screen.py    同上，跨平台 Python 版本
partitions.csv      factory 5M + storage 2M(fat) + kids 8M(fat)
```

## 编译与烧录

要求 **ESP-IDF v5.4.3**（其它版本未验证，5.5.x 有 API 变更，会编不过）。

```bash
idf.py set-target esp32s3
idf.py build
idf.py -p COMx flash monitor
```

`sdkconfig` 已随仓库提交，是这块板子上验证过的配置。修改它之前请先读这几项，它们有明确原因：

| 配置 | 值 | 原因 |
|---|---|---|
| `CONFIG_FREERTOS_HZ` | `1000` | tick 1ms。默认 100 时 `vTaskDelay(1)` 是 10ms，音频线程轮询粒度太粗会卡顿 |
| `CONFIG_LOG_DEFAULT_LEVEL` | `3` (INFO) | **不要调回 VERBOSE(5)**：sdmmc 驱动会为每个 512 字节扇区打一行日志，115200 波特率下把读卡吞吐压到 ~76KB/s，音乐必然卡 |
| `CONFIG_SPIRAM_FETCH_INSTRUCTIONS` / `SPIRAM_RODATA` | `y` | 必须保持。这块板的 LCD 用 PSRAM 帧缓冲 + 内部 RAM bounce buffer，代码和常量留在 flash 会和屏幕 DMA 抢 flash |
| `LV_COLOR_DEPTH` | `16` | 与屏参和图标位图布局绑定，改了图标会乱码 |

改完设备端文案后需要重跑 `powershell -File tools\gen_font.ps1`，脚本会扫描
`main/**/*.c|h` 里出现的非 ASCII 字符，把新字形补进位图字体。

## Web 控制台

设备默认开 AP，SSID 形如 `MusicBox-XXXX`（后四位是 MAC），密码可在设置页修改。连上后用
浏览器打开 `http://192.168.4.1`。API：

| 方法 | 路径 | 说明 |
|---|---|---|
| GET | `/` | 控制台页面 |
| GET | `/api/status` | 播放状态、当前曲目、Wi-Fi 信息 |
| GET | `/api/tracks` | 曲库列表 |
| POST | `/api/rescan` | 重新扫描 SD 卡 |
| POST | `/api/control` | 播放控制 |
| POST | `/api/upload` | 上传文件到 SD 卡 |
| POST | `/api/delete` | 删除文件 |
| POST | `/api/cast` | 请求体为 JPEG，解码后全屏显示 |
| POST | `/api/cast/stop` | 退出投屏，返回桌面 |

## 手机投屏

> **该功能当前默认关闭**：`main/app_cast.h` 里的 `APP_CAST_ENABLE` 为 `0`，控制台不会显示按钮、
> `/api/cast` 也不注册。改成 `1` 重新编译即可启用。

控制台页面里有一个「**手机拍照投屏**」按钮：点了会调起手机系统相机，拍照后自动缩到
640×480 并上传到设备，设备全屏显示这张照片。

```text
手机浏览器 ──拍照──> canvas 缩图 ──JPEG──> POST /api/cast ──TJpgDec──> RGB 屏
```

使用步骤：

1. 手机连设备热点（`MusicBox-XXXX`）或与设备同局域网
2. 浏览器打开 `http://192.168.4.1`
3. 点「手机拍照投屏」→ 用系统相机拍一张 → 状态栏显示「已投屏 640×480」
4. 触摸设备屏幕任意位置，或点网页上的「退出投屏」，设备回到桌面

### 为什么是「拍照」而不是实时视频流

浏览器要连续读取摄像头必须调用 `getUserMedia()`，而该接口**只在安全上下文可用**
（HTTPS、localhost 或 file://）。`http://192.168.4.1` 不满足条件，Android Chrome 上
`navigator.mediaDevices` 直接是 `undefined`，iOS Safari 同样拒绝，自签名 HTTPS 也无解。
所以这里改用 `<input type="file" capture="environment">` 调起系统相机——它不经过
`getUserMedia`，两个平台都能用，也不需要证书。代价是"拍一张传一张"，不是连续视频。

### 电脑端（可选）

想知道连续画面能跑多少帧，可以用 PC 发送端：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\send_screen.ps1 -Device 192.168.4.1 -Width 320 -Fps 8
```

或 Python 版（需 `pip install mss pillow requests`）：

```bash
python tools/send_screen.py --host 192.168.4.1 --width 320 --fps 8
```

实测 320 宽约 8–12 fps，640 宽约 2–5 fps（瓶颈是软解和 WiFi 吞吐）。

### 传输协议

`POST /api/cast` 的请求体就是一张完整 JPEG，上限 **128 KB**。设备端会按需自动降采样
（1/2、1/4、1/8）把解码结果控制在 640×480 以内，再最近邻放大、黑边居中，然后直接写进
面板帧缓冲。所有缓冲**首帧才申请**，不占开机内存（典型 320 宽画面约 240 KB PSRAM）。
超出上限返回 413，内存不足返回 503。

## 许可证

本项目**原创部分**（`main/` 下除 `fonts/` 以外的源码、`ui/`、`tools/` 脚本、根目录构建文件、
`partitions.csv`、`sdkconfig`）采用
[**PolyForm Noncommercial License 1.0.0**](https://polyformproject.org/licenses/noncommercial/1.0.0)，全文见 [`LICENSE`](LICENSE)。

- ✅ 允许：个人学习、爱好项目、研究实验、教学、慈善 / 公共研究 / 公共卫生 / 环保 / 政府机构使用；可修改、可分发（需随附本协议与 `Required Notice` 行）
- ❌ 禁止：任何商业用途（含企业内部生产使用、以本项目为基础的商品、付费服务、嵌入商用产品）
- ❗ 本协议**不是 OSI 认证的开源协议**，准确说法是 **source-available（源码可见、非商业许可）**。请不要称本项目为 "open source"
- 💼 商业授权：如需商用请在本仓库开 Issue，标题注明「商业授权」并说明用途与规模，我会提供单独的商业许可

### 不在本协议范围内的部分

`components/` 下的第三方组件、预编译二进制库、字体资源**不受**上述协议约束，
它们各自保留原有许可证，详见 [`THIRD_PARTY_NOTICES.md`](THIRD_PARTY_NOTICES.md)。
本项目协议仅覆盖作者原创代码，不能也不会对第三方部分「再授权」。

## 贡献

欢迎 Issue 与 Pull Request。提交即表示你同意你的贡献同样以 PolyForm Noncommercial 1.0.0 授权，
并允许项目作者将其用于商业授权（否则作者无法对整份代码做商业许可）。若你不同意，请只提 Issue 而不提交代码。

## 作者与联系

- 作者：**HJZ_YTLZ** · GitHub [@HuangJZSir](https://github.com/HuangJZSir)
- 反馈、建议、商业授权询问：**只走 [Issues](https://github.com/HuangJZSir/open_code/issues)**，不开邮件渠道。
  公开讨论便于其他人检索已有答案，也避免私信遗漏。
- 版权声明：见 [`LICENSE`](LICENSE) 顶部的 `Required Notice` 行，那一行是本仓库的正式权利人标识。

## 致谢

[LVGL](https://lvgl.io)、[lvgl_esp32_drivers](https://github.com/lvgl/lvgl_esp32_drivers)、
[ESP-IDF](https://github.com/espressif/esp-idf)、[esp_audio_codec](https://github.com/espressif/esp-adf-libs)，
以及 Espressif 开源的各类 ESP32 示例。

位图字体的源字体为 [Noto Sans SC](https://fonts.google.com/noto/specimen/Noto+Sans+SC)（SIL Open Font License 1.1）。
