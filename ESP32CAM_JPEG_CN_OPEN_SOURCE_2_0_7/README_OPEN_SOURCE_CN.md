# ESP32-CAM 人脸识别上传版 Arduino-ESP32 2.0.7

这是一个专门面向 Arduino-ESP32 core 2.0.7 的 ESP32-CAM 人脸识别项目。

它的目标不是做完整门禁硬件，而是做：

- ESP32-CAM 网页预览
- 人脸录入
- 人脸识别
- 识别成功/失败状态显示
- 检测到人脸后上传图片到巴法云
- 把识别结果发送到巴法云 MQTT 主题

## 项目来源说明

本项目的网页门禁思路参考了 Robot Zero One 的 ESP32-CAM Access Control 项目：

- GitHub: https://github.com/robotzero1/esp32cam-access-control
- Blog: https://robotzero.one/access-control-with-face-recognition/

Robot Zero One 原项目主要用于：

```text
ESP32-CAM 识别人脸后控制继电器开门
```

本项目在此基础上做了面向 Arduino-ESP32 core 2.0.7 的适配和重写：

```text
ESP32-CAM 识别人脸后上传图片和状态到巴法云
```

另外，原项目本身也提到它组合参考了：

- ESP32-CAM Face Recognition for Home Automation
- ESP-WHO Face Recognition with WebSocket Communication

## 许可证注意

我没有在 Robot Zero One 的 `esp32cam-access-control` 仓库里看到明确的 `LICENSE` 文件。

所以如果你把这个项目开源，建议这样处理：

1. 在 README 里保留 Robot Zero One 的来源链接。
2. 说明你的版本是 Arduino-ESP32 2.0.7 适配版。
3. 不要把原作者项目说成完全由自己原创。
4. 如果要加 MIT、GPL、Apache 等许可证，建议只声明你自己新增/重写的部分。
5. 如果以后要商用或大范围传播，最好联系原作者确认授权。

可以在你的开源仓库里这样写：

```text
This project is an Arduino-ESP32 core 2.0.7 adaptation based on the ESP32-CAM access-control idea from Robot Zero One:
https://github.com/robotzero1/esp32cam-access-control
https://robotzero.one/access-control-with-face-recognition/

Major changes include Arduino-ESP32 2.0.7 face-recognition API migration, Chinese web UI, hardware-door-control removal, Bemfa image upload, and MQTT status reporting.
```

中文可以写：

```text
本项目为 Arduino-ESP32 core 2.0.7 适配版，项目思路参考 Robot Zero One 的 ESP32-CAM Access Control。
本版本重写/改造了人脸识别 API、中文网页、巴法云图片上传和 MQTT 状态上报，并删除了继电器、蜂鸣器、红绿灯等外接门禁硬件逻辑。
```

## 当前文件

这个开源目录里建议发布这些文件：

| 文件 | 作用 |
| --- | --- |
| `ESP32CAM_JPEG_CN_OPEN_SOURCE_2_0_7.ino` | 主程序，已去掉个人 WiFi 和巴法云私钥 |
| `camera_pins.h` | ESP32-CAM 摄像头引脚定义 |
| `camera_index.h` | 压缩后的中文网页 |
| `HTML_interface.html` | 原始中文网页源码 |
| `partitions.csv` | 带 `fr` 人脸分区的分区表 |
| `README_OPEN_SOURCE_CN.md` | 当前说明文档 |

不要发布带有真实 WiFi 密码、巴法云私钥的私人测试版本。

## 适用环境

推荐环境：

| 项目 | 推荐 |
| --- | --- |
| 开发板 | AI Thinker ESP32-CAM |
| 摄像头 | OV2640，支持 JPEG |
| Arduino-ESP32 core | 2.0.7 |
| 分区表 | 使用本目录的 `partitions.csv` |
| PSRAM | 建议有 |
| 网页浏览器 | Chrome / Edge |

## 需要改的配置

打开 `ESP32CAM_JPEG_CN_OPEN_SOURCE_2_0_7.ino`，先修改这些占位符：

```cpp
const char *ssid = "YOUR_WIFI_SSID";
const char *password = "YOUR_WIFI_PASSWORD";
const char *BEMFA_UID = "YOUR_BEMFA_PRIVATE_KEY";
```

巴法云主题默认是：

```cpp
const char *BEMFA_TOPIC = "stu";
const char *BEMFA_STATUS_TOPIC = "status";
```

含义：

- `stu`：图片上传主题
- `status`：文字状态主题

如果你的巴法云主题名字不同，就改这里。

## 和原版最大的区别

原版主要是门禁控制：

```text
识别主人 -> 继电器开门 -> 蜂鸣器/灯提示
```

本版本主要是云端上传：

```text
识别主人 -> 上传图片 -> status 发送“主人识别成功”
识别失败 -> 上传图片 -> status 发送“不是主人在人脸识别”
```

已经删除：

- 继电器
- 蜂鸣器
- 红灯
- 绿灯
- 网页开门音效
- `door_open` 消息
- 手动“检测人脸”按钮
- 手动“人脸识别”按钮

## Arduino-ESP32 2.0.7 适配点

原版旧 API：

```cpp
#include "fd_forward.h"
#include "fr_forward.h"
#include "fr_flash.h"
```

本版本新 API：

```cpp
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_112_v1_s8.hpp"
```

核心变化：

```text
旧版 face_detect / align_face / get_face_id
改为
HumanFaceDetectMSR01 + HumanFaceDetectMNP01 + FaceRecognition112V1S8
```

## 主要功能

1. 开机连接 WiFi。
2. 启动 ESP32-CAM。
3. 启动本地中文网页。
4. 浏览器打开 ESP32 的 IP 地址后显示摄像头画面。
5. 输入姓名后可以录入人脸。
6. 默认自动进入人脸识别。
7. 识别到主人后网页显示成功。
8. 识别到主人后上传图片到巴法云。
9. 识别到主人后向 `status` 发送 `主人识别成功`。
10. 识别到非主人后上传图片到巴法云。
11. 识别到非主人后向 `status` 发送 `不是主人在人脸识别`。
12. 30 秒内最多主动上传一次，避免刷爆云端。

## 使用流程

1. 安装 Arduino-ESP32 core 2.0.7。
2. 打开本目录里的 `.ino` 文件。
3. 修改 WiFi 和巴法云配置。
4. 选择 AI Thinker ESP32-CAM。
5. 上传程序。
6. 打开串口监视器，波特率 `115200`。
7. 看到类似下面的信息：

```text
WiFi 已连接
网页服务已启动
摄像头已就绪，请打开 http://xxx.xxx.xxx.xxx/
```

8. 浏览器打开这个 IP。
9. 输入姓名，点击“录入人脸”。
10. 录入完成后，网页会自动识别。

## 常见问题

### 1. 为什么打开网页才开始识别？

当前版本的人脸处理逻辑放在 WebSocket 连接循环里。

也就是说：

```text
打开网页 -> WebSocket 连接 -> ESP32 开始持续抓图、预览、识别、上传
```

如果想不开网页也后台识别，需要把识别逻辑从网页连接里拆出来，这是另一个版本的架构。

### 2. 为什么识别时会卡？

ESP32-CAM 算力很弱，人脸识别模型比较吃 CPU。

本版本已经做了限速：

```cpp
RECOGNIZE_INTERVAL_MS = 3000;
PREVIEW_INTERVAL_MS = 100;
CLOUD_REPORT_COOLDOWN_MS = 30000;
```

意思是：

- 识别不是每帧都跑。
- 预览不是每帧都发。
- 云端不是每次检测到脸都传。

这样会比原始每帧识别更稳。

### 3. `HTTP -11` 是什么？

巴法云图片上传出现 `HTTP -11`，常见意思是 HTTPClient 等响应超时。

文字状态接口比较小，所以有可能出现：

```text
图片上传 -11
状态发送 200
```

这说明文字状态成功了，图片接口可能是超时或响应慢。

### 4. `fr` 分区找不到怎么办？

需要确认本目录里的 `partitions.csv` 跟 `.ino` 在同一个草图目录里。

本项目需要 `fr` 分区保存人脸样本：

```csv
fr,       data, ,        0x310000, 0xF0000,
```

如果没有 `fr` 分区，代码会进入临时模式：

```text
可以录入和识别，但重启后会丢失
```

## 安全提醒

这个项目只是学习和实验用。

ESP32-CAM 的人脸识别不是专业门禁系统：

- 可能被照片骗过。
- 没有活体检测。
- 光线、距离、角度都会影响识别。
- 不建议单独用于真正的门锁安全。

更适合做：

- 学习项目
- 实验项目
- 提醒/通知系统
- 简单家庭自动化辅助

