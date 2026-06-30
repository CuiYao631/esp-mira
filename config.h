#ifndef CONFIG_H
#define CONFIG_H

// ==================== 1. I2C & 触摸芯片引脚配置 ====================
#define I2C_SDA     4
#define I2C_SCL     5
#define BS8112_KEYS 12   // BS8112A-3 共 12 个触摸键

// ==================== 2. WS2812 引脚与各路灯珠数量配置 ====================
#define RGB1_PIN    3
#define RGB1_COUNT  36   // 外圈 1

#define RGB2_PIN    6
#define RGB2_COUNT  30   // 中外圈 2

#define RGB3_PIN    7
#define RGB3_COUNT  24   // 中内圈 3

#define RGB4_PIN    10
#define RGB4_COUNT  18   // 内圈 4

#define NUM_STRIPS  4
#define NUM_LEDS    (RGB1_COUNT + RGB2_COUNT + RGB3_COUNT + RGB4_COUNT)  // 108

// 各路灯带在全局数组中的起始偏移
#define RGB1_OFFSET 0
#define RGB2_OFFSET (RGB1_COUNT)
#define RGB3_OFFSET (RGB1_COUNT + RGB2_COUNT)
#define RGB4_OFFSET (RGB1_COUNT + RGB2_COUNT + RGB3_COUNT)

// ---------- 动画参数 ----------
#define BREATHE_SPEED    2        // 每 tick 的相位步进（510 相位循环）
#define WAKE_FADE_TICKS  80       // 每环淡入所需 tick 数（约 1.6 秒）
#define SPIN_SPEED_F     0.20f    // 每 tick 移动的环位置数

// ---------- 共享数据结构 ----------
static const uint8_t GROUP_1 = 1;   // RGB1 外圈
static const uint8_t GROUP_2 = 2;   // RGB2 中外圈
static const uint8_t GROUP_3 = 3;   // RGB3 中内圈
static const uint8_t GROUP_4 = 4;   // RGB4 内圈

struct LedPixel {
  uint8_t r, g, b, bri;
};

enum AnimMode { ANIM_NONE = 0, ANIM_BREATHE, ANIM_WAKE, ANIM_SPIN };

struct AnimState {
  AnimMode mode;
  bool     rainbow;
  uint8_t  r, g, b, maxBri;
  int      brPhase;   // BREATHE: 0..509
  int      wkStep;    // WAKE: 环索引 0..3（4 圈依次亮起）
  int      wkTimer;   // WAKE: 当前环已过 tick 数
  float    spinPos[4]; // SPIN: 每圈头部位置（小数）
  int8_t   spinDir[4]; // SPIN: 每圈方向 +1=CW, -1=CCW
};

#endif // CONFIG_H
