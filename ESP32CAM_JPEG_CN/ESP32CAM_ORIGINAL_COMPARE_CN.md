# ESP32CAM_JPEG_CN 与 ESP32CAM 原版对比报告

本文对比的是这两个目录：

- 原版：`上游原项目（见仓库根目录 THIRD_PARTY_NOTICES.md）`
- 现在版：`ESP32CAM_JPEG_CN`

一句话结论：现在版不是简单改了几个按钮，而是把原来的“ESP32-CAM 人脸门禁示例”改成了“Arduino-ESP32 2.0.7 可用的人脸识别 + 中文网页 + 巴法云图片上传 + 状态上报版本”。

## 修改了百分之多少

百分比要先说明怎么算，因为代码里我给你加了大量中文注释，注释也会让行数变多。

| 对比项 | 原版 | 现在版 | 变化 |
| --- | ---: | ---: | ---: |
| 主程序总行数 | 409 行 | 1060 行 | 增加 651 行，约 +159.2% |
| 主程序近似代码行 | 348 行 | 616 行 | 增加 268 行，约 +77.0% |
| 主程序纯注释行 | 11 行 | 252 行 | 增加 241 行 |
| 网页总行数 | 242 行 | 211 行 | 减少 31 行，约 -12.8% |

Git 行级差异统计：

| 文件 | 新增 | 删除 | 说明 |
| --- | ---: | ---: | --- |
| `ESP32CAM.ino` -> `ESP32CAM_JPEG_CN.ino` | 951 行 | 300 行 | 主程序核心大量重写 |
| `HTML_interface.html` | 12 行 | 43 行 | 中文化并删除两个按钮和门禁音效逻辑 |

如果按“代码内容”看，主程序大约改了 70% 到 80%。

如果把中文教学注释也算进去，主程序新增量超过原版 150%，可以理解成基本重写了一版。

如果按“功能结构”看，核心人脸识别 API、图像处理流程、网页状态、云端上报、硬件控制逻辑都变了，已经不是原版的小修小补。

## 文件级变化

| 文件 | 原版 | 现在版 | 变化 |
| --- | --- | --- | --- |
| `ESP32CAM.ino` | 有 | 改名为 `ESP32CAM_JPEG_CN.ino` | 主程序重写并全中文注释 |
| `HTML_interface.html` | 有 | 有 | 中文化，删掉“检测/识别”两个按钮 |
| `camera_index.h` | 有 | 有 | 根据新网页重新生成 |
| `camera_pins.h` | 有 | 有 | 基本未改 |
| `partitions.csv` | 有 | 有 | `fr` 人脸分区写法改成 2.0.7 更稳的形式 |
| `rzo_partitions.csv` | 有 | 无 | 当前版本不用它 |
| `CODE_WALKTHROUGH_CN.md` | 无 | 有 | 新增中文代码讲解文档 |
| `ESP32CAM_ORIGINAL_COMPARE_CN.md` | 无 | 有 | 本对比文档 |

## 1. ESP32 核心版本适配

原版使用的是旧 ESP32 人脸识别 API，更像 Arduino-ESP32 1.0.x 时代的写法：

```cpp
#include "fd_forward.h"
#include "fr_forward.h"
#include "fr_flash.h"
```

现在版改成 Arduino-ESP32 2.0.7 能用的人脸检测和识别头文件：

```cpp
#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_112_v1_s8.hpp"
```

这不是单纯换头文件。旧版和新版的人脸识别对象、数据结构、函数调用方式都不一样。

原版核心流程是：

```text
fmt2rgb888
-> face_detect
-> align_face
-> get_face_id
-> recognize_face_with_name / enroll_face_id_to_flash_with_name
```

现在版核心流程是：

```text
fmt2rgb888
-> HumanFaceDetectMSR01 第一阶段检测
-> HumanFaceDetectMNP01 第二阶段检测
-> FaceRecognition112V1S8 录入/识别/删除/保存
```

## 2. 人脸检测流程变化

原版使用旧的 `mtmn_config_t` 配置人脸检测：

```cpp
mtmn_config_t mtmn_config = app_mtmn_config();
out_res.net_boxes = face_detect(image_matrix, &mtmn_config);
```

现在版删除了 `app_mtmn_config()`，改成两个检测器对象：

```cpp
static HumanFaceDetectMSR01 detector_stage1(0.1F, 0.5F, 10, 0.2F);
static HumanFaceDetectMNP01 detector_stage2(0.5F, 0.3F, 5);
```

现在的人脸检测被封装进了：

```cpp
static bool detect_faces(camera_fb_t *frame, std::list<dl::detect::result_t> **results)
```

这样写的好处是：

- 主流程更清楚。
- 人脸检测失败时能统一处理。
- 以后要调检测参数，只需要改检测器对象。
- 适合 Arduino-ESP32 2.0.7 的新版 API。

## 3. 人脸识别流程变化

原版使用：

```cpp
face_id_name_list st_face_list;
face_id_node *f = recognize_face_with_name(&st_face_list, out_res.face_id);
```

现在版使用：

```cpp
static FaceRecognition112V1S8 recognizer;
face_info_t face = recognizer.recognize(tensor, landmarks);
```

原版保存人脸：

```cpp
enroll_face_id_to_flash_with_name(face_list, new_id, st_name.enroll_name);
```

现在版保存人脸：

```cpp
recognizer.enroll_id(tensor, landmarks, st_name.enroll_name, face_flash_ready);
```

这里最大的区别是：现在版把人脸库交给 `FaceRecognition112V1S8 recognizer` 管理。

它负责：

- 录入人脸。
- 识别人脸。
- 删除单个人脸。
- 删除全部人脸。
- 从 Flash 读取人脸。
- 把人脸写回 Flash。

## 4. 人脸库容量变化

原版：

```cpp
#define ENROLL_CONFIRM_TIMES 5
#define FACE_ID_SAVE_NUMBER 7
```

原版 `FACE_ID_SAVE_NUMBER` 表示最多 7 个人，每人录 5 次。

现在版：

```cpp
#define ENROLL_CONFIRM_TIMES 5
#define PERSON_SAVE_NUMBER 7
#define FACE_ID_SAVE_NUMBER (PERSON_SAVE_NUMBER * ENROLL_CONFIRM_TIMES)
```

现在写得更直白：

- `PERSON_SAVE_NUMBER = 7`：最多 7 个人。
- `ENROLL_CONFIRM_TIMES = 5`：每个人录 5 次。
- `FACE_ID_SAVE_NUMBER = 35`：总共最多 35 个样本。

这对小白更容易看懂。

## 5. Flash 人脸分区变化

原版 `partitions.csv`：

```csv
fr,       32, 32,        0x310000, 0xF0000,
```

现在版 `partitions.csv`：

```csv
fr,       data, ,        0x310000, 0xF0000,
```

原因：Arduino-ESP32 2.0.7 下，`recognizer.set_partition(...)` 找 `fr` 分区时，用 `data` 类型更稳。

现在版还增加了：

```cpp
static bool face_flash_ready = false;
```

作用是判断 `fr` 分区有没有找到。

如果找到了：

```text
录入的人脸会保存到 Flash，重启后还在。
```

如果没找到：

```text
可以临时录入和识别，但重启后会丢。
```

这样比原版更不容易一出错就直接卡死。

## 6. 图像处理和内存管理变化

原版在 `loop()` 里申请固定图片矩阵：

```cpp
dl_matrix3du_t *image_matrix = dl_matrix3du_alloc(1, 320, 240, 3);
```

现在版改成复用 RGB 缓冲区：

```cpp
static uint8_t *rgb_buf = NULL;
static size_t rgb_buf_len = 0;
```

新增函数：

```cpp
static bool ensure_rgb_buffer(size_t required_len)
static bool prepare_rgb_frame(camera_fb_t *frame, dl::Tensor<uint8_t> &tensor)
```

这样做的原因：

- 人脸模型要吃 RGB888，不是直接吃 JPEG。
- ESP32-CAM 内存小，不能每次乱申请内存。
- 有 PSRAM 就优先放 PSRAM。
- 缓冲区够大就复用，减少内存碎片。

## 7. 视频预览发送变化

原版每轮循环都直接发当前摄像头帧：

```cpp
client.sendBinary((const char *)fb->buf, fb->len);
```

现在版增加了预览间隔：

```cpp
const unsigned long PREVIEW_INTERVAL_MS = 100;
```

并封装为：

```cpp
static bool send_preview_frame(camera_fb_t *frame, WebsocketsClient &client)
```

现在版不是每一帧都强行发送，而是大约每 100ms 发一帧。

好处：

- 网页预览更稳。
- 人脸识别时不至于把 WebSocket 塞太满。
- 给 ESP32-CAM 留一点算人脸的时间。

## 8. 人脸处理频率变化

原版基本是每帧都尝试处理人脸。

现在版增加了三个间隔：

```cpp
const unsigned long DETECT_INTERVAL_MS = 1500;
const unsigned long ENROLL_INTERVAL_MS = 1200;
const unsigned long RECOGNIZE_INTERVAL_MS = 3000;
```

意思是：

- 检测模式：约 1.5 秒处理一次。
- 录入模式：约 1.2 秒采一个样本。
- 识别模式：约 3 秒识别一次。

原因：ESP32-CAM 算力很弱，人脸模型又很吃 CPU。如果每帧都识别，画面就会卡，甚至 WebSocket 掉线。

## 9. 默认状态变化

原版连接网页后默认：

```text
STREAMING
```

也就是只预览，必须点按钮才检测或识别。

现在版默认：

```cpp
en_fsm_state g_state = START_RECOGNITION;
```

网页连接后发送：

```text
自动识别中
```

也就是打开网页后默认自动识别，不需要再点“检测人脸”或“人脸识别”。

## 10. 删除外接硬件逻辑

原版有这些硬件：

```cpp
#define green_pin 12
#define red_pin 13
#define relay_pin 15
#define buzzer_pin 14
```

原版功能包括：

- 识别成功，继电器开门。
- 绿灯亮。
- 红灯亮。
- 蜂鸣器响。
- 5 秒后继电器复位。
- 网页收到 `door_open` 后播放提示音。

现在版已经删掉这些逻辑：

- 不控制继电器。
- 不控制红绿灯。
- 不控制蜂鸣器。
- 不再发送 `door_open`。
- 不再有 `open_door()`。
- 不再有开门计时器。

现在版只做识别和上传，不做门禁硬件控制。

## 11. 巴法云功能是新增的

原版没有巴法云。

现在版新增：

```cpp
#include <HTTPClient.h>
```

新增配置：

```cpp
const char *BEMFA_UID
const char *BEMFA_TOPIC
const char *BEMFA_STATUS_TOPIC
const char *BEMFA_UPLOAD_URL
const char *BEMFA_STATUS_URL
```

新增图片上传函数：

```cpp
static bool upload_face_image_to_bemfa(camera_fb_t *frame, WebsocketsClient &client)
```

它的作用：

```text
检测到人脸后，把当前摄像头画面上传到巴法云图片主题 stu。
```

新增状态上报函数：

```cpp
static bool publish_status_to_bemfa(const char *message)
```

它的作用：

```text
把“主人识别成功”或“不是主人在人脸识别”发到巴法云 status 主题。
```

新增完整云端事件函数：

```cpp
static void report_cloud_event(camera_fb_t *frame, WebsocketsClient &client, const char *message)
```

它做两件事：

```text
1. 上传图片。
2. 发送文字状态。
```

## 12. 云端上报冷却时间

现在版新增：

```cpp
const unsigned long CLOUD_REPORT_COOLDOWN_MS = 30000;
```

也就是 30 秒内最多主动上报一次。

原因：

- 避免有人一直站在镜头前，ESP32-CAM 连续疯狂上传。
- 避免巴法云图片接口被刷爆。
- 避免上传图片时卡住人脸识别。

如果你点按钮触发过：

```cpp
pending_cloud_upload = true;
```

那下一次检测到人脸会强制上传一次。

## 13. HTTP -11 的原因

现在版里图片上传用的是：

```cpp
http.setTimeout(5000);
```

如果串口出现：

```text
Bemfa upload failed, HTTP -11
```

通常表示 HTTPClient 等接口响应超时。

这不一定说明图片没发出去，有时是服务器收到了，但 ESP32 没等到完整响应。

状态接口更小，所以你之前看到：

```text
Bemfa status OK, HTTP 200
```

这说明文字状态发送成功。

## 14. 网页变化

原版网页标题：

```html
Face Recognition Access Control
```

现在版网页标题：

```html
人脸识别上传
```

原版按钮：

- `DETECT FACES`
- `ADD USER`
- `ACCESS CONTROL`
- `DELETE ALL`

现在版按钮：

- `录入人脸`
- `全部删除`

已经删除：

- 检测人脸按钮。
- 人脸识别按钮。

原因：现在默认自动识别，不需要你再点这两个按钮。

原版列表标题：

```text
Captured Faces
```

现在版：

```text
已录入人脸
```

原版删除单个人脸显示：

```text
X
```

现在版显示：

```text
删
```

## 15. 网页音效删除

原版网页里有 AudioContext：

```js
a = new AudioContext();
function alertSound(w,x,y) { ... }
```

并且收到：

```js
door_open
```

会播放音效。

现在版已经删除。

原因：你现在不需要继电器开门，也不需要门禁网页音效。

## 16. 中文化变化

原版串口和网页提示大多是英文：

- `WiFi connected`
- `Camera Ready`
- `FACE NOT RECOGNISED`
- `NO FACE DETECTED`
- `CAPTURING`
- `RECOGNISING`

现在版改成中文：

- `WiFi 已连接`
- `摄像头已就绪`
- `未识别到已录入人脸`
- `未检测到人脸`
- `正在录入人脸`
- `正在识别人脸，检测到后上传巴法云`

这样你看串口和网页时更容易理解。

## 17. 人脸列表显示变化

原版直接遍历链表：

```cpp
face_id_node *head = st_face_list.head;
```

现在版使用：

```cpp
std::vector<face_info_t> faces = recognizer.get_enrolled_ids();
```

并新增：

```cpp
static String face_label(const face_info_t &face)
static bool label_seen(const std::vector<String> &labels, const String &label)
```

作用：

```text
同一个人录了 5 个样本，网页列表只显示一个名字。
```

## 18. 删除单个人脸变化

原版删除：

```cpp
delete_face_id_in_flash_with_name(&st_face_list, person);
```

现在版删除：

```cpp
static void delete_face_by_name(const char *name)
```

现在版会循环删除同名的全部样本。

原因：一个人不是只有一个样本，而是有 5 个样本。只删一个可能删不干净。

## 19. 没有人脸时的提示变化

原版检测不到脸会比较频繁发送：

```text
NO FACE DETECTED
```

现在版新增：

```cpp
uint8_t missed_face_count = 0;
```

连续 3 次没检测到脸才提示：

```cpp
if (g_state != START_DETECT && missed_face_count >= 3)
```

这样网页状态不会疯狂跳来跳去。

## 20. WiFi 稳定性变化

现在版新增：

```cpp
WiFi.setSleep(false);
```

作用：关闭 WiFi 省电模式。

ESP32-CAM 做网页流和上传时，如果 WiFi 省电，有时会更容易卡顿或断流。

## 21. 摄像头配置变化

原版：

```cpp
config.pixel_format = PIXFORMAT_JPEG;
if (psramFound()) {
  config.frame_size = FRAMESIZE_UXGA;
  config.jpeg_quality = 10;
  config.fb_count = 2;
} else {
  config.frame_size = FRAMESIZE_SVGA;
  config.jpeg_quality = 12;
  config.fb_count = 1;
}
```

现在版：

```cpp
config.frame_size = FRAMESIZE_UXGA;
config.pixel_format = PIXFORMAT_JPEG;
config.grab_mode = CAMERA_GRAB_LATEST;
config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
config.jpeg_quality = 15;
config.fb_count = 1;
```

并且有 PSRAM 时：

```cpp
config.jpeg_quality = 15;
config.fb_count = 2;
```

最后实际运行仍然切到：

```cpp
s->set_framesize(s, FRAMESIZE_QVGA);
```

重点区别：

- 新增 `CAMERA_GRAB_LATEST`，尽量抓最新画面。
- 新增 `fb_location`，有 PSRAM 就放 PSRAM。
- JPEG 质量从更高质量改成稍低一点，换速度和稳定性。

## 22. 当前版本仍然保留的东西

这些和原版思路一致：

- 使用 ESP32-CAM。
- 使用 AI Thinker 引脚配置。
- 浏览器访问 ESP32 的 IP 看画面。
- WebSocket 端口仍然是 82。
- 人脸录入仍然是输入名字后录入。
- 每个人仍然录 5 次样本。
- 仍然可以删除单个人脸。
- 仍然可以删除全部人脸。
- 仍然使用 `camera_index.h` 存网页。

## 23. 当前版本的重要限制

现在版的人脸识别主循环仍然和网页 WebSocket 连接绑在一起：

```text
浏览器打开网页并连接后，ESP32 才持续抓图、预览、识别、上传。
```

也就是说，它更像“打开网页监控并识别上传”的版本。

如果你以后想做成“不开网页也能后台自动识别上传”，还需要再改架构，把摄像头识别逻辑从 WebSocket 客户端循环里拆出来。

## 24. 现在版新增的核心函数清单

主程序里新增或重写的关键函数：

```cpp
face_label()
label_seen()
current_face_process_interval()
send_face_list()
delete_face_by_name()
delete_all_faces()
ensure_rgb_buffer()
prepare_rgb_frame()
detect_faces()
send_preview_frame()
send_recognized()
send_not_recognized()
upload_face_image_to_bemfa()
publish_status_to_bemfa()
should_report_cloud_event()
report_cloud_event()
process_face_frame()
app_facenet_main()
app_httpserver_init()
handle_message()
setup()
loop()
```

其中最关键的是：

- `process_face_frame()`：人脸检测、录入、识别、云端上报的核心。
- `upload_face_image_to_bemfa()`：图片上传。
- `publish_status_to_bemfa()`：文字状态上报。
- `prepare_rgb_frame()`：把摄像头图片转成模型能识别的格式。
- `detect_faces()`：新版两阶段人脸检测。

## 25. 原版删除或替换掉的关键函数/结构

原版里这些旧东西已经不用了：

```cpp
fd_forward.h
fr_forward.h
fr_flash.h
mtmn_config_t
app_mtmn_config()
face_id_name_list
face_id_node
aligned_face
dl_matrix3du_alloc()
face_detect()
align_face()
get_face_id()
recognize_face_with_name()
enroll_face_id_to_flash_with_name()
read_face_id_from_flash_with_name()
delete_face_id_in_flash_with_name()
delete_face_all_in_flash_with_name()
open_door()
```

删除原因：

- 一部分是 Arduino-ESP32 2.0.7 不适合继续用的旧 API。
- 一部分是外接硬件门禁逻辑，现在不需要了。
- 一部分被新版 `FaceRecognition112V1S8` 替代。

## 26. 增加了什么功能

现在版比原版增加了这些功能：

1. 支持 Arduino-ESP32 core 2.0.7 的新版人脸识别库。
2. 中文网页。
3. 中文串口日志。
4. 默认自动识别人脸。
5. 去掉检测/识别按钮，减少操作。
6. 支持巴法云图片上传。
7. 支持巴法云 MQTT 状态上报。
8. 主人识别成功时发送 `主人识别成功`。
9. 非主人识别时发送 `不是主人在人脸识别`。
10. 没录入主人但检测到脸时发送 `检测到人脸但没有录入主人`。
11. 30 秒云端上报冷却，避免重复刷屏。
12. 图片上传失败不影响文字状态发送。
13. `fr` 分区找不到时进入临时模式，不直接崩。
14. 人脸列表去重显示。
15. 删除同名人脸时会删掉该名字的全部样本。
16. 预览帧限速，降低卡顿。
17. 识别帧限速，降低卡顿。
18. 复用 RGB 缓冲区，减少内存碎片。
19. 关闭 WiFi 睡眠，提高网页和上传稳定性。
20. 给小白阅读加了大量中文注释。

## 27. 删掉了什么功能

现在版删除了这些原版功能：

1. 继电器开门。
2. 绿灯提示。
3. 红灯提示。
4. 蜂鸣器提示。
5. 网页开门音效。
6. `door_open` WebSocket 消息。
7. 手动点击“检测人脸”按钮。
8. 手动点击“人脸识别”按钮。
9. 旧版 1.0.x 人脸识别 API。
10. 原版英文界面。

这些删除是按你的要求做的：你现在只要识别和上传，不要外接硬件门禁。

## 28. 最核心的流程对比

原版流程：

```text
开机
-> 初始化摄像头
-> 连接 WiFi
-> 打开网页
-> 用户点检测/识别
-> 每帧做人脸检测
-> 识别成功就开继电器
-> 网页显示英文状态
```

现在版流程：

```text
开机
-> 初始化摄像头
-> 连接 WiFi
-> 打开网页
-> 默认进入自动识别
-> 按间隔做人脸检测/识别
-> 识别成功：上传图片 + status 发送“主人识别成功”
-> 识别失败：上传图片 + status 发送“不是主人在人脸识别”
-> 网页显示中文状态
```

## 29. 小白记忆版

你可以这样理解这份代码：

```text
摄像头拍一张图
-> 发给网页当预览
-> 隔几秒拿一张图去做人脸检测
-> 检测到脸就做人脸识别
-> 如果是主人，就告诉网页“识别成功”
-> 同时把图片上传到巴法云 stu
-> 再把文字结果发到巴法云 status
-> 如果不是主人，也上传图片并发警报文字
```

这就是现在版的核心。

