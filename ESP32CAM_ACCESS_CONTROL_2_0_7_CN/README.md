# ESP32-CAM 门禁识别版 Arduino-ESP32 2.0.7

这是一个不使用巴法云的本地门禁识别版本。

它保留原作者网页门禁项目的思路：

```text
网页预览 -> 录入人脸 -> 门禁识别 -> 识别成功开门
```

同时适配 Arduino-ESP32 core 2.0.7 的新版人脸识别 API。

## 当前功能

- 中文网页预览
- 检测人脸
- 录入人脸
- 门禁识别
- 删除单个人脸
- 删除全部人脸
- 识别成功后继电器开门
- 绿灯、红灯、蜂鸣器提示
- 网页收到开门消息后播放提示音

## 文件说明

| 文件 | 作用 |
| --- | --- |
| `ESP32CAM_ACCESS_CONTROL_2_0_7_CN.ino` | 主程序 |
| `HTML_interface.html` | 中文网页源码 |
| `camera_index.h` | 压缩后的网页，ESP32 实际发送这个 |
| `camera_pins.h` | ESP32-CAM 摄像头引脚 |
| `partitions.csv` | 带 `fr` 人脸分区的分区表 |

## 使用环境

推荐：

- AI Thinker ESP32-CAM
- OV2640 摄像头
- Arduino-ESP32 core 2.0.7
- Arduino IDE 2.x
- 有 PSRAM 的 ESP32-CAM

## 需要先修改 WiFi

打开主程序：

```text
ESP32CAM_ACCESS_CONTROL_2_0_7_CN.ino
```

修改：

```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
```

## 默认硬件引脚

```cpp
#define green_pin 12
#define red_pin 13
#define relay_pin 15
#define buzzer_pin 14
```

默认电平：

```cpp
const uint8_t RELAY_IDLE_LEVEL = HIGH;
const uint8_t RELAY_OPEN_LEVEL = LOW;
```

意思是：

- 平时继电器是 `HIGH`
- 开门时继电器拉到 `LOW`

如果你的继电器模块是相反的，就改成：

```cpp
const uint8_t RELAY_IDLE_LEVEL = LOW;
const uint8_t RELAY_OPEN_LEVEL = HIGH;
```

蜂鸣器默认：

```cpp
const uint8_t BUZZER_IDLE_LEVEL = HIGH;
const uint8_t BUZZER_ON_LEVEL = LOW;
```

如果蜂鸣器电平相反，也可以互换。

## 网页按钮

网页上有 4 个主要按钮：

| 按钮 | 作用 |
| --- | --- |
| `视频预览` | 只看画面，不做人脸判断 |
| `检测人脸` | 只判断有没有脸，不开门 |
| `录入人脸` | 输入姓名后录入人脸 |
| `门禁识别` | 判断是不是已录入人脸，成功后开门 |

## 录入流程

1. 打开串口监视器，波特率 `115200`。
2. 上传程序。
3. 串口看到 ESP32-CAM 的 IP 地址。
4. 浏览器打开这个 IP。
5. 输入姓名。
6. 点击 `录入人脸`。
7. 对准摄像头，等待录入 5 次。
8. 网页显示 `xxx 录入完成`。

## 门禁流程

1. 点击 `门禁识别`。
2. 摄像头检测到人脸。
3. 如果匹配到已录入的人：

```text
门禁通过：名字
门已打开：名字
```

继电器触发，绿灯亮，蜂鸣器响一声。

4. 如果没有匹配：

```text
门禁拒绝：不是已录入人脸
```

红灯亮，蜂鸣器响两声。

5. 门默认打开 5 秒，然后继电器自动复位。

## 和巴法云版的区别

这个版本完全不包含：

- `HTTPClient`
- 巴法云 UID
- 图片上传
- MQTT 状态上报
- 云端冷却时间

所以它更像原作者项目：

```text
本地网页 + 本地识别 + 本地门禁硬件
```

不是：

```text
识别 + 云端上传
```

## 和原作者版本的区别

原作者版本主要基于旧 ESP32 Arduino core 的人脸 API：

```cpp
fd_forward.h
fr_forward.h
fr_flash.h
```

本版本使用 Arduino-ESP32 2.0.7 可用的新 API：

```cpp
human_face_detect_msr01.hpp
human_face_detect_mnp01.hpp
face_recognition_112_v1_s8.hpp
```

另外本版本做了这些改动：

- 中文网页
- 中文串口提示
- 保留门禁按钮布局
- 增加预览限速
- 增加识别限速
- 复用 RGB 缓冲区
- `fr` 分区找不到时进入临时模式

## 已验证编译结果

使用：

```text
arduino-cli compile --fqbn esp32:esp32:esp32cam
```

编译通过。

大概占用：

```text
程序空间：89%
动态内存：23%
```

## 注意

ESP32-CAM 人脸识别不是专业安全门禁。

它没有活体检测，可能被照片骗过。适合学习、实验、辅助门禁，不建议单独作为正式门锁的唯一安全判断。

