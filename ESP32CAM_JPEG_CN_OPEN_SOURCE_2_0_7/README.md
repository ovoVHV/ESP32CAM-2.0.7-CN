# ESP32-CAM 人脸识别上传版 2.0.7

这是一个面向 Arduino-ESP32 core 2.0.7 的 ESP32-CAM 人脸识别项目。

功能：

- 中文网页预览
- 人脸录入
- 人脸识别
- 识别结果显示
- 检测到人脸后上传图片到巴法云
- 向巴法云 MQTT 主题发送识别状态

## 硬件

- AI Thinker ESP32-CAM
- OV2640 摄像头
- 建议带 PSRAM

本版本不需要：

- 继电器
- 蜂鸣器
- 红灯
- 绿灯

## 软件环境

推荐：

- Arduino IDE 2.x
- Arduino-ESP32 core 2.0.7
- 开发板选择：AI Thinker ESP32-CAM

## 使用前修改配置

打开：

```text
ESP32CAM_JPEG_CN_OPEN_SOURCE_2_0_7.ino
```

修改这些占位符：

```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char *BEMFA_UID = "YOUR_BEMFA_PRIVATE_KEY";
```

默认巴法云主题：

```cpp
const char *BEMFA_TOPIC = "stu";
const char *BEMFA_STATUS_TOPIC = "status";
```

## 文件说明

| 文件 | 作用 |
| --- | --- |
| `ESP32CAM_JPEG_CN_OPEN_SOURCE_2_0_7.ino` | 主程序 |
| `camera_pins.h` | 摄像头引脚 |
| `camera_index.h` | 压缩后的网页 |
| `HTML_interface.html` | 网页源码 |
| `partitions.csv` | 带 `fr` 人脸分区的分区表 |
| `README_OPEN_SOURCE_CN.md` | 更详细的中文开源说明 |

## 运行流程

1. 填写 WiFi 和巴法云配置。
2. 上传程序到 ESP32-CAM。
3. 打开串口监视器，波特率 `115200`。
4. 根据串口提示打开 ESP32-CAM 的 IP 地址。
5. 输入姓名并点击“录入人脸”。
6. 录入完成后，网页会自动识别人脸。
7. 识别到主人时，上传图片并发送 `主人识别成功`。
8. 识别到非主人时，上传图片并发送 `不是主人在人脸识别`。

## 项目来源

本项目的网页门禁思路参考了 Robot Zero One 的 ESP32-CAM Access Control：

- https://github.com/robotzero1/esp32cam-access-control
- https://robotzero.one/access-control-with-face-recognition/

本版本主要改动：

- 适配 Arduino-ESP32 core 2.0.7。
- 使用新版人脸检测/识别 API。
- 删除继电器、蜂鸣器、红绿灯等门禁硬件逻辑。
- 改成中文网页。
- 增加巴法云图片上传。
- 增加巴法云 MQTT 状态上报。

## 注意

Robot Zero One 原仓库没有看到明确 `LICENSE` 文件。  
如果你继续发布或二次开发，请保留来源说明。

本项目仅适合学习和实验，不建议直接作为正式门锁安全系统。ESP32-CAM 人脸识别没有活体检测，可能被照片骗过。

