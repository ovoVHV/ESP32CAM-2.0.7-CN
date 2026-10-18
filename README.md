# ESP32-CAM 2.0.7 中文项目集

这是 **CKSWITCH（中文创客思维池）** 整理、重写和维护的 ESP32-CAM 中文项目集，面向 Arduino-ESP32 core 2.0.7。

项目包含三个可独立使用的工程：

| 目录 | 内容 |
| --- | --- |
| `ESP32CAM_ACCESS_CONTROL_2_0_7_CN` | 中文本地人脸门禁：继电器、红绿灯、蜂鸣器和本地网页 |
| `ESP32CAM_JPEG_CN` | 中文人脸识别：巴法云图片上传、状态上报、操作手册和代码讲解 |
| `ESP32CAM_JPEG_CN_OPEN_SOURCE_2_0_7` | 已去除个人配置的分享版，适合直接复制后填写自己的 WiFi 和巴法云参数 |

## 作者与来源

当前版本的改造、适配、中文界面、文档和巴法云功能由：

**CKSWITCH（中文创客思维池）**

完成和维护。

本项目是对较早 ESP32-CAM 人脸门禁项目的长期改造，不是从零原创。核心思路和部分工程结构来自：

- **Robot Zero One / `robotzero1`**
- 原项目：[ESP32-CAM Access Control](https://github.com/robotzero1/esp32cam-access-control)
- 原教程：[Access Control with Face Recognition](https://robotzero.one/access-control-with-face-recognition/)

原教程还注明参考了 ESP32-CAM Face Recognition for Home Automation 和 ESP-WHO Face Recognition with WebSocket Communication。完整来源和权利边界见 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md)。

## 使用环境

- AI Thinker ESP32-CAM
- OV2640 摄像头
- 建议带 PSRAM
- Arduino-ESP32 core 2.0.7
- Arduino IDE 2.x

每个子目录内都有对应的 `.ino`、网页、摄像头引脚和 `partitions.csv`。请使用与主程序同名的目录打开工程，并按说明填写自己的配置。

## 隐私处理

本仓库发布副本中的 WiFi 名称、WiFi 密码和巴法云私钥都已替换为占位符：

```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char *BEMFA_UID = "YOUR_BEMFA_PRIVATE_KEY";
```

请在烧录前填写自己的值。不要把真实密钥提交到公开仓库。

## 许可证说明

CKSWITCH 对自己新增或重写的代码、中文网页、文档和适配工作按仓库根目录 [LICENSE](LICENSE) 授权。

原项目及其他第三方代码、模型和库不因本仓库发布而获得新的授权。第三方权利和来源以 [THIRD_PARTY_NOTICES.md](THIRD_PARTY_NOTICES.md) 为准。

## 安全提醒

这是学习、实验和演示项目。ESP32-CAM 人脸识别没有活体检测，可能被照片欺骗；光照、距离和角度也会影响识别结果。不要把它单独当作正式门锁的唯一安全判断。

## 项目状态

本次发布按用户要求完成了隐私清理和 GitHub 开源整理，未在发布前编译或烧录。
