# 第三方来源与权利说明

## Robot Zero One

本项目的网页门禁思路、早期工程结构和人脸门禁示例来自 Robot Zero One（GitHub 用户 `robotzero1`）的项目：

- https://github.com/robotzero1/esp32cam-access-control
- https://robotzero.one/access-control-with-face-recognition/

原项目仓库当前没有发现明确的 LICENSE 文件，因此本仓库保留来源说明，不把原作者代码声明为 CKSWITCH 原创，也不替原作者授予新的许可证。

## 更早的参考项目

Robot Zero One 的原教程还提到以下参考方向：

- ESP32-CAM Face Recognition for Home Automation
- ESP-WHO Face Recognition with WebSocket Communication

相关链接请以原教程页面的引用为准。若要将本项目用于商业产品或大范围再发布，请先确认上游作者和相关第三方项目的授权。

## CKSWITCH 改造范围

CKSWITCH（中文创客思维池）在本项目中完成或维护的内容包括：

- Arduino-ESP32 core 2.0.7 的新版人脸检测/识别 API 适配
- 中文网页和中文串口提示
- 本地门禁硬件控制版本
- 巴法云图片上传和状态上报版本
- 缓冲区复用、识别限速、预览限速和云端上报限流
- 中文操作手册、代码讲解和对比文档
- 配置脱敏和本仓库整理

这些新增或重写内容按仓库根目录 `LICENSE` 中的条款发布；第三方部分仍受其原有权利约束。
