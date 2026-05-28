// mira-ble.ino - 主入口文件（setup/loop/串口处理）
// 功能模块分布：
//   config.h  - 配置宏、引脚定义、数据结构
//   ble.ino   - BLE 通信和核心板数据管理
//   led.ino   - LED 控制、动画、命令解析
//   touch.ino - 触摸检测（原生电容触摸 + BS8112A-3）

#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <ArduinoJson.h>
#include <Adafruit_NeoPixel.h>
#include <BS811X.h>
#include <Wire.h>
#include <string.h>

#include "config.h"

// ---------- 共享全局变量（所有 .ino 文件可访问）----------
// BLE
BLEServer*         pServer         = nullptr;
BLECharacteristic* pCharacteristic = nullptr;
bool               deviceConnected = false;

String coreOsVersion  = "";
String coreMac        = "";
String coreWifiStatus = "";
String coreWifiSsid   = "";
String coreWifiIp     = "";
unsigned long lastCoreHeartbeatMs = 0;

// LED
Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);
LedPixel  g_ledStage[NUM_LEDS];
LedPixel  g_ledActive[NUM_LEDS];
bool      g_ledDirty    = true;
AnimState g_anim        = {};
AnimState g_animSaved   = {};

// 触摸
uint16_t g_touchThr = TOUCH_THRESHOLD;
volatile bool g_touchIRQ = false;
static void IRAM_ATTR onTouchISR(void* arg) {
  g_touchIRQ = true;
  (void)arg;
}

BS811X   bs8112;
uint16_t g_bs8112Keys     = 0;
uint16_t g_bs8112PrevKeys = 0;

// ---------- 串口输入缓冲 ----------
static char    g_usbTextBuf[128];
static uint8_t g_usbTextLen = 0;
static char    g_u2TextBuf[128];
static uint8_t g_u2TextLen  = 0;

void processSerialInput() {
  // Serial2（核心板）：JSON 心跳或 LED 命令
  while (Serial2.available()) {
    uint8_t b = (uint8_t)Serial2.read();
    if (b == '\n') {
      g_u2TextBuf[g_u2TextLen] = '\0';
      if (g_u2TextLen > 0 && g_u2TextBuf[g_u2TextLen - 1] == '\r')
        g_u2TextBuf[--g_u2TextLen] = '\0';
      if (g_u2TextLen > 0) {
        String line(g_u2TextBuf);
        Serial.println("[串口收到] " + line);
        if (isCoreHeartbeat(line)) {
          parseCoreHeartbeat(line);
        } else {
          processLedCommand(g_u2TextBuf);
          if (deviceConnected) {
            pCharacteristic->setValue(g_u2TextBuf);
            pCharacteristic->notify();
          }
        }
      }
      g_u2TextLen = 0;
    } else if (b != '\r' && b >= 0x20) {
      if (g_u2TextLen < sizeof(g_u2TextBuf) - 1)
        g_u2TextBuf[g_u2TextLen++] = (char)b;
    }
  }

  // USB Serial（调试终端）
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();
    if (b == '\n') {
      g_usbTextBuf[g_usbTextLen] = '\0';
      if (g_usbTextLen > 0 && g_usbTextBuf[g_usbTextLen - 1] == '\r')
        g_usbTextBuf[--g_usbTextLen] = '\0';
      if (g_usbTextLen > 0) processLedCommand(g_usbTextBuf);
      g_usbTextLen = 0;
    } else if (b != '\r' && b >= 0x20) {
      if (g_usbTextLen < sizeof(g_usbTextBuf) - 1)
        g_usbTextBuf[g_usbTextLen++] = (char)b;
    }
  }
}

// ---------- setup ----------
void setup() {
  Serial.begin(115200);
  Serial2.begin(115200, SERIAL_8N1, 16, 17);  // RX=16, TX=17（核心板）

  setupLED();
  setupTouch();
  setupBLE();

  Serial.println("Ready. 输入 HELP 查看 LED 命令列表。");
}

// ---------- loop ----------
void loop() {
  processSerialInput();
  tickAnimation();
  loopTouch();

  // LED 刷新
  if (g_ledDirty) {
    renderActiveLeds();
    g_ledDirty = false;
  }

  loopBLE();
  delay(20);
}
