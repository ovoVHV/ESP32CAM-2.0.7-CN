/*
  改造与维护：CKSWITCH（中文创客思维池）
  基于 Robot Zero One（robotzero1）的 ESP32-CAM Access Control 项目深度改造。
  原项目：https://github.com/robotzero1/esp32cam-access-control
  原教程：https://robotzero.one/access-control-with-face-recognition/
  授权范围及第三方权利请见仓库根目录 LICENSE 和 THIRD_PARTY_NOTICES.md。
*/
/*
  ESP32-CAM 门禁识别版 Arduino-ESP32 core 2.0.7

  这个版本不使用巴法云，只做本地网页门禁：
  1. 网页实时预览摄像头画面
  2. 检测人脸
  3. 录入人脸
  4. 门禁识别
  5. 识别成功后触发继电器开门
  6. 红灯、绿灯、蜂鸣器提示

  适合 AI Thinker ESP32-CAM + OV2640 JPEG 摄像头。
  人脸识别 API 使用 Arduino-ESP32 2.0.7 可用的新接口。
*/

#include <Arduino.h>
#include <WiFi.h>
#include <ArduinoWebsockets.h>
#include "esp_camera.h"
#include "esp_http_server.h"
#include "esp_partition.h"
#include "img_converters.h"
#include "camera_index.h"

#include <list>
#include <vector>
#include <string>

#include "human_face_detect_msr01.hpp"
#include "human_face_detect_mnp01.hpp"
#include "face_recognition_112_v1_s8.hpp"

// =========================
// 1. WiFi 配置
// =========================

// 改成你的 WiFi 名称。
const char *ssid = "YOUR_WIFI_SSID";

// 改成你的 WiFi 密码。
const char *password = "YOUR_WIFI_PASSWORD";

// =========================
// 2. 人脸库配置
// =========================

// 每个人录入 5 次样本，越多越稳，但也越占空间。
#define ENROLL_CONFIRM_TIMES 5

// 最多保存 7 个人。
#define PERSON_SAVE_NUMBER 7

// 总样本数 = 人数 * 每人样本数。
#define FACE_ID_SAVE_NUMBER (PERSON_SAVE_NUMBER * ENROLL_CONFIRM_TIMES)

// 姓名最大长度。
#define ENROLL_NAME_LEN 32

// =========================
// 3. 摄像头型号
// =========================

// 当前使用 AI Thinker ESP32-CAM。
//#define CAMERA_MODEL_WROVER_KIT
//#define CAMERA_MODEL_ESP_EYE
//#define CAMERA_MODEL_M5STACK_PSRAM
//#define CAMERA_MODEL_M5STACK_WIDE
#define CAMERA_MODEL_AI_THINKER
#include "camera_pins.h"

using namespace websockets;

// =========================
// 4. 门禁硬件引脚
// =========================

// 这些引脚沿用你之前能工作的接线。
// 如果你不接某个硬件，可以先不管它，但不要让对应 GPIO 短路。
#define green_pin 12
#define red_pin 13
#define relay_pin 15
#define buzzer_pin 14

// 继电器默认 HIGH，开门时拉 LOW。
// 如果你的继电器模块相反，把这两个值互换。
const uint8_t RELAY_IDLE_LEVEL = HIGH;
const uint8_t RELAY_OPEN_LEVEL = LOW;

// 蜂鸣器默认 HIGH，响的时候拉 LOW。
const uint8_t BUZZER_IDLE_LEVEL = HIGH;
const uint8_t BUZZER_ON_LEVEL = LOW;

// 门打开保持 5 秒。
const unsigned long DOOR_OPEN_MS = 5000;

// =========================
// 5. 全局对象和缓冲区
// =========================

WebsocketsServer socket_server;
httpd_handle_t camera_httpd = NULL;

static camera_fb_t *fb = NULL;
static uint8_t *rgb_buf = NULL;
static size_t rgb_buf_len = 0;

// 两阶段人脸检测器。
static HumanFaceDetectMSR01 detector_stage1(0.1F, 0.5F, 10, 0.2F);
static HumanFaceDetectMNP01 detector_stage2(0.5F, 0.3F, 5);

// 人脸识别器。
static FaceRecognition112V1S8 recognizer;

// 是否成功找到 fr 分区。
static bool face_flash_ready = false;

// =========================
// 6. 时间控制
// =========================

unsigned long door_opened_millis = 0;
unsigned long last_detected_millis = 0;
unsigned long last_face_process_millis = 0;
unsigned long last_preview_millis = 0;

// 检测/录入/识别的间隔。
// ESP32-CAM 算力有限，不建议每帧都跑人脸识别。
const unsigned long DETECT_INTERVAL_MS = 1000;
const unsigned long ENROLL_INTERVAL_MS = 1100;
const unsigned long RECOGNIZE_INTERVAL_MS = 1500;

// 网页预览大约 10fps。
const unsigned long PREVIEW_INTERVAL_MS = 100;

// 非 JPEG 画面转预览 JPEG 时使用。当前 OV2640 通常直接输出 JPEG。
const uint8_t PREVIEW_JPEG_QUALITY = 65;

// 录入时还剩多少次样本。
int enroll_samples_left = 0;

// 连续没检测到人脸的次数，用来减少网页状态乱跳。
uint8_t missed_face_count = 0;

// =========================
// 7. 状态机
// =========================

typedef enum
{
  START_STREAM,       // 只预览
  START_DETECT,       // 只检测有没有人脸
  START_RECOGNITION,  // 门禁识别
  START_ENROLL,       // 录入人脸
  DELETE_ALL,
} en_fsm_state;

// 默认只预览。点“门禁识别”后才开始开门判断。
en_fsm_state g_state = START_STREAM;

typedef struct
{
  char enroll_name[ENROLL_NAME_LEN];
} httpd_resp_value;

httpd_resp_value st_name;

// =========================
// 8. 基础工具
// =========================

static String face_label(const face_info_t &face)
{
  if (face.name.length() > 0)
  {
    return String(face.name.c_str());
  }
  return String("编号 ") + String(face.id);
}

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

static void send_status(WebsocketsClient &client, const String &message)
{
  Serial.println(message);
  client.send(message);
}

// =========================
// 9. 门禁硬件控制
// =========================

static void beep_once()
{
  digitalWrite(buzzer_pin, BUZZER_ON_LEVEL);
  delay(50);
  digitalWrite(buzzer_pin, BUZZER_IDLE_LEVEL);
}

static void beep_twice()
{
  beep_once();
  delay(60);
  beep_once();
}

static void set_idle_outputs()
{
  digitalWrite(green_pin, LOW);
  digitalWrite(red_pin, LOW);
  digitalWrite(buzzer_pin, BUZZER_IDLE_LEVEL);
}

static void open_door(WebsocketsClient &client, const String &name)
{
  if (digitalRead(relay_pin) == RELAY_IDLE_LEVEL)
  {
    digitalWrite(relay_pin, RELAY_OPEN_LEVEL);
    digitalWrite(green_pin, HIGH);
    digitalWrite(red_pin, LOW);
    beep_once();
    door_opened_millis = millis();

    Serial.println(String("门已打开：") + name);
    client.send(String("门已打开：") + name);
    client.send("door_open");
  }
}

static void close_door_if_needed()
{
  if (digitalRead(relay_pin) == RELAY_OPEN_LEVEL && millis() - door_opened_millis >= DOOR_OPEN_MS)
  {
    digitalWrite(relay_pin, RELAY_IDLE_LEVEL);
    set_idle_outputs();
    Serial.println("门禁继电器已复位");
  }
}

static void access_denied(WebsocketsClient &client)
{
  digitalWrite(green_pin, LOW);
  digitalWrite(red_pin, HIGH);
  beep_twice();
  send_status(client, "门禁拒绝：不是已录入人脸");
}

// =========================
// 10. 人脸列表管理
// =========================

static void send_face_list(WebsocketsClient &client)
{
  client.send("delete_faces");

  std::vector<face_info_t> faces = recognizer.get_enrolled_ids();
  std::vector<String> labels;

  for (const face_info_t &face : faces)
  {
    String label = face_label(face);
    if (label_seen(labels, label))
    {
      continue;
    }

    labels.push_back(label);
    client.send(String("listface:") + label);
  }
}

static void delete_face_by_name(const char *name)
{
  while (true)
  {
    std::vector<face_info_t> matches = recognizer.get_enrolled_ids_with_name(name);
    if (matches.empty())
    {
      break;
    }
    recognizer.delete_id(matches.front().id, false);
  }

  if (face_flash_ready)
  {
    recognizer.write_ids_to_flash();
  }
}

static void delete_all_faces(WebsocketsClient &client)
{
  recognizer.clear_id(face_flash_ready);
  client.send("delete_faces");
  send_status(client, "已删除全部人脸");
}

// =========================
// 11. 图像转换和检测
// =========================

static bool ensure_rgb_buffer(size_t required_len)
{
  if (rgb_buf && rgb_buf_len >= required_len)
  {
    return true;
  }

  if (rgb_buf)
  {
    free(rgb_buf);
    rgb_buf = NULL;
    rgb_buf_len = 0;
  }

  rgb_buf = (uint8_t *)(psramFound() ? ps_malloc(required_len) : malloc(required_len));
  if (!rgb_buf)
  {
    Serial.printf("RGB 缓冲区申请失败：%u 字节\n", (unsigned)required_len);
    return false;
  }

  rgb_buf_len = required_len;
  return true;
}

static bool prepare_rgb_frame(camera_fb_t *frame, dl::Tensor<uint8_t> &tensor)
{
  if (!frame)
  {
    return false;
  }

  size_t required_len = frame->width * frame->height * 3;
  if (!ensure_rgb_buffer(required_len))
  {
    return false;
  }

  if (!fmt2rgb888(frame->buf, frame->len, frame->format, rgb_buf))
  {
    Serial.println("图像转 RGB888 失败");
    return false;
  }

  tensor.set_element(rgb_buf)
        .set_shape({(int)frame->height, (int)frame->width, 3})
        .set_auto_free(false);
  return true;
}

static bool detect_faces(camera_fb_t *frame, std::list<dl::detect::result_t> **results)
{
  if (!frame || !rgb_buf)
  {
    return false;
  }

  std::list<dl::detect::result_t> &candidates =
      detector_stage1.infer(rgb_buf, {(int)frame->height, (int)frame->width, 3});
  *results = &detector_stage2.infer(rgb_buf, {(int)frame->height, (int)frame->width, 3}, candidates);
  return (*results)->size() > 0;
}

static bool send_preview_frame(camera_fb_t *frame, WebsocketsClient &client)
{
  if (!frame)
  {
    return false;
  }

  if (frame->format == PIXFORMAT_JPEG)
  {
    client.sendBinary((const char *)frame->buf, frame->len);
    return true;
  }

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
// 12. 人脸处理核心
// =========================

static void process_face_frame(camera_fb_t *frame, WebsocketsClient &client)
{
  if (g_state != START_DETECT && g_state != START_ENROLL && g_state != START_RECOGNITION)
  {
    return;
  }

  dl::Tensor<uint8_t> tensor;
  if (!prepare_rgb_frame(frame, tensor))
  {
    send_status(client, "图像处理失败");
    return;
  }

  std::list<dl::detect::result_t> *results = NULL;
  if (!detect_faces(frame, &results))
  {
    missed_face_count++;
    if (g_state != START_DETECT && missed_face_count >= 3)
    {
      client.send("未检测到人脸");
    }
    return;
  }

  missed_face_count = 0;
  last_detected_millis = millis();
  std::vector<int> landmarks = results->front().keypoint;

  if (g_state == START_DETECT)
  {
    send_status(client, "检测到人脸");
    return;
  }

  if (g_state == START_ENROLL)
  {
    if (strlen(st_name.enroll_name) == 0)
    {
      g_state = START_STREAM;
      send_status(client, "请先输入姓名");
      return;
    }

    if (recognizer.get_enrolled_id_num() >= FACE_ID_SAVE_NUMBER)
    {
      g_state = START_STREAM;
      send_status(client, "人脸库已满");
      return;
    }

    int id = recognizer.enroll_id(tensor, landmarks, st_name.enroll_name, face_flash_ready);
    if (id < 0)
    {
      send_status(client, "录入失败，请重新对准镜头");
      return;
    }

    enroll_samples_left--;
    int sample_number = ENROLL_CONFIRM_TIMES - enroll_samples_left;
    send_status(client, String("正在录入 ") + st_name.enroll_name + "：第 " + sample_number + "/" + ENROLL_CONFIRM_TIMES + " 次");

    if (enroll_samples_left <= 0)
    {
      g_state = START_STREAM;
      send_status(client, String(st_name.enroll_name) + " 录入完成");
      send_face_list(client);
    }
    return;
  }

  if (recognizer.get_enrolled_id_num() == 0)
  {
    send_status(client, "请先录入人脸");
    return;
  }

  face_info_t face = recognizer.recognize(tensor, landmarks);
  if (face.id >= 0)
  {
    String label = face_label(face);
    send_status(client, String("门禁通过：") + label);
    open_door(client, label);
  }
  else
  {
    access_denied(client);
  }
}

// =========================
// 13. 人脸识别初始化
// =========================

void app_facenet_main()
{
  face_flash_ready = false;

  // 阈值越大越严格，越小越宽松。
  recognizer.set_thresh(0.55F);

  if (!recognizer.set_partition(ESP_PARTITION_TYPE_DATA, ESP_PARTITION_SUBTYPE_ANY, "fr"))
  {
    Serial.println("未找到人脸分区 fr，进入临时模式：可以录入和识别，但重启后会丢失");
    return;
  }

  int loaded = recognizer.set_ids_from_flash();
  face_flash_ready = true;
  Serial.printf("Flash 模式：已载入 %d 个人脸样本\n", loaded);
}

// =========================
// 14. 网页服务器
// =========================

static esp_err_t index_handler(httpd_req_t *req)
{
  httpd_resp_set_type(req, "text/html");
  httpd_resp_set_hdr(req, "Content-Encoding", "gzip");
  return httpd_resp_send(req, (const char *)index_ov2640_html_gz, index_ov2640_html_gz_len);
}

httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL};

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
// 15. 网页消息处理
// =========================

void handle_message(WebsocketsClient &client, WebsocketsMessage msg)
{
  String data = msg.data();

  if (data == "stream")
  {
    g_state = START_STREAM;
    last_face_process_millis = 0;
    set_idle_outputs();
    send_status(client, "视频预览中");
  }
  else if (data == "detect")
  {
    g_state = START_DETECT;
    last_face_process_millis = 0;
    send_status(client, "正在检测人脸");
  }
  else if (data.startsWith("capture:"))
  {
    String person = data.substring(8);
    person.trim();
    if (person.length() == 0)
    {
      send_status(client, "请先输入姓名");
      return;
    }

    person.toCharArray(st_name.enroll_name, sizeof(st_name.enroll_name));
    enroll_samples_left = ENROLL_CONFIRM_TIMES;
    g_state = START_ENROLL;
    last_face_process_millis = 0;

    if (face_flash_ready)
    {
      send_status(client, "正在录入人脸");
    }
    else
    {
      send_status(client, "临时录入：重启后会丢失");
    }
  }
  else if (data == "recognise")
  {
    g_state = START_RECOGNITION;
    last_face_process_millis = 0;
    send_status(client, "门禁识别中");
  }
  else if (data.startsWith("remove:"))
  {
    String person = data.substring(7);
    person.trim();
    delete_face_by_name(person.c_str());
    send_face_list(client);
    send_status(client, String("已删除：") + person);
  }
  else if (data == "delete_all")
  {
    delete_all_faces(client);
  }
}

// =========================
// 16. setup：开机执行一次
// =========================

void setup()
{
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  pinMode(green_pin, OUTPUT);
  pinMode(red_pin, OUTPUT);
  pinMode(relay_pin, OUTPUT);
  pinMode(buzzer_pin, OUTPUT);

  digitalWrite(green_pin, LOW);
  digitalWrite(red_pin, LOW);
  digitalWrite(relay_pin, RELAY_IDLE_LEVEL);
  digitalWrite(buzzer_pin, BUZZER_IDLE_LEVEL);
  beep_once();

  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sscb_sda = SIOD_GPIO_NUM;
  config.pin_sscb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.frame_size = FRAMESIZE_UXGA;
  config.pixel_format = PIXFORMAT_JPEG;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = psramFound() ? CAMERA_FB_IN_PSRAM : CAMERA_FB_IN_DRAM;
  config.jpeg_quality = 15;
  config.fb_count = 1;

  if (psramFound())
  {
    config.jpeg_quality = 15;
    config.fb_count = 2;
  }
  else
  {
    Serial.println("未检测到 PSRAM，人脸处理能力会受限");
    config.frame_size = FRAMESIZE_SVGA;
  }

#if defined(CAMERA_MODEL_ESP_EYE)
  pinMode(13, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
#endif

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK)
  {
    Serial.printf("摄像头初始化失败，错误码 0x%x\n", err);
    return;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s->id.PID == OV3660_PID)
  {
    s->set_vflip(s, 1);
    s->set_brightness(s, 1);
    s->set_saturation(s, -2);
  }

  // QVGA 对 ESP32-CAM 的人脸识别比较稳。
  s->set_framesize(s, FRAMESIZE_QVGA);

#if defined(CAMERA_MODEL_M5STACK_WIDE)
  s->set_vflip(s, 1);
  s->set_hmirror(s, 1);
#endif

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  while (WiFi.status() != WL_CONNECTED)
  {
    delay(500);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi 已连接");

  app_httpserver_init();
  app_facenet_main();
  socket_server.listen(82);

  Serial.print("摄像头已就绪，请打开 http://");
  Serial.print(WiFi.localIP());
  Serial.println("/");

  digitalWrite(green_pin, HIGH);
  beep_once();
  digitalWrite(green_pin, LOW);
}

// =========================
// 17. loop：一直循环执行
// =========================

void loop()
{
  WebsocketsClient client = socket_server.accept();
  client.onMessage(handle_message);

  send_face_list(client);
  client.send("视频预览中");

  while (client.available())
  {
    client.poll();
    close_door_if_needed();

    fb = esp_camera_fb_get();
    if (!fb)
    {
      client.send("摄像头采集失败");
      continue;
    }

    bool face_mode = (g_state == START_DETECT || g_state == START_ENROLL || g_state == START_RECOGNITION);
    if (face_mode && (last_face_process_millis == 0 || millis() - last_face_process_millis >= current_face_process_interval()))
    {
      last_face_process_millis = millis();
      process_face_frame(fb, client);
    }

    if (g_state == START_DETECT && millis() - last_detected_millis > 800)
    {
      client.send("正在检测人脸");
    }

    if (last_preview_millis == 0 || millis() - last_preview_millis >= PREVIEW_INTERVAL_MS)
    {
      last_preview_millis = millis();
      send_preview_frame(fb, client);
    }

    esp_camera_fb_return(fb);
    fb = NULL;
  }
}
