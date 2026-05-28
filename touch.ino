// touch.ino - 触摸检测（原生电容触摸 + BS8112A-3 芯片）

void sendTouchEvent(uint8_t type, uint16_t raw) {
  const char* evtName;
  if (type == TOUCH_EVT_PRESS)        evtName = "PRESS";
  else if (type == TOUCH_EVT_RELEASE) evtName = "RELEASE";
  else                                evtName = "HOLD";

  char textBuf[32];
  snprintf(textBuf, sizeof(textBuf), "TOUCH,%s,%u\r\n", evtName, raw);
  Serial.print(textBuf);
  Serial2.print(textBuf);
  if (deviceConnected) {
    pCharacteristic->setValue(textBuf);
    pCharacteristic->notify();
  }
}

// ---------- BS8112A-3 触摸芯片 ----------
void sendBS8112Event(uint8_t key, bool pressed) {
  char textBuf[48];
  snprintf(textBuf, sizeof(textBuf), "KEY,%u,%s\r\n", key, pressed ? "PRESS" : "RELEASE");
  Serial.print(textBuf);
  Serial2.print(textBuf);
  if (deviceConnected) {
    pCharacteristic->setValue(textBuf);
    pCharacteristic->notify();
  }
}

void pollBS8112() {
  g_bs8112PrevKeys = g_bs8112Keys;
  g_bs8112Keys = bs8112.readKeys();

  for (uint8_t i = 1; i <= BS8112_KEYS; i++) {
    bool wasPressed = bitRead(g_bs8112PrevKeys, i - 1);
    bool isPressed  = bitRead(g_bs8112Keys, i - 1);
    if (isPressed && !wasPressed) {
      sendBS8112Event(i, true);
    } else if (!isPressed && wasPressed) {
      sendBS8112Event(i, false);
    }
  }
}

// 触摸初始化（在 setup 中调用）
void setupTouch() {
  touchAttachInterruptArg(TOUCH_PIN, onTouchISR, nullptr, g_touchThr);

  Wire.begin(BS8112_SDA, BS8112_SCL);
  if (bs8112.begin("8112")) {
    Serial.println("BS8112A-3 初始化成功");
  } else {
    Serial.println("[警告] BS8112A-3 初始化失败，请检查 I2C 连接");
  }
}

// 触摸循环（在 loop 中调用）
void loopTouch() {
  pollBS8112();

  static bool lastTouched = false;
  static unsigned long lastHoldReport = 0;

  uint16_t touchVal = (uint16_t)touchRead(TOUCH_PIN);
  bool isTouched = (touchVal < g_touchThr) || g_touchIRQ;
  g_touchIRQ = false;

  if (isTouched && !lastTouched)  sendTouchEvent(TOUCH_EVT_PRESS,   touchVal);
  if (!isTouched && lastTouched)  sendTouchEvent(TOUCH_EVT_RELEASE, touchVal);
  if (isTouched && millis() - lastHoldReport >= 500) {
    lastHoldReport = millis();
    sendTouchEvent(TOUCH_EVT_HOLD, touchVal);
  }
  lastTouched = isTouched;
}
