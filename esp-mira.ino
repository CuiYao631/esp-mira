// ===========================================================================
// esp-mira 主程序
//   - LED 灯效模块： led.h / led.cpp
//   - 触摸模块：     touch.h / touch.cpp
// 本文件只负责初始化、主循环调度和串口命令的读取。
// ===========================================================================
#include <Arduino.h>
#include "led.h"
#include "touch.h"

// 串口行缓冲
static char    g_usbTextBuf[128];
static uint8_t g_usbTextLen = 0;
static void processSerialDataInput();

void setup() {
  Serial.begin(115200);

  ledSetup();     // 初始化 4 路 WS2812 灯环
  touchSetup();   // 初始化 I2C 与 BS8112A 触摸芯片

  Serial.println("Ready.");
  Serial.println("Commands:");
  ledPrintCommands();
}

void loop() {
  processSerialDataInput();  // 读取串口命令并交给 LED 模块
  touchPoll();               // 触摸检测 -> 串口打印 PRESS/HOLD/RELEASE
  ledTick();                 // 推进灯效动画并按需刷新

  delay(20);
}

// 逐字节读取串口，遇到换行后把整行命令交给 LED 模块处理
static void processSerialDataInput() {
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();
    if (b == '\n') {
      g_usbTextBuf[g_usbTextLen] = '\0';
      if (g_usbTextLen > 0 && g_usbTextBuf[g_usbTextLen - 1] == '\r')
        g_usbTextBuf[--g_usbTextLen] = '\0';
      if (g_usbTextLen > 0) ledProcessCommand(g_usbTextBuf);
      g_usbTextLen = 0;
    } else if (b != '\r' && b >= 0x20) {
      if (g_usbTextLen < sizeof(g_usbTextBuf) - 1)
        g_usbTextBuf[g_usbTextLen++] = (char)b;
    }
  }
}
