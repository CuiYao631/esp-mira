// touch.ino - 触摸检测（BS8112A-3 芯片）

// ---------- BS8112A-3 触摸芯片 ----------
void sendBS8112Event(uint8_t key, bool pressed) {
  char textBuf[48];
  snprintf(textBuf, sizeof(textBuf), "KEY,%u,%s\r\n", key, pressed ? "PRESS" : "RELEASE");
  Serial.print(textBuf);
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
  Wire.begin(I2C_SDA, I2C_SCL);
  if (bs8112.begin("8112")) {
    Serial.println("BS8112A-3 初始化成功");
  } else {
    Serial.println("[警告] BS8112A-3 初始化失败，请检查 I2C 连接");
  }
}

// 触摸循环（在 loop 中调用）
void loopTouch() {
  pollBS8112();
}
