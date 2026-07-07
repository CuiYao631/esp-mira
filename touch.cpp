#include <Arduino.h>
#include <Wire.h>
#include <BS811X.h>
#include "touch.h"

// ==================== I2C & 触摸芯片 (BS8112A) ====================
#define I2C_SDA   4
#define I2C_SCL   5
#define TOUCH_KEY 10               // 仅使用 KEY 10

static BS811X bs8112;

void touchSetup() {
  Wire.begin(I2C_SDA, I2C_SCL);
  if (bs8112.begin("8112")) {
    Serial.println("BS8112A init OK.");
  } else {
    Serial.println("BS8112A init FAILED, check I2C wiring.");
  }
}

// ---------------------------------------------------------------------------
// 触摸：仅 KEY 10，串口打印 PRESS / HOLD / RELEASE 三种状态
// ---------------------------------------------------------------------------
void touchPoll() {
  bs8112.readKeys();                                   // 刷新芯片状态
  bool pressed = bs8112.getKey_passive(TOUCH_KEY);
  static bool    wasPressed = false;
  static uint8_t holdTick   = 0;

  if (pressed && !wasPressed) {
    Serial.println("TOUCH,PRESS");
    holdTick = 0;
  } else if (pressed && wasPressed) {
    if (++holdTick >= 25) { Serial.println("TOUCH,HOLD"); holdTick = 0; }  // ~每 500ms
  } else if (!pressed && wasPressed) {
    Serial.println("TOUCH,RELEASE");
  }
  wasPressed = pressed;
}
