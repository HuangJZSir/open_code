# 第三方组件与资源声明

本仓库中并非所有文件都受根目录 [`LICENSE`](LICENSE)（PolyForm Noncommercial 1.0.0）约束。
**本项目的协议只覆盖 HJZ_YTLZ 原创的代码**（权利人标识见 `LICENSE` 顶部的 `Required Notice` 行），`components/` 下的第三方组件各自保留原有许可证，
任何人均可单独按原许可证使用它们（包括商业用途），本项目无权对其附加限制。

| 路径 | 组件 | 版本 | 许可证 | 版权 |
|---|---|---|---|---|
| `components/lvgl/` | [LVGL](https://github.com/lvgl/lvgl) | 9.0.0 | MIT（`components/lvgl/LICENCE.txt`） | © LVGL Kft |
| `components/lvgl_esp32_drivers/` | [lvgl_esp32_drivers](https://github.com/lvgl/lvgl_esp32_drivers) | — | Apache-2.0（文件头） | © 2021 Espressif Systems (Shanghai) Co. Ltd. |
| `components/lvgl_touch/` | GT911 / FT62XX / CST3240 触摸驱动 | — | Apache-2.0（文件头） | © 2021 Espressif Systems (Shanghai) Co. Ltd. |
| `components/esp_audio_codec/` | [esp_audio_codec](https://github.com/espressif/esp-adf-libs) | 2.2.1 | ESPRESSIF MIT（`components/esp_audio_codec/LICENSE`） | © 2024 Espressif Systems (Shanghai) Co., Ltd. |
| `components/esp_audio_codec/lib/esp32s3/*.a` | 预编译解码库（`libesp_audio_codec.a`、`libesp_audio_simple_dec.a`） | 2.2.1 | 同上，随许可证声明再分发 | © 2024 Espressif Systems (Shanghai) Co., Ltd. |

外部依赖（不在本仓库内，由 ESP-IDF 在构建时提供）：

- **ESP-IDF** —— Apache-2.0，© Espressif Systems
- **FreeRTOS** —— MIT，© Amazon.com, Inc.

## 关于字体资源

`main/fonts/ui_font_16.c`、`ui_font_24.c`、`ui_font_32.c`、`ui_font_cjk24.c` 由
[`tools/gen_font.ps1`](tools/gen_font.ps1) 栅格化生成，来源字体为
**[Noto Sans SC](https://fonts.google.com/noto/specimen/Noto+Sans+SC)（SIL Open Font License 1.1）**。
该字体允许自由使用、修改与再分发（含商用），因此本项目不含任何专有字体的衍生数据。

重新生成（需本机已安装 Noto Sans SC）：

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\gen_font.ps1 -Family "Noto Sans SC"
```

生成的位图是本项目代码的一部分，随本项目协议分发；任何人也可随时用上述命令从 OFL 字体重建。
OFL 1.1 的义务针对「Font Software」本身，不针对嵌入软件中的栅格化位图。

`components/lvgl/src/font/` 下的字体文件属于 LVGL 上游发行内容，随 LVGL 按其
MIT 许可证分发，不在本项目声明范围内。

## 关于图标

`main/ui/icons.c` 由 [`tools/gen_icons.ps1`](tools/gen_icons.ps1) 程序化绘制生成
（几何图元 + 超采样降采样），不包含任何第三方图标字体或图标集，属原创代码，
受根目录 `LICENSE` 约束。
