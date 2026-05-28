#ifndef CONFIG_H
#define CONFIG_H

// ---------- BLE 配置 ----------
#define DEVICE_PREFIX "Mira"
#define DEVICE_NAME   DEVICE_PREFIX "-ESP32"

#define SERVICE_UUID        "12345678-1234-1234-1234-123456789abc"
#define CHARACTERISTIC_UUID "abcd1234-ab12-ab12-ab12-abcdef012345"

#define LIGHT_VER "0.9.3"

// 核心板心跳超时：10秒未收到则判定通讯异常并清空数据
#define CORE_HEARTBEAT_TIMEOUT_MS 10000UL

// ---------- LED 配置 ----------
#define LED_PIN    15
#define OUTER_RING 24
#define INNER_RING 16
#define NUM_LEDS   (OUTER_RING + INNER_RING)

#define BREATHE_SPEED    2        // 每 tick 的相位步进（510 相位循环）
#define WAKE_FADE_TICKS  80       // 每环淡入所需 tick 数（约 1.6 秒）
#define SPIN_SPEED_F     0.20f    // 每 tick 移动的环位置数
#define SPIN_TAIL_LEN    8        // 彗星尾长（保留供参考）

// ---------- 触摸配置 ----------
#define TOUCH_PIN        14
#define TOUCH_THRESHOLD  32

// BS8112A-3 I2C 触摸芯片
#define BS8112_SDA   21
#define BS8112_SCL   22
#define BS8112_KEYS  12   // BS8112A-3 共 12 个触摸键

// ---------- 共享数据结构 ----------
static const uint8_t GROUP_OUTER       = 0;
static const uint8_t GROUP_INNER       = 1;
static const uint8_t TOUCH_EVT_PRESS   = 1;
static const uint8_t TOUCH_EVT_RELEASE = 2;
static const uint8_t TOUCH_EVT_HOLD    = 3;

struct LedPixel {
  uint8_t r, g, b, bri;
};

enum AnimMode { ANIM_NONE = 0, ANIM_BREATHE, ANIM_WAKE, ANIM_SPIN };

struct AnimState {
  AnimMode mode;
  bool     rainbow;
  uint8_t  r, g, b, maxBri;
  int      brPhase;   // BREATHE: 0..509
  int      wkStep;    // WAKE: 环索引 0..1
  int      wkTimer;   // WAKE: 当前环已过 tick 数
  float    outerPos;  // SPIN: 外环头部位置（小数）
  float    innerPos;  // SPIN: 内环头部位置（小数）
  int8_t   outerDir;  // SPIN: +1=CW, -1=CCW
  int8_t   innerDir;
};

#endif // CONFIG_H
