/*
  改造与维护：CKSWITCH（中文创客思维池）
  基于 Robot Zero One（robotzero1）的 ESP32-CAM Access Control 项目深度改造。
  原项目：https://github.com/robotzero1/esp32cam-access-control
  原教程：https://robotzero.one/access-control-with-face-recognition/
  授权范围及第三方权利请见仓库根目录 LICENSE 和 THIRD_PARTY_NOTICES.md。
*/
/*
  ESP32-CAM 人脸识别 + 巴法云上传版

  当前版本只做这些事情：
  1. 连接 WiFi
  2. 启动摄像头
  3. 启动网页预览
  4. 录入/删除/识别人脸
  5. 检测到人脸后上传图片到巴法云图片主题 stu
  6. 把识别结果发送到巴法云 MQTT 主题 status

 

  读代码时先抓住主线：
  摄像头抓图 -> 网页预览 -> 人脸检测 -> 人脸识别 -> 图片上传 -> 状态上报
*/

// Arduino 基础库，提供 pinMode、delay、Serial 等基础功能。
#include <Arduino.h>

// WiFi 库，用来让 ESP32-CAM 连接路由器或手机热点。
#include <WiFi.h>

// HTTPClient 库，用来向巴法云发送 HTTP 请求，上传图片和状态。
#include <HTTPClient.h>

// WebSocket 库，用来让网页按钮和 ESP32-CAM 实时通信。
#include <ArduinoWebsockets.h>

// ESP32-CAM 摄像头驱动库。
#include "esp_camera.h"

// ESP32 自带网页服务器库，浏览器访问 ESP32 的 IP 靠它。
#include "esp_http_server.h"

// Flash 分区库，用来查找 fr 分区保存人脸样本。
#include "esp_partition.h"

// 图片格式转换库，例如 JPEG 转 RGB888。
#include "img_converters.h"

// 压缩后的网页文件。浏览器访问 ESP32 时，ESP32 实际发送的是这里面的网页数据。
#include "camera_index.h"

// C++ 容器库。人脸检测结果用 list，已录入人脸列表用 vector。
#include <list>
#include <vector>
#include <string>

// ESP32 Arduino core 2.0.7 的新版人脸检测/识别头文件。
// 第一阶段检测：粗略找脸。
#include "human_face_detect_msr01.hpp"

// 第二阶段检测：精确确认脸和关键点。
#include "human_face_detect_mnp01.hpp"

// 人脸识别模型：负责录入、保存、读取、比对人脸。
#include "face_recognition_112_v1_s8.hpp"

// =========================
// 1. WiFi 和巴法云配置
// =========================

// WiFi 名称。
const char *ssid = "YOUR_WIFI_SSID";

// WiFi 密码。
const char *password = "YOUR_WIFI_PASSWORD";

// 巴法云私钥。注意：这是敏感信息，不要把完整源码发给别人。
const char *BEMFA_UID = "YOUR_BEMFA_PRIVATE_KEY";

// 图片上传主题。检测到人脸时，图片会上传到这个主题。
const char *BEMFA_TOPIC = "stu";

// 状态消息主题。识别成功/失败的文字消息会发到这个主题。
const char *BEMFA_STATUS_TOPIC = "status";

// 巴法云图片二进制上传接口。
const char *BEMFA_UPLOAD_URL = "http://apis.bemfa.com/vb/api/v1/imagesUploadBin";

// 巴法云 HTTP 推送 MQTT 消息接口。
const char *BEMFA_STATUS_URL = "http://apis.bemfa.com/va/postJsonMsg";

// =========================
// 2. 人脸库容量配置
// =========================

// 每个人录入几次样本。5 表示一个人录 5 张特征。
#define ENROLL_CONFIRM_TIMES 5

// 最多保存几个人。
#define PERSON_SAVE_NUMBER 7

// 总样本数 = 人数 * 每人样本数。
// 例如 7 个人 * 每人 5 次 = 最多 35 个样本。
#define FACE_ID_SAVE_NUMBER (PERSON_SAVE_NUMBER * ENROLL_CONFIRM_TIMES)

// 录入姓名最大长度。
#define ENROLL_NAME_LEN 32

// =========================
// 3. 摄像头型号选择
// =========================

// 这里只启用 AI Thinker ESP32-CAM。
// 如果以后换板子，再改这个型号和 camera_pins.h。
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER

// 摄像头引脚定义。AI Thinker 的摄像头 GPIO 都在这个文件里。
#include "camera_pins.h"

// 使用 ArduinoWebsockets 这个命名空间，后面就能直接写 WebsocketsServer。
using namespace websockets;

// =========================
// 4. 全局对象
// =========================

// WebSocket 服务器。网页按钮和状态通信靠它。
WebsocketsServer socket_server;

// HTTP 网页服务器句柄。浏览器访问 ESP32 的 IP 时，由它提供网页。
httpd_handle_t camera_httpd = NULL;

// 摄像头当前帧。fb 可以理解成“当前这一张照片”。
static camera_fb_t *fb = NULL;

// 人脸模型不能直接吃 JPEG，所以要把 JPEG 转成 RGB888。
// rgb_buf 就是转换后的 RGB888 图片缓冲区。
static uint8_t *rgb_buf = NULL;

// rgb_buf_len 记录当前 RGB 缓冲区有多大，避免每帧重复申请内存。
static size_t rgb_buf_len = 0;

// 第一阶段人脸检测器：粗略找脸。
// 参数不用先深究，当前这组比较稳。
static HumanFaceDetectMSR01 detector_stage1(0.1F, 0.5F, 10, 0.2F);

// 第二阶段人脸检测器：精确确认脸和关键点。
static HumanFaceDetectMNP01 detector_stage2(0.5F, 0.3F, 5);

// 人脸识别器：录入、识别、删除、保存人脸都靠它。
static FaceRecognition112V1S8 recognizer;

// 是否成功找到 Flash 里的 fr 人脸分区。
// true：可以把录入的人脸保存到 Flash，重启不丢。
// false：只能临时录入，重启后会丢。
static bool face_flash_ready = false;

// =========================
// 5. 时间和状态变量
// =========================

// 最近一次检测到人脸的时间。
unsigned long last_detected_millis = 0;

// 检测模式下，每隔多久处理一次人脸。
const unsigned long DETECT_INTERVAL_MS = 1500;

// 录入模式下，每隔多久采一次样本。
const unsigned long ENROLL_INTERVAL_MS = 1200;

// 识别模式下，每隔多久识别一次。
// ESP32-CAM 算力弱，不建议太小，否则画面会卡。
const unsigned long RECOGNIZE_INTERVAL_MS = 3000;

// 网页预览帧间隔。100ms 大约是 10fps。
const unsigned long PREVIEW_INTERVAL_MS = 100;

// 云端上报冷却时间。30 秒内最多上传一次图片和状态，避免刷爆云端。
const unsigned long CLOUD_REPORT_COOLDOWN_MS = 30000;

// 如果摄像头不是 JPEG 格式，预览转换 JPEG 时使用这个质量。
// 当前摄像头输出就是 JPEG，所以多数时候不会用到。
const uint8_t PREVIEW_JPEG_QUALITY = 65;

// 录入人脸时，还剩几次样本没采。
int enroll_samples_left = 0;

// 最近一次做人脸检测/识别的时间。
unsigned long last_face_process_millis = 0;

// 最近一次发送网页预览图的时间。
unsigned long last_preview_millis = 0;

// 最近一次上传巴法云的时间。
unsigned long last_cloud_report_millis = 0;

// 连续几次没有检测到人脸。
// 用它避免网页状态一会儿“识别成功”，下一帧马上“未检测到人脸”。
uint8_t missed_face_count = 0;

// 如果你点了网页按钮，就强制下一次检测到脸时上传一次云端。
bool pending_cloud_upload = false;

// =========================
// 6. 设备当前模式
// =========================

// 这是一个枚举，表示程序当前在干什么。
typedef enum
{
  START_STREAM,       // 只预览，不做人脸识别。
  START_DETECT,       // 只检测有没有人脸。
  START_RECOGNITION,  // 识别人脸。
  START_ENROLL,       // 录入人脸。
  DELETE_ALL,         // 删除全部人脸，当前主要是保留状态名。
} en_fsm_state;

// 当前默认状态：自动识别人脸。
en_fsm_state g_state = START_RECOGNITION;

// 保存网页传来的录入姓名。
typedef struct
{
  char enroll_name[ENROLL_NAME_LEN];
} httpd_resp_value;

// 当前要录入的名字。
httpd_resp_value st_name;

// =========================
// 7. 小工具函数
// =========================

// 把识别结果转换成网页显示的名字。
// 如果 face.name 有内容，就显示名字。
// 如果没有名字，就显示“编号 id”。
static String face_label(const face_info_t &face)
{
  if (face.name.length() > 0)
  {
    return String(face.name.c_str());
  }
  return String("编号 ") + String(face.id);
}

// 判断一个名字是否已经在 labels 列表里。
// 用途：一个人录了 5 个样本，但网页列表只显示一个名字。
static bool label_seen(const std::vector<String> &labels, const String &label)
{
  for (const String &item : labels)
  {
    if (item == label)
    {
      return true;
    }
  }
  return false;
}

// 根据当前模式返回不同的人脸处理间隔。
// 识别模式慢一点，录入模式稍快一点。
static unsigned long current_face_process_interval()
{
  if (g_state == START_RECOGNITION)
  {
    return RECOGNIZE_INTERVAL_MS;
  }

  if (g_state == START_ENROLL)
  {
    return ENROLL_INTERVAL_MS;
  }

  return DETECT_INTERVAL_MS;
}

// =========================
// 8. 人脸列表管理
// =========================

// 把 ESP32 当前保存的人脸名字发给网页。
// 网页收到 delete_faces 会先清空列表。
// 网页收到 listface:xxx 会添加一个名字。
static void send_face_list(WebsocketsClient &client)
{
  client.send("delete_faces");

  std::vector<face_info_t> faces = recognizer.get_enrolled_ids();
  std::vector<String> labels;

  for (const face_info_t &face : faces)
  {
    String label = face_label(face);

    // 同一个人可能有 5 个样本，这里避免重复显示 5 次。
    if (label_seen(labels, label))
    {
      continue;
    }

    labels.push_back(label);
    client.send(String("listface:") + label);
  }
}

// 删除某个名字的所有人脸样本。
// 因为同一个名字可能有 5 个样本，所以要循环删。
static void delete_face_by_name(const char *name)
{
  while (true)
  {
    std::vector<face_info_t> matches = recognizer.get_enrolled_ids_with_name(name);
    if (matches.empty())
    {
      break;
    }

    // false 表示先只删内存里的，循环删完后再统一写 Flash。
    recognizer.delete_id(matches.front().id, false);
  }

  // 如果 fr 分区可用，把删除后的结果写回 Flash。
  if (face_flash_ready)
  {
    recognizer.write_ids_to_flash();
  }
}

// 删除全部人脸。
static void delete_all_faces(WebsocketsClient &client)
{
  // face_flash_ready 为 true 时，会同步清除 Flash 里保存的人脸。
  recognizer.clear_id(face_flash_ready);
  client.send("delete_faces");
}

// =========================
// 9. 图片缓冲和转换
// =========================

// 确保 rgb_buf 足够大。
// 人脸模型需要 RGB888，一张图大小 = 宽 * 高 * 3。
static bool ensure_rgb_buffer(size_t required_len)
{
  // 如果现有缓冲区够大，直接复用。
  if (rgb_buf && rgb_buf_len >= required_len)
  {
    return true;
  }

  // 如果不够大，先释放旧缓冲区。
  if (rgb_buf)
  {
    free(rgb_buf);
    rgb_buf = NULL;
    rgb_buf_len = 0;
  }

  // 有 PSRAM 就放 PSRAM，没有就放普通内存。
  rgb_buf = (uint8_t *)(psramFound() ? ps_malloc(required_len) : malloc(required_len));
  if (!rgb_buf)
  {
    Serial.printf("RGB 缓冲区申请失败：%u 字节\n", (unsigned)required_len);
    return false;
  }

  rgb_buf_len = required_len;
  return true;
}

// 把摄像头 JPEG 帧转换成 RGB888，并包装成模型需要的 Tensor。
static bool prepare_rgb_frame(camera_fb_t *frame, dl::Tensor<uint8_t> &tensor)
{
  if (!frame)
  {
    return false;
  }

  // RGB888 每个像素 3 个字节。
  size_t required_len = frame->width * frame->height * 3;
  if (!ensure_rgb_buffer(required_len))
  {
    return false;
  }

  // 把 JPEG/RGB565 等格式转换成 RGB888。
  if (!fmt2rgb888(frame->buf, frame->len, frame->format, rgb_buf))
  {
    Serial.println("图像转 RGB888 失败");
    return false;
  }

  // 把 rgb_buf 告诉人脸模型。
  // set_auto_free(false) 表示 tensor 不负责释放 rgb_buf，rgb_buf 由我们自己复用。
  tensor.set_element(rgb_buf)
        .set_shape({(int)frame->height, (int)frame->width, 3})
        .set_auto_free(false);

  return true;
}

// 运行两阶段人脸检测。
// results 会指向检测结果列表。
static bool detect_faces(camera_fb_t *frame, std::list<dl::detect::result_t> **results)
{
  if (!frame || !rgb_buf)
  {
    return false;
  }

  // 第一阶段：粗略找脸。
  std::list<dl::detect::result_t> &candidates =
      detector_stage1.infer(rgb_buf, {(int)frame->height, (int)frame->width, 3});

  // 第二阶段：确认人脸和关键点。
  *results = &detector_stage2.infer(rgb_buf, {(int)frame->height, (int)frame->width, 3}, candidates);

  // size > 0 表示检测到了至少一张脸。
  return (*results)->size() > 0;
}

// 把当前摄像头画面发给网页显示。
static bool send_preview_frame(camera_fb_t *frame, WebsocketsClient &client)
{
  if (!frame)
  {
    return false;
  }

  // 当前摄像头输出 JPEG，所以通常直接发给网页即可。
  if (frame->format == PIXFORMAT_JPEG)
  {
    client.sendBinary((const char *)frame->buf, frame->len);
    return true;
  }

  // 如果以后换成非 JPEG 摄像头格式，就在这里临时压成 JPEG 再发网页。
  uint8_t *jpg_buf = NULL;
  size_t jpg_len = 0;
  bool converted = fmt2jpg(frame->buf,
                           frame->len,
                           frame->width,
                           frame->height,
                           frame->format,
                           PREVIEW_JPEG_QUALITY,
                           &jpg_buf,
                           &jpg_len);

  if (!converted || !jpg_buf)
  {
    client.send("预览图压缩失败");
    return false;
  }

  client.sendBinary((const char *)jpg_buf, jpg_len);
  free(jpg_buf);
  return true;
}

// =========================
// 10. 网页状态显示
// =========================

// 识别成功时，网页和串口显示识别结果。
static void send_recognized(WebsocketsClient &client, const face_info_t &face)
{
  String label = face_label(face);
  String message = String("识别成功：") + label;

  Serial.println(message);
  client.send(message);
}

// 识别失败时，网页和串口显示失败结果。
static void send_not_recognized(WebsocketsClient &client)
{
  client.send("未识别到已录入人脸");
  Serial.println("未识别到已录入人脸");
}

// =========================
// 11. 巴法云图片上传
// =========================

// 上传当前人脸图片到巴法云图片主题 stu。
static bool upload_face_image_to_bemfa(camera_fb_t *frame, WebsocketsClient &client)
{
  if (!frame || WiFi.status() != WL_CONNECTED)
  {
    client.send("云端上传失败：WiFi未连接");
    return false;
  }

  // 默认直接上传摄像头当前帧。
  uint8_t *upload_buf = frame->buf;
  size_t upload_len = frame->len;
  bool allocated_jpg = false;

  // 如果当前帧不是 JPEG，就先转换成 JPEG 再上传。
  // 现在摄像头已经是 JPEG，这段一般不会走。
  if (frame->format != PIXFORMAT_JPEG)
  {
    if (!fmt2jpg(frame->buf, frame->len, frame->width, frame->height, frame->format, 75, &upload_buf, &upload_len))
    {
      client.send("云端上传失败：图片压缩失败");
      return false;
    }
    allocated_jpg = true;
  }

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(BEMFA_UPLOAD_URL);

  // 巴法云图片接口要求这些请求头。
  http.addHeader("Content-Type", "image/jpg");
  http.addHeader("Authorization", BEMFA_UID);
  http.addHeader("Authtopic", BEMFA_TOPIC);
  http.addHeader("Connection", "close");

  // POST 图片二进制数据。
  int code = http.POST(upload_buf, upload_len);
  String response = http.getString();
  http.end();

  // 如果前面临时压缩了 JPEG，这里要释放临时内存。
  if (allocated_jpg)
  {
    free(upload_buf);
  }

  if (code > 0 && code < 300)
  {
    Serial.printf("Bemfa upload OK, HTTP %d, %u bytes\n", code, (unsigned)upload_len);
    client.send("人脸图片已上传巴法云");
    return true;
  }

  // HTTP -11 常见原因是图片接口响应超时。
  Serial.printf("Bemfa upload failed, HTTP %d, response: %s\n", code, response.c_str());
  client.send(String("云端上传失败，HTTP ") + code);
  return false;
}

// =========================
// 12. 巴法云状态上报
// =========================

// 向巴法云 status 主题发送文字消息。
static bool publish_status_to_bemfa(const char *message)
{
  if (WiFi.status() != WL_CONNECTED)
  {
    Serial.println("Bemfa status failed: WiFi disconnected");
    return false;
  }

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(BEMFA_STATUS_URL);
  http.addHeader("Content-Type", "application/json; charset=utf-8");
  http.addHeader("Connection", "close");

  // type = 1 表示 MQTT 主题。
  // 如果 status 是 TCP 主题，type 要改成 3。
  String body = String("{\"uid\":\"") + BEMFA_UID +
                "\",\"topic\":\"" + BEMFA_STATUS_TOPIC +
                "\",\"type\":1,\"msg\":\"" + message + "\"}";

  int code = http.POST(body);
  String response = http.getString();
  http.end();

  if (code > 0 && code < 300)
  {
    Serial.printf("Bemfa status OK, HTTP %d, msg: %s\n", code, message);
    return true;
  }

  Serial.printf("Bemfa status failed, HTTP %d, response: %s\n", code, response.c_str());
  return false;
}

// 判断现在能不能上报云端。
static bool should_report_cloud_event()
{
  // 点过网页按钮后，pending_cloud_upload 会变 true，下一次检测到脸就强制上报。
  return pending_cloud_upload ||
         last_cloud_report_millis == 0 ||
         millis() - last_cloud_report_millis >= CLOUD_REPORT_COOLDOWN_MS;
}

// 一次完整的云端上报：上传图片 + 发送文字状态。
static void report_cloud_event(camera_fb_t *frame, WebsocketsClient &client, const char *message)
{
  pending_cloud_upload = false;
  last_cloud_report_millis = millis();

  // 图片上传失败不影响文字状态发送。
  upload_face_image_to_bemfa(frame, client);
  publish_status_to_bemfa(message);
}

// =========================
// 13. 人脸处理核心函数
// =========================

// 这个函数是整份代码最核心的地方。
// 它负责：检测人脸、录入人脸、识别人脸、触发云端上报。
static void process_face_frame(camera_fb_t *frame, WebsocketsClient &client)
{
  // 如果当前不是检测/录入/识别模式，就不做人脸处理。
  if (g_state != START_DETECT && g_state != START_ENROLL && g_state != START_RECOGNITION)
  {
    return;
  }

  // 把摄像头帧转成模型能用的 Tensor。
  dl::Tensor<uint8_t> tensor;
  if (!prepare_rgb_frame(frame, tensor))
  {
    client.send("图像处理失败");
    return;
  }

  // 检测有没有人脸。
  std::list<dl::detect::result_t> *results = NULL;
  if (!detect_faces(frame, &results))
  {
    missed_face_count++;

    // 连续 3 次没检测到才提示，避免状态太跳。
    if (g_state != START_DETECT && missed_face_count >= 3)
    {
      client.send("未检测到人脸");
    }
    return;
  }

  // 检测到人脸后，清空漏检计数。
  missed_face_count = 0;
  last_detected_millis = millis();

  // landmarks 是人脸关键点，例如眼睛、鼻子、嘴的位置。
  std::vector<int> landmarks = results->front().keypoint;

  // START_DETECT：只检测有没有脸，不做人脸比对。
  if (g_state == START_DETECT)
  {
    client.send("检测到人脸");

    if (should_report_cloud_event())
    {
      report_cloud_event(frame, client, "检测到人脸");
    }
    return;
  }

  // START_ENROLL：录入当前人脸。
  if (g_state == START_ENROLL)
  {
    if (strlen(st_name.enroll_name) == 0)
    {
      g_state = START_STREAM;
      client.send("请先输入姓名");
      return;
    }

    if (recognizer.get_enrolled_id_num() >= FACE_ID_SAVE_NUMBER)
    {
      g_state = START_STREAM;
      client.send("人脸库已满");
      return;
    }

    // enroll_id 会提取当前脸的特征并加入人脸库。
    // face_flash_ready 为 true 时，同时写入 Flash。
    int id = recognizer.enroll_id(tensor, landmarks, st_name.enroll_name, face_flash_ready);
    if (id < 0)
    {
      client.send("录入失败，请重新对准镜头");
      return;
    }

    enroll_samples_left--;
    int sample_number = ENROLL_CONFIRM_TIMES - enroll_samples_left;
    client.send(String("正在录入 ") + st_name.enroll_name + "：第 " + sample_number + "/" + ENROLL_CONFIRM_TIMES + " 次");

    if (enroll_samples_left <= 0)
    {
      g_state = START_RECOGNITION;
      client.send(String(st_name.enroll_name) + " 录入完成");
      send_face_list(client);
    }
    return;
  }

  // 如果还没有录入任何人脸，无法判断是不是主人。
  if (recognizer.get_enrolled_id_num() == 0)
  {
    if (g_state == START_RECOGNITION)
    {
      client.send("请先录入人脸");

      if (should_report_cloud_event())
      {
        report_cloud_event(frame, client, "检测到人脸但没有录入主人");
      }
    }
    else
    {
      client.send("检测到人脸");
    }
    return;
  }

  // START_RECOGNITION：真正进行人脸识别。
  face_info_t face = recognizer.recognize(tensor, landmarks);
  if (face.id >= 0)
  {
    // face.id >= 0 表示匹配到已录入的人脸。
    if (should_report_cloud_event())
    {
      report_cloud_event(frame, client, "主人识别成功");
    }
    send_recognized(client, face);
  }
  else if (g_state == START_RECOGNITION)
  {
    // 没匹配到主人，发送警报状态。
    if (should_report_cloud_event())
    {
      report_cloud_event(frame, client, "不是主人在人脸识别");
      send_not_recognized(client);
    }
    else
    {
      client.send("未识别到已录入人脸");
    }
  }
  else
  {
    client.send("检测到人脸");
  }
}

// =========================
// 14. 人脸识别初始化
// =========================

void app_facenet_main()
{
  face_flash_ready = false;

  // 识别阈值。
  // 数值越大越严格，误认少但可能认不出主人。
  // 数值越小越宽松，更容易认出主人但误认风险增加。
  recognizer.set_thresh(0.55F);

  // 设置保存人脸样本的 Flash 分区，分区名必须叫 fr。
  if (!recognizer.set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr"))
  {
    Serial.println("未找到人脸分区 fr，进入临时模式：可以录入和识别，但重启后会丢失");
    return;
  }

  // 从 Flash 读取已保存的人脸样本。
  int loaded = recognizer.set_ids_from_flash();
  face_flash_ready = true;
  Serial.printf("Flash 模式：已载入 %d 个人脸样本\n", loaded);
}

// =========================
// 15. 网页服务器
// =========================

// 浏览器访问根路径 / 时，返回压缩后的网页。
static esp_err_t index_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}

// 定义网页路由：访问 / 时调用 index_handler。
httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL};

// 启动 ESP32 的 HTTP 网页服务器。
void app_httpserver_init()
{
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  if (httpd_start(&camera_httpd, &config) == ESP_OK)
  {
    Serial.println("网页服务已启动");
    httpd_register_uri_handler(camera_httpd, &index_uri);
  }
}

// =========================
// 16. 处理网页按钮消息
// =========================

// 网页点按钮后，会通过 WebSocket 发消息到这里。
void handle_message(WebsocketsClient &client, WebsocketsMessage msg)
{
  String data = msg.data();

  if (data == "stream")
  {
    // 只预览画面，不做人脸识别。
    g_state = START_STREAM;
    last_face_process_millis = 0;
    client.send("视频预览中");
  }
  else if (data == "detect")
  {
    // 当前版本里，检测按钮也进入识别模式。
    // pending_cloud_upload = true 表示下一次检测到脸时强制上报一次。
    g_state = START_RECOGNITION;
    last_face_process_millis = 0;
    pending_cloud_upload = true;
    client.send("正在识别人脸，检测到后上传巴法云");
  }
  else if (data.startsWith("capture:"))
  {
    // capture:名字 表示录入这个名字的人脸。
    String person = data.substring(8);
    person.trim();
    if (person.length() == 0)
    {
      client.send("请先输入姓名");
      return;
    }

    person.toCharArray(st_name.enroll_name, sizeof(st_name.enroll_name));
    enroll_samples_left = ENROLL_CONFIRM_TIMES;
    g_state = START_ENROLL;
    last_face_process_millis = 0;

    if (face_flash_ready)
    {
      client.send("正在录入人脸");
    }
    else
    {
      client.send("临时录入：重启后会丢失");
    }
  }
  else if (data == "recognise")
  {
    // 进入识别模式，并强制下一次检测到人脸时上报一次云端。
    g_state = START_RECOGNITION;
    last_face_process_millis = 0;
    pending_cloud_upload = true;
    client.send("正在识别人脸，检测到后上传巴法云");
  }
  else if (data.startsWith("remove:"))
  {
    // remove:名字 表示删除这个人的全部样本。
    String person = data.substring(7);
    person.trim();
    delete_face_by_name(person.c_str());
    send_face_list(client);
  }
  else if (data == "delete_all")
  {
    // 删除全部人脸。
    delete_all_faces(client);
  }
}

// =========================
// 17. setup：开机只执行一次
// =========================

void setup()
{
  // 打开串口，方便看日志。
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  // 摄像头配置结构体。
  camera_config_t config;

  // 摄像头使用 LEDC 产生 XCLK 时钟。
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;

  // 摄像头数据引脚。
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;

  // 摄像头时钟和同步引脚。
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;

  // 摄像头 SCCB/I2C 控制引脚。
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;

  // 摄像头电源和复位引脚。
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;

  // 摄像头外部时钟。支持 JPEG 的 OV2640 通常用 20MHz。
  config.xclk_freq_hz = 20000000;

  // 初始化时先用 UXGA 申请较大缓冲，后面再切到 QVGA 实际运行。
  config.frame_size = FRAMESIZE_UXGA;

  // 摄像头直接输出 JPEG，网页预览最快。
  config.pixel_format = PIXFORMAT_JPEG;

  // 抓取最新帧，避免积压旧画面。
  config.grab_mode = CAMERA_GRAB_LATEST;

  // 有 PSRAM 就把帧缓冲放 PSRAM，没有就放 DRAM。
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;

  // JPEG 数值越小质量越高、文件越大；数值越大质量越低、文件越小。
  config.jpeg_quality = 15;

  // 默认一个帧缓冲。
  config.fb_count = 1;

  // 如果有 PSRAM，可以用两个帧缓冲，画面更顺。
  if (psramFound())
  {
    config.jpeg_quality = 15;
    config.fb_count = 2;
  }

  // 没有 PSRAM 时，人脸处理会更吃紧。
  if (!psramFound())
  {
    Serial.println("未检测到 PSRAM，人脸处理能力会受限");
    config.frame_size = FRAMESIZE_SVGA;
  }

  // 初始化摄像头。
  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("摄像头初始化失败，错误码 0x%x\n", err);
    return;
  }

  // 获取摄像头传感器对象，用它调整分辨率、亮度等。
  sensor_t *s = esp_camera_sensor_get();

  // OV3660 传感器需要做一些画面修正。OV2640 一般不会进这里。
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  // 实际运行使用 QVGA，也就是 320x240，速度和识别稳定性比较平衡。
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  // 连接 WiFi。
  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi 已连接");

  // 启动网页服务器。
  app_httpserver_init();

  // 初始化人脸识别和 Flash 人脸库。
  app_facenet_main();

  // 启动 WebSocket 服务，网页按钮和图片流使用 82 端口。
  socket_server.listen(82);

  Serial.print("摄像头已就绪，请打开 http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");
}

// =========================
// 18. loop：一直重复执行
// =========================

void loop()
{
  // 等待浏览器网页通过 WebSocket 连接进来。
  WebsocketsClient client = socket_server.accept();

  // 设置网页消息处理函数。
  client.onMessage(handle_message);

  // 网页刚连接时，发送已录入人脸列表。
  send_face_list(client);
  client.send("自动识别中");

  // 只要网页还连接着，就一直循环抓图、预览、识别。
  while (client.available())
  {
    // 处理网页按钮消息。
    client.poll();

    // 抓取一帧摄像头图片。
    fb = esp_camera_fb_get();
    if (!fb)
    {
      client.send("摄像头采集失败");
      continue;
    }

    // 判断当前是否需要做人脸处理。
    bool face_mode = (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION);

    // 到了处理间隔才做人脸检测/识别，避免每帧都跑模型导致卡死。
    if (face_mode && (last_face_process_millis == 0 || millis() - last_face_process_millis >= current_face_process_interval()))
    {
      last_face_process_millis = millis();
      process_face_frame(fb, client);
    }

    // 检测模式下，如果一段时间没检测到人脸，就持续提示正在检测。
    if (g_state == START_DETECT && millis() - last_detected_millis > 500)
    {
      client.send("正在检测人脸");
    }

    // 到了预览间隔才发一帧给网页。
    if (last_preview_millis == 0 || millis() - last_preview_millis >= PREVIEW_INTERVAL_MS)
    {
      last_preview_millis = millis();
      send_preview_frame(fb, client);
    }

    // 非常重要：摄像头帧用完必须还回去，否则会内存/缓冲耗尽。
    esp_camera_fb_return(fb);
    fb = NULL;
  }
}
