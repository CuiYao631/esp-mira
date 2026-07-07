#include <Arduino.h>
#include <Adafruit_NeoPixel.h>
#include <string.h>
#include "led.h"

// ==================== WS2812：4 路独立灯环 ====================
#define RGB1_PIN   3
#define RGB1_COUNT 36              // 外圈   (ring 1)
#define RGB2_PIN   6
#define RGB2_COUNT 30              // 中外圈 (ring 2)
#define RGB3_PIN   7
#define RGB3_COUNT 24              // 中内圈 (ring 3)
#define RGB4_PIN   10
#define RGB4_COUNT 18              // 内圈   (ring 4)

#define N_RINGS  4
#define NUM_LEDS (RGB1_COUNT + RGB2_COUNT + RGB3_COUNT + RGB4_COUNT)

static Adafruit_NeoPixel rgb1(RGB1_COUNT, RGB1_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel rgb2(RGB2_COUNT, RGB2_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel rgb3(RGB3_COUNT, RGB3_PIN, NEO_GRB + NEO_KHZ800);
static Adafruit_NeoPixel rgb4(RGB4_COUNT, RGB4_PIN, NEO_GRB + NEO_KHZ800);

// 逻辑缓冲 0..NUM_LEDS-1 按环顺序排布，渲染时分发到 4 条灯带
static Adafruit_NeoPixel* const g_rings[N_RINGS]   = { &rgb1, &rgb2, &rgb3, &rgb4 };
static const int              g_ringCount[N_RINGS] = { RGB1_COUNT, RGB2_COUNT, RGB3_COUNT, RGB4_COUNT };
static const int              g_ringStart[N_RINGS] = { 0,
                                                       RGB1_COUNT,
                                                       RGB1_COUNT + RGB2_COUNT,
                                                       RGB1_COUNT + RGB2_COUNT + RGB3_COUNT };

// 静态颜色辅助函数 (ColorHSV / gamma32) 通过该引用调用，动画代码沿用 strip.*
static Adafruit_NeoPixel& strip = rgb1;

// THINK 各环物理角度对齐偏置(度)，圆形灯板方向歪了可微调这里
static float RING_ANGLE_OFFSET[N_RINGS] = { 0.0f, 0.0f, 0.0f, 0.0f };

#define BREATHE_SPEED   2          // phase steps per tick (510-phase cycle)
#define WAKE_FADE_TICKS 80         // ticks per ring fade-in (~1.6s per ring)
#define SPIN_SPEED_F    0.20f      // ring positions per tick
#define SPIN_TAIL_POWER 0.55f      // 拖尾衰减曲线指数(越小拖尾越长, 0.3~0.8)
#define SPIN_HEAD_BOOST 1.35f      // 彗星头部亮度增益(1.0=无增益)
#define SPIN_GLOW_RANGE 3          // 头部泛光半径(颗)
#define SPIN_GLOW_FALLOFF 0.25f    // 泛光衰减系数(每远离1颗灯珠的亮度比例)
#define SIRI_SPEED_F    0.08f      // THINK 流体动画时间步进(可微调流动速度)
#define GLOBAL_GLOW_SELF   0.70f   // 全局泛光：自身保留比例
#define GLOBAL_GLOW_NEIGHBOR 0.15f // 全局泛光：相邻灯珠混入比例

struct LedPixel {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t bri;
};

enum AnimMode { ANIM_NONE = 0, ANIM_BREATHE, ANIM_WAKE, ANIM_SPIN, ANIM_SIRI };

struct AnimState {
  AnimMode mode;
  bool     rainbow;
  uint8_t  r, g, b, maxBri;
  int      brPhase;              // BREATHE: 0..509
  int      wkStep;               // WAKE: 当前渐入的进度 (0..N_RINGS)
  int      wkTimer;              // WAKE: ticks since ring fade started
  float    ringPos[N_RINGS];     // SPIN: 每环彗星头部(小数)
  int8_t   ringDir[N_RINGS];     // SPIN: +1=CW, -1=CCW
  float    siriTime;             // THINK: 动画时间轴
};

static LedPixel  g_ledStage[NUM_LEDS];
static LedPixel  g_ledActive[NUM_LEDS];
static bool      g_ledDirty = true;
static AnimState g_anim     = {};
static AnimState g_animSaved = {};  // last active anim, restored by RESUME

static int  mapGroupIndexToLed(int group, int idx);
static void copyStageToActive();
static void clearActiveLeds();
static void renderActiveLeds();
static void tickAnimation();
static uint32_t getSiriColor(int index, int count, int ringIndex, float time, float angleOffset);

// ---------------------------------------------------------------------------
// 公共接口
// ---------------------------------------------------------------------------
void ledSetup() {
  rgb1.begin(); rgb2.begin(); rgb3.begin(); rgb4.begin();
  rgb1.setBrightness(255); rgb2.setBrightness(255);
  rgb3.setBrightness(255); rgb4.setBrightness(255);

  for (int i = 0; i < NUM_LEDS; i++) {
    g_ledStage[i]  = {0, 0, 0, 0};
    g_ledActive[i] = {0, 0, 0, 0};
  }
  renderActiveLeds();
}

// 全局泛光后处理：每颗灯珠混入相邻灯珠的颜色，产生柔光扩散效果
static void applyGlobalGlow() {
  static LedPixel glowBuf[NUM_LEDS];
  memcpy(glowBuf, g_ledActive, sizeof(g_ledActive));

  for (int ring = 0; ring < N_RINGS; ring++) {
    int start = g_ringStart[ring];
    int count = g_ringCount[ring];
    for (int i = 0; i < count; i++) {
      int idx = start + i;
      // 左邻
      int left  = start + ((i - 1 + count) % count);
      // 右邻
      int right = start + ((i + 1) % count);

      LedPixel& self = glowBuf[idx];
      LedPixel& L    = g_ledActive[left];
      LedPixel& R    = g_ledActive[right];

      // 自身保留 + 左右邻居混入
      uint16_t r = (uint16_t)((float)self.r * GLOBAL_GLOW_SELF +
                              (float)L.r * GLOBAL_GLOW_NEIGHBOR +
                              (float)R.r * GLOBAL_GLOW_NEIGHBOR + 0.5f);
      uint16_t g = (uint16_t)((float)self.g * GLOBAL_GLOW_SELF +
                              (float)L.g * GLOBAL_GLOW_NEIGHBOR +
                              (float)R.g * GLOBAL_GLOW_NEIGHBOR + 0.5f);
      uint16_t b = (uint16_t)((float)self.b * GLOBAL_GLOW_SELF +
                              (float)L.b * GLOBAL_GLOW_NEIGHBOR +
                              (float)R.b * GLOBAL_GLOW_NEIGHBOR + 0.5f);
      uint16_t bri = (uint16_t)((float)self.bri * GLOBAL_GLOW_SELF +
                                (float)L.bri * GLOBAL_GLOW_NEIGHBOR +
                                (float)R.bri * GLOBAL_GLOW_NEIGHBOR + 0.5f);

      if (r > 255) r = 255; if (g > 255) g = 255;
      if (b > 255) b = 255; if (bri > 255) bri = 255;

      self.r = (uint8_t)r; self.g = (uint8_t)g;
      self.b = (uint8_t)b; self.bri = (uint8_t)bri;
    }
  }
  memcpy(g_ledActive, glowBuf, sizeof(g_ledActive));
}

void ledTick() {
  tickAnimation();
  if (g_ledDirty) {
    applyGlobalGlow();
    renderActiveLeds();
    g_ledDirty = false;
  }
}

void ledPrintCommands() {
  Serial.println("=== LED Commands ===");
  Serial.println("  ALL,R,G,B,BRI                       set all 108 LEDs");
  Serial.println("  ONE,grp,idx,R,G,B,BRI               set one LED (grp 0-3)");
  Serial.println("  BRI,val                             set brightness (0-255)");
  Serial.println("  OFF                                 turn off all LEDs");
  Serial.println("  RAINBOW[,BRI]                       static rainbow (default BRI=200)");
  Serial.println("  BREATHE[,R,G,B[,BRI]|RAINBOW[,BRI]] breathing effect");
  Serial.println("  WAKE[,R,G,B[,BRI]|RAINBOW[,BRI]]    wake-up: inner->outer fade-in");
  Serial.println("  SPIN[,R,G,B[,ODIR,IDIR[,BRI]]|RAINBOW[,ODIR,IDIR[,BRI]]]");
  Serial.println("                                       spinning comet (ODIR/IDIR: 0=CW, 1=CCW)");
  Serial.println("                                       outer rings 0/2, inner rings 1/3");
  Serial.println("  THINK[,BRI]                         fluid thinking effect (default BRI=150)");
  Serial.println("  STOP                                stop animation (state saved)");
  Serial.println("  RESUME                              resume last stopped animation");
  Serial.println("  HELP                                show this help");
}

// ---------------------------------------------------------------------------
// 内部工具
// ---------------------------------------------------------------------------
static int mapGroupIndexToLed(int group, int idx) {
  if (group < 0 || group >= N_RINGS) return -1;
  if (idx < 0 || idx >= g_ringCount[group]) return -1;
  return g_ringStart[group] + idx;
}

static void copyStageToActive() {
  memcpy(g_ledActive, g_ledStage, sizeof(g_ledActive));
}

static void clearActiveLeds() {
  for (int i = 0; i < NUM_LEDS; i++) g_ledActive[i] = {0, 0, 0, 0};
}

static void renderActiveLeds() {
  for (int ring = 0; ring < N_RINGS; ring++) {
    Adafruit_NeoPixel* px = g_rings[ring];
    int start = g_ringStart[ring];
    int count = g_ringCount[ring];
    for (int i = 0; i < count; i++) {
      const LedPixel& p = g_ledActive[start + i];
      uint8_t r = (uint8_t)(((uint16_t)p.r * p.bri) / 255);
      uint8_t g = (uint8_t)(((uint16_t)p.g * p.bri) / 255);
      uint8_t b = (uint8_t)(((uint16_t)p.b * p.bri) / 255);
      px->setPixelColor(i, r, g, b);
    }
    px->show();
  }
}

// ---------------------------------------------------------------------------
// Animation helpers
// ---------------------------------------------------------------------------

static void animBreatheTick() {
  g_anim.brPhase += BREATHE_SPEED;
  if (g_anim.brPhase >= 510) g_anim.brPhase = 0;
  uint8_t lvl = (g_anim.brPhase < 255) ? (uint8_t)g_anim.brPhase
                                        : (uint8_t)(510 - g_anim.brPhase);
  uint8_t bri = (uint8_t)((uint16_t)lvl * g_anim.maxBri / 254);

  for (int i = 0; i < NUM_LEDS; i++) {
    if (g_anim.rainbow) {
      uint16_t hue = (uint16_t)((uint32_t)i * 65536 / NUM_LEDS);
      uint32_t c = strip.gamma32(strip.ColorHSV(hue, 255, 255));
      g_ledActive[i] = { (uint8_t)((c >> 16) & 0xFF),
                         (uint8_t)((c >>  8) & 0xFF),
                         (uint8_t)( c        & 0xFF), bri };
    } else {
      g_ledActive[i] = { g_anim.r, g_anim.g, g_anim.b, bri };
    }
  }
  g_ledDirty = true;
}

static void animWakeTick() {
  // wkStep: 0..N_RINGS-1 依次从内圈到外圈渐入，完成后停止
  if (g_anim.wkStep >= N_RINGS) { g_anim.mode = ANIM_NONE; return; }

  int ring  = (N_RINGS - 1) - g_anim.wkStep;   // 从内圈(ring 4) 向外圈(ring 1)
  int start = g_ringStart[ring];
  int count = g_ringCount[ring];

  uint8_t bri = (g_anim.wkTimer >= WAKE_FADE_TICKS)
                ? g_anim.maxBri
                : (uint8_t)((uint16_t)g_anim.wkTimer * g_anim.maxBri / WAKE_FADE_TICKS);

  for (int i = 0; i < count; i++) {
    int led = start + i;
    if (g_anim.rainbow) {
      uint16_t hue = (uint16_t)((uint32_t)i * 65536 / count);
      uint32_t c = strip.gamma32(strip.ColorHSV(hue, 255, 255));
      g_ledActive[led] = { (uint8_t)((c >> 16) & 0xFF),
                           (uint8_t)((c >>  8) & 0xFF),
                           (uint8_t)( c        & 0xFF), bri };
    } else {
      g_ledActive[led] = { g_anim.r, g_anim.g, g_anim.b, bri };
    }
  }

  if (++g_anim.wkTimer > WAKE_FADE_TICKS) {
    g_anim.wkTimer = 0;
    g_anim.wkStep++;
  }
  g_ledDirty = true;
}

static void animSpinTick() {
  for (int ring = 0; ring < N_RINGS; ring++) {
    int   count = g_ringCount[ring];
    int   start = g_ringStart[ring];
    float fC    = (float)count;

    g_anim.ringPos[ring] += g_anim.ringDir[ring] * SPIN_SPEED_F;
    if (g_anim.ringPos[ring] <  0)  g_anim.ringPos[ring] += fC;
    if (g_anim.ringPos[ring] >= fC) g_anim.ringPos[ring] -= fC;

    // 第一遍：计算基础拖尾亮度
    uint8_t baseBri[36];  // 最大环 36 颗
    for (int i = 0; i < count; i++) {
      float dist = (g_anim.ringDir[ring] > 0)
                   ? fmodf(g_anim.ringPos[ring] - (float)i + fC, fC)
                   : fmodf((float)i - g_anim.ringPos[ring] + fC, fC);
      float t = (fC - 1.0f - dist) / (fC - 1.0f);
      if (t < 0.0f) t = 0.0f;
      // 可调拖尾曲线：pow(t, SPIN_TAIL_POWER)，指数越小拖尾越长越柔和
      float tailVal = powf(t, SPIN_TAIL_POWER);
      // 彗星头部增亮
      if (t > 0.85f) tailVal *= SPIN_HEAD_BOOST;
      uint8_t bri = (uint8_t)((float)g_anim.maxBri * tailVal + 0.5f);
      if (bri == 0 && t > 0.0f) bri = 1;
      baseBri[i] = bri;
    }

    // 第二遍：头部泛光——彗星头部向周围灯珠扩散额外亮度
    int headIdx = (int)(g_anim.ringPos[ring] + 0.5f) % count;
    for (int i = 0; i < count; i++) {
      int glowDist = abs(i - headIdx);
      if (glowDist > count / 2) glowDist = count - glowDist;  // 环形最短距离
      if (glowDist <= SPIN_GLOW_RANGE && glowDist > 0) {
        float glowFactor = powf(SPIN_GLOW_FALLOFF, (float)glowDist);
        uint16_t glowBri = (uint16_t)((float)g_anim.maxBri * glowFactor * 0.55f);
        uint16_t combined = (uint16_t)baseBri[i] + glowBri;
        if (combined > 255) combined = 255;
        baseBri[i] = (uint8_t)combined;
      }
    }

    // 第三遍：写入 LED 缓冲
    for (int i = 0; i < count; i++) {
      int led = start + i;
      if (g_anim.rainbow) {
        uint16_t hue = (uint16_t)((uint32_t)i * 65536 / count);
        uint32_t c = strip.gamma32(strip.ColorHSV(hue, 255, 255));
        g_ledActive[led] = { (uint8_t)((c >> 16) & 0xFF),
                             (uint8_t)((c >>  8) & 0xFF),
                             (uint8_t)( c        & 0xFF), baseBri[i] };
      } else {
        g_ledActive[led] = { g_anim.r, g_anim.g, g_anim.b, baseBri[i] };
      }
    }
  }
  g_ledDirty = true;
}

// 🌟 核心算法：为单个灯珠计算思考流体色彩与亮度 🌟
// index: 当前灯珠索引，count: 该圈总灯珠数，ringIndex: 第几圈(1-4)，time: 动画时间
static uint32_t getSiriColor(int index, int count, int ringIndex, float time, float angleOffset) {
  // 1. 计算当前灯珠在圆圈中的弧度位置 (0 到 2*PI)
  float angle = (index * 2.0 * PI / count) + (angleOffset * PI / 180.0);

  // 2. 特效核心：叠加两个不同频率的正弦波，创造有机的流体呼吸感
  float wave1 = sin(angle + time * 1.2 + ringIndex * 0.5);
  float wave2 = cos(angle * 2.0 - time * 0.8 + ringIndex * 0.3);
  float combinedWave = (wave1 + wave2) / 2.0; // 范围 -1.0 到 1.0

  // 3. 动态色相调制：洋红->蓝色->青色 之间快速过渡
  uint32_t baseHue = 43000 + (int)(combinedWave * 8000.0) + (int)(time * 500.0);

  // 4. 动态亮度/饱和度调制：中央亮、两端如烟雾般虚化
  float brightnessParam = (combinedWave + 1.0) / 2.0; // 缩放到 0.0 - 1.0
  uint8_t value = (uint8_t)(pow(brightnessParam, 2.0) * 255.0);
  uint8_t saturation = (uint8_t)(200 + (1.0 - brightnessParam) * 55.0);

  // 5. 生成 HSV 颜色(伽马矫正在调用处用 gamma32 完成)
  return Adafruit_NeoPixel::ColorHSV(baseHue, saturation, value);
}

static void animSiriTick() {
  for (int ring = 0; ring < N_RINGS; ring++) {
    int count = g_ringCount[ring];
    int start = g_ringStart[ring];
    for (int i = 0; i < count; i++) {
      uint32_t c = strip.gamma32(
          getSiriColor(i, count, ring + 1, g_anim.siriTime, RING_ANGLE_OFFSET[ring]));
      g_ledActive[start + i] = { (uint8_t)((c >> 16) & 0xFF),
                                 (uint8_t)((c >>  8) & 0xFF),
                                 (uint8_t)( c        & 0xFF), g_anim.maxBri };
    }
  }
  g_anim.siriTime += SIRI_SPEED_F;
  g_ledDirty = true;
}

static void tickAnimation() {
  switch (g_anim.mode) {
    case ANIM_BREATHE: animBreatheTick(); break;
    case ANIM_WAKE:    animWakeTick();    break;
    case ANIM_SPIN:    animSpinTick();    break;
    case ANIM_SIRI:    animSiriTick();    break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
// 串口命令处理
// ---------------------------------------------------------------------------
void ledProcessCommand(const char* line) {
  int r, g, b, bri, grp, idx, val;

  if (strncmp(line, "ALL,", 4) == 0) {
    g_anim.mode = ANIM_NONE;
    if (sscanf(line, "ALL,%d,%d,%d,%d", &r, &g, &b, &bri) == 4) {
      for (int i = 0; i < NUM_LEDS; i++)
        g_ledStage[i] = {(uint8_t)constrain(r,0,255), (uint8_t)constrain(g,0,255),
                         (uint8_t)constrain(b,0,255), (uint8_t)constrain(bri,0,255)};
      copyStageToActive();
      g_ledDirty = true;
      Serial.printf("OK ALL %d,%d,%d,%d\r\n", r, g, b, bri);
    } else {
      Serial.print("ERR format: ALL,R,G,B,BRI\r\n");
    }

  } else if (strncmp(line, "ONE,", 4) == 0) {
    g_anim.mode = ANIM_NONE;
    if (sscanf(line, "ONE,%d,%d,%d,%d,%d,%d", &grp, &idx, &r, &g, &b, &bri) == 6) {
      int led = mapGroupIndexToLed(grp, idx);
      if (led < 0) {
        Serial.print("ERR bad index\r\n");
        return;
      }
      for (int i = 0; i < NUM_LEDS; i++) g_ledStage[i] = {0, 0, 0, 0};
      g_ledStage[led] = {(uint8_t)constrain(r,0,255), (uint8_t)constrain(g,0,255),
                         (uint8_t)constrain(b,0,255), (uint8_t)constrain(bri,0,255)};
      copyStageToActive();
      g_ledDirty = true;
      Serial.printf("OK ONE grp=%d idx=%d %d,%d,%d,%d\r\n", grp, idx, r, g, b, bri);
    } else {
      Serial.print("ERR format: ONE,grp,idx,R,G,B,BRI\r\n");
    }

  } else if (strncmp(line, "BRI,", 4) == 0) {
    g_anim.mode = ANIM_NONE;
    if (sscanf(line, "BRI,%d", &val) == 1) {
      val = constrain(val, 0, 255);
      for (int i = 0; i < NUM_LEDS; i++)
        g_ledStage[i].bri = (uint8_t)val;
      copyStageToActive();
      g_ledDirty = true;
      Serial.printf("OK BRI %d\r\n", val);
    } else {
      Serial.print("ERR format: BRI,0-255\r\n");
    }

  } else if (strcmp(line, "OFF") == 0) {
    g_anim.mode = ANIM_NONE;
    clearActiveLeds();
    g_ledDirty = true;
    Serial.print("OK OFF\r\n");

  } else if (strncmp(line, "RAINBOW", 7) == 0) {
    g_anim.mode = ANIM_NONE;
    int rbri = 200;
    sscanf(line, "RAINBOW,%d", &rbri);
    rbri = constrain(rbri, 0, 255);
    for (int i = 0; i < NUM_LEDS; i++) {
      uint16_t hue = (uint16_t)((uint32_t)i * 65536 / NUM_LEDS);
      uint32_t c = strip.ColorHSV(hue, 255, 255);
      c = strip.gamma32(c);
      g_ledStage[i] = {(uint8_t)((c >> 16) & 0xFF), (uint8_t)((c >> 8) & 0xFF),
                        (uint8_t)(c & 0xFF), (uint8_t)rbri};
    }
    copyStageToActive();
    g_ledDirty = true;
    Serial.printf("OK RAINBOW bri=%d\r\n", rbri);

  } else if (strncmp(line, "BREATHE", 7) == 0) {
    AnimState na = {};
    na.maxBri = 200; na.r = 255; na.g = 255; na.b = 255;
    const char* p = line + 7;
    if (*p == ',') {
      p++;
      if (strncmp(p, "RAINBOW", 7) == 0) {
        na.rainbow = true; p += 7;
        int v; if (*p == ',' && sscanf(p + 1, "%d", &v) == 1) na.maxBri = (uint8_t)constrain(v, 0, 255);
      } else {
        int tr, tg, tb, tbri;
        int n = sscanf(p, "%d,%d,%d,%d", &tr, &tg, &tb, &tbri);
        if (n < 3) { Serial.print("ERR format: BREATHE[,R,G,B[,BRI]|RAINBOW[,BRI]]\r\n"); return; }
        na.r = (uint8_t)constrain(tr,0,255); na.g = (uint8_t)constrain(tg,0,255); na.b = (uint8_t)constrain(tb,0,255);
        if (n >= 4) na.maxBri = (uint8_t)constrain(tbri, 0, 255);
      }
    }
    clearActiveLeds(); na.mode = ANIM_BREATHE; g_anim = na;
    Serial.printf("OK BREATHE %s bri=%d\r\n", g_anim.rainbow ? "RAINBOW" : "COLOR", g_anim.maxBri);

  } else if (strncmp(line, "WAKE", 4) == 0) {
    AnimState na = {};
    na.maxBri = 200; na.r = 255; na.g = 255; na.b = 255;
    const char* p = line + 4;
    if (*p == ',') {
      p++;
      if (strncmp(p, "RAINBOW", 7) == 0) {
        na.rainbow = true; p += 7;
        int v; if (*p == ',' && sscanf(p + 1, "%d", &v) == 1) na.maxBri = (uint8_t)constrain(v, 0, 255);
      } else {
        int tr, tg, tb, tbri;
        int n = sscanf(p, "%d,%d,%d,%d", &tr, &tg, &tb, &tbri);
        if (n < 3) { Serial.print("ERR format: WAKE[,R,G,B[,BRI]|RAINBOW[,BRI]]\r\n"); return; }
        na.r = (uint8_t)constrain(tr,0,255); na.g = (uint8_t)constrain(tg,0,255); na.b = (uint8_t)constrain(tb,0,255);
        if (n >= 4) na.maxBri = (uint8_t)constrain(tbri, 0, 255);
      }
    }
    clearActiveLeds(); na.mode = ANIM_WAKE; g_anim = na;
    Serial.printf("OK WAKE %s bri=%d\r\n", g_anim.rainbow ? "RAINBOW" : "COLOR", g_anim.maxBri);

  } else if (strncmp(line, "SPIN", 4) == 0) {
    AnimState na = {};
    na.maxBri = 200; na.r = 255; na.g = 255; na.b = 255;
    int8_t outerDir = 1, innerDir = 1;
    const char* p = line + 4;
    if (*p == ',') {
      p++;
      if (strncmp(p, "RAINBOW", 7) == 0) {
        na.rainbow = true; p += 7;
        int od = 0, id = 0, tbri = 200;
        int n = sscanf(p, ",%d,%d,%d", &od, &id, &tbri);
        if (n >= 1) outerDir = (int8_t)((od == 0) ? 1 : -1);
        if (n >= 2) innerDir = (int8_t)((id == 0) ? 1 : -1);
        if (n >= 3) na.maxBri = (uint8_t)constrain(tbri, 0, 255);
      } else {
        int tr, tg, tb, tod = 0, tid = 0, tbri = 200;
        int n = sscanf(p, "%d,%d,%d,%d,%d,%d", &tr, &tg, &tb, &tod, &tid, &tbri);
        if (n < 3) { Serial.print("ERR format: SPIN[,R,G,B[,ODIR,IDIR[,BRI]]|RAINBOW[,ODIR,IDIR[,BRI]]]\r\n"); return; }
        na.r = (uint8_t)constrain(tr,0,255); na.g = (uint8_t)constrain(tg,0,255); na.b = (uint8_t)constrain(tb,0,255);
        if (n >= 4) outerDir = (int8_t)((tod == 0) ? 1 : -1);
        if (n >= 5) innerDir = (int8_t)((tid == 0) ? 1 : -1);
        if (n >= 6) na.maxBri = (uint8_t)constrain(tbri, 0, 255);
      }
    }
    // ODIR 作用于外侧环(0,2)，IDIR 作用于内侧环(1,3)，形成对转效果
    for (int ring = 0; ring < N_RINGS; ring++) {
      na.ringDir[ring] = (ring % 2 == 0) ? outerDir : innerDir;
      na.ringPos[ring] = 0.0f;
    }
    clearActiveLeds(); na.mode = ANIM_SPIN; g_anim = na;
    Serial.printf("OK SPIN %s outer=%s inner=%s bri=%d\r\n",
      g_anim.rainbow ? "RAINBOW" : "COLOR",
      outerDir > 0 ? "CW" : "CCW",
      innerDir > 0 ? "CW" : "CCW",
      g_anim.maxBri);

  } else if (strncmp(line, "THINK", 5) == 0) {
    AnimState na = {};
    na.maxBri = 150;                 // 默认亮度，具体明暗由思考算法内部动态计算
    const char* p = line + 5;
    if (*p == ',') {
      int v; if (sscanf(p + 1, "%d", &v) == 1) na.maxBri = (uint8_t)constrain(v, 0, 255);
    }
    na.siriTime = 0.0f;
    clearActiveLeds(); na.mode = ANIM_SIRI; g_anim = na;
    Serial.printf("OK THINK bri=%d\r\n", g_anim.maxBri);

  } else if (strcmp(line, "STOP") == 0) {
    if (g_anim.mode != ANIM_NONE) g_animSaved = g_anim;
    g_anim.mode = ANIM_NONE;
    Serial.print("OK STOP\r\n");

  } else if (strcmp(line, "RESUME") == 0) {
    if (g_animSaved.mode == ANIM_NONE) {
      Serial.print("ERR no animation to resume\r\n");
    } else {
      clearActiveLeds();
      g_anim = g_animSaved;
      Serial.printf("OK RESUME %s\r\n",
        g_anim.mode == ANIM_BREATHE ? "BREATHE" :
        g_anim.mode == ANIM_WAKE    ? "WAKE"    :
        g_anim.mode == ANIM_SPIN    ? "SPIN"    : "THINK");
    }

  } else if (strcmp(line, "HELP") == 0) {
    Serial.print("=== LED Commands ===\r\n");
    Serial.print("ALL,R,G,B,BRI                       set all 108 LEDs\r\n");
    Serial.print("ONE,grp,idx,R,G,B,BRI               set one LED (grp 0-3)\r\n");
    Serial.print("BRI,val                             set brightness (0-255)\r\n");
    Serial.print("OFF                                 turn off all LEDs\r\n");
    Serial.print("RAINBOW[,BRI]                       static rainbow (default BRI=200)\r\n");
    Serial.print("BREATHE[,R,G,B[,BRI]|RAINBOW[,BRI]] breathing effect\r\n");
    Serial.print("WAKE[,R,G,B[,BRI]|RAINBOW[,BRI]]    wake-up: inner->outer fade-in\r\n");
    Serial.print("SPIN[,R,G,B[,ODIR,IDIR[,BRI]]|RAINBOW[,ODIR,IDIR[,BRI]]]\r\n");
    Serial.print("                                     spinning comet (ODIR/IDIR: 0=CW, 1=CCW)\r\n");
    Serial.print("                                     outer rings 0/2, inner rings 1/3\r\n");
    Serial.print("THINK[,BRI]                         fluid thinking effect (default BRI=150)\r\n");
    Serial.print("STOP                                stop animation (state saved)\r\n");
    Serial.print("RESUME                              resume last stopped animation\r\n");
    Serial.print("HELP                                show this help\r\n");

  } else {
    Serial.printf("ERR unknown: %s\r\n", line);
  }
}
