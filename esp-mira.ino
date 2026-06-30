// esp-mira.ino - 主入口文件（setup/loop/串口处理）
// 功能模块分布：
//   config.h  - 配置宏、引脚定义、数据结构
//   led.ino   - LED 控制、动画、命令解析
//   touch.ino - 触摸检测（原生电容触摸 + BS8112A-3）

#include <Adafruit_NeoPixel.h>
#include <BS811X.h>
#include <Wire.h>
#include <string.h>

#include "config.h"

// ---------- 共享全局变量（所有 .ino 文件可访问）----------
// LED（4 路独立 WS2812）
Adafruit_NeoPixel strips[NUM_STRIPS] = {
  Adafruit_NeoPixel(RGB1_COUNT, RGB1_PIN, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(RGB2_COUNT, RGB2_PIN, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(RGB3_COUNT, RGB3_PIN, NEO_GRB + NEO_KHZ800),
  Adafruit_NeoPixel(RGB4_COUNT, RGB4_PIN, NEO_GRB + NEO_KHZ800),
};
LedPixel  g_ledStage[NUM_LEDS];
LedPixel  g_ledActive[NUM_LEDS];
bool      g_ledDirty    = true;
AnimState g_anim        = {};
AnimState g_animSaved   = {};

// 触摸（BS8112A-3）
BS811X   bs8112;
uint16_t g_bs8112Keys     = 0;
uint16_t g_bs8112PrevKeys = 0;

// ---------- 串口输入缓冲 ----------
static char    g_usbTextBuf[128];
static uint8_t g_usbTextLen = 0;

void processSerialInput() {
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

  setupLED();
  setupTouch();

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

  delay(20);
}
