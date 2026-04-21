#include <Adafruit_NeoPixel.h>
#include <string.h>

#define LED_PIN    15
#define TOUCH_PIN  14

#define TOUCH_THRESHOLD  32

#define OUTER_RING 24    
#define INNER_RING 16    
#define NUM_LEDS   (OUTER_RING + INNER_RING) 

#define BREATHE_SPEED      2        // phase steps per tick (510-phase cycle)
#define WAKE_FADE_TICKS    80       // ticks per ring fade-in (~1.6s per ring)
#define SPIN_SPEED_F       0.20f    // ring positions per tick
#define SPIN_TAIL_LEN      8        // comet tail length (LEDs) — kept for reference

Adafruit_NeoPixel strip(NUM_LEDS, LED_PIN, NEO_GRB + NEO_KHZ800);

static const uint8_t GROUP_OUTER = 0;
static const uint8_t GROUP_INNER = 1;
static const uint8_t TOUCH_EVT_PRESS   = 1;
static const uint8_t TOUCH_EVT_RELEASE = 2;
static const uint8_t TOUCH_EVT_HOLD    = 3;

struct LedPixel {
  uint8_t r;
  uint8_t g;
  uint8_t b;
  uint8_t bri;
};

enum AnimMode { ANIM_NONE = 0, ANIM_BREATHE, ANIM_WAKE, ANIM_SPIN };

struct AnimState {
  AnimMode mode;
  bool     rainbow;
  uint8_t  r, g, b, maxBri;
  int      brPhase;    // BREATHE: 0..509
  int      wkStep;     // WAKE: LED order index 0..NUM_LEDS
  int      wkTimer;    // WAKE: ticks since last LED lit
  float    outerPos;   // SPIN: outer ring head (fractional)
  float    innerPos;   // SPIN: inner ring head (fractional)
  int8_t   outerDir;   // SPIN: +1=CW, -1=CCW
  int8_t   innerDir;
};

HardwareSerial SerialData(2);

uint16_t g_touchThr = TOUCH_THRESHOLD;
LedPixel g_ledStage[NUM_LEDS];
LedPixel g_ledActive[NUM_LEDS];
bool      g_ledDirty = true;
AnimState g_anim     = {};
AnimState g_animSaved = {};  // last active anim, restored by RESUME

void processSerialDataInput();
int  mapGroupIndexToLed(int group, int idx);
void copyStageToActive();
void clearActiveLeds();
void renderActiveLeds();
void sendTouchEvent(uint8_t type, uint16_t raw);
void processDebugText(const char* line);
void tickAnimation();

volatile bool     g_touchIRQ    = false;
static void IRAM_ATTR onTouchISR(void* arg) {
  g_touchIRQ = true;
  (void)arg;
}

void setup() {
  Serial.begin(115200);
  SerialData.begin(115200, SERIAL_8N1, 16, 17);
  strip.begin();
  strip.setBrightness(255);
  strip.show();
  for (int i = 0; i < NUM_LEDS; i++) {
    g_ledStage[i] = {0, 0, 0, 0};
    g_ledActive[i] = {0, 0, 0, 0};
  }
  touchAttachInterruptArg(TOUCH_PIN, onTouchISR, nullptr, g_touchThr);
  renderActiveLeds();
  Serial.println("Ready.");
  Serial.println("Commands:");
  Serial.println("  ALL,R,G,B,BRI   - set all 40 LEDs");
  Serial.println("  ONE,grp,idx,R,G,B,BRI - set one LED");
  Serial.println("  BRI,val         - brightness");
  Serial.println("  OFF             - turn off all LEDs");
  Serial.println("  RAINBOW[,BRI]   - rainbow gradient");
  Serial.println("  BREATHE[,R,G,B[,BRI]|RAINBOW[,BRI]] - breathing");
  Serial.println("  WAKE[,R,G,B[,BRI]|RAINBOW[,BRI]]    - wake-up anim");
  Serial.println("  SPIN[,R,G,B[,ODIR,IDIR[,BRI]]|RAINBOW[,...]] - comet");
  Serial.println("  STOP            - stop animation (state saved)");
  Serial.println("  RESUME          - resume last stopped animation");
  Serial.println("  HELP            - show this help");
}

void loop() {
  processSerialDataInput();
  tickAnimation();
  static bool lastTouched = false;
  static unsigned long lastHoldReport = 0;

  uint16_t touchVal = (uint16_t)touchRead(TOUCH_PIN);
  bool isTouched = (touchVal < g_touchThr) || g_touchIRQ;
  g_touchIRQ = false;

  if (isTouched && !lastTouched) sendTouchEvent(TOUCH_EVT_PRESS, touchVal);
  if (!isTouched && lastTouched) sendTouchEvent(TOUCH_EVT_RELEASE, touchVal);
  if (isTouched && millis() - lastHoldReport >= 500) {
    lastHoldReport = millis();
    sendTouchEvent(TOUCH_EVT_HOLD, touchVal);
  }
  lastTouched = isTouched;

  if (g_ledDirty) {
    renderActiveLeds();
    g_ledDirty = false;
  }

  delay(20);
}

static char g_usbTextBuf[128];
static uint8_t g_usbTextLen = 0;
static char g_u2TextBuf[128];
static uint8_t g_u2TextLen = 0;

void processSerialDataInput() {
  while (SerialData.available()) {
    uint8_t b = (uint8_t)SerialData.read();
    if (b == '\n') {
      g_u2TextBuf[g_u2TextLen] = '\0';
      if (g_u2TextLen > 0 && g_u2TextBuf[g_u2TextLen - 1] == '\r')
        g_u2TextBuf[--g_u2TextLen] = '\0';
      if (g_u2TextLen > 0) processDebugText(g_u2TextBuf);
      g_u2TextLen = 0;
    } else if (b != '\r' && b >= 0x20) {
      if (g_u2TextLen < sizeof(g_u2TextBuf) - 1)
        g_u2TextBuf[g_u2TextLen++] = (char)b;
    }
  }
  while (Serial.available()) {
    uint8_t b = (uint8_t)Serial.read();
    if (b == '\n') {
      g_usbTextBuf[g_usbTextLen] = '\0';
      if (g_usbTextLen > 0 && g_usbTextBuf[g_usbTextLen - 1] == '\r')
        g_usbTextBuf[--g_usbTextLen] = '\0';
      if (g_usbTextLen > 0) processDebugText(g_usbTextBuf);
      g_usbTextLen = 0;
    } else if (b != '\r' && b >= 0x20) {
      if (g_usbTextLen < sizeof(g_usbTextBuf) - 1)
        g_usbTextBuf[g_usbTextLen++] = (char)b;
    }
  }
}

int mapGroupIndexToLed(int group, int idx) {
  if (group == GROUP_OUTER) {
    if (idx < 0 || idx >= OUTER_RING) return -1;
    return idx;
  }
  if (group == GROUP_INNER) {
    if (idx < 0 || idx >= INNER_RING) return -1;
    return OUTER_RING + idx;
  }
  return -1;
}

void copyStageToActive() {
  memcpy(g_ledActive, g_ledStage, sizeof(g_ledActive));
}

void clearActiveLeds() {
  for (int i = 0; i < NUM_LEDS; i++) g_ledActive[i] = {0, 0, 0, 0};
}

void renderActiveLeds() {
  for (int i = 0; i < NUM_LEDS; i++) {
    uint8_t r = (uint8_t)(((uint16_t)g_ledActive[i].r * g_ledActive[i].bri) / 255);
    uint8_t g = (uint8_t)(((uint16_t)g_ledActive[i].g * g_ledActive[i].bri) / 255);
    uint8_t b = (uint8_t)(((uint16_t)g_ledActive[i].b * g_ledActive[i].bri) / 255);
    strip.setPixelColor(i, r, g, b);
  }
  strip.show();
}

void sendTouchEvent(uint8_t type, uint16_t raw) {
  const char* evtName;
  if (type == TOUCH_EVT_PRESS)        evtName = "PRESS";
  else if (type == TOUCH_EVT_RELEASE) evtName = "RELEASE";
  else                                evtName = "HOLD";

  char textBuf[32];
  snprintf(textBuf, sizeof(textBuf), "TOUCH,%s,%u\r\n", evtName, raw);
  Serial.print(textBuf);
  SerialData.print(textBuf);
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
  // wkStep: 0=inner ring fading in, 1=outer ring fading in, 2=done
  if (g_anim.wkStep >= 2) { g_anim.mode = ANIM_NONE; return; }

  bool isInner = (g_anim.wkStep == 0);
  int  start   = isInner ? OUTER_RING : 0;
  int  count   = isInner ? INNER_RING : OUTER_RING;
  int  ringSize = count;

  uint8_t bri = (g_anim.wkTimer >= WAKE_FADE_TICKS)
                ? g_anim.maxBri
                : (uint8_t)((uint16_t)g_anim.wkTimer * g_anim.maxBri / WAKE_FADE_TICKS);

  for (int i = 0; i < count; i++) {
    int led = start + i;
    if (g_anim.rainbow) {
      uint16_t hue = (uint16_t)((uint32_t)i * 65536 / ringSize);
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
  g_anim.outerPos += g_anim.outerDir * SPIN_SPEED_F;
  if (g_anim.outerPos <  0)          g_anim.outerPos += OUTER_RING;
  if (g_anim.outerPos >= OUTER_RING) g_anim.outerPos -= OUTER_RING;

  g_anim.innerPos += g_anim.innerDir * SPIN_SPEED_F;
  if (g_anim.innerPos <  0)          g_anim.innerPos += INNER_RING;
  if (g_anim.innerPos >= INNER_RING) g_anim.innerPos -= INNER_RING;

  const float fO = (float)OUTER_RING;
  const float fI = (float)INNER_RING;

  for (int i = 0; i < OUTER_RING; i++) {
    float dist = (g_anim.outerDir > 0)
                 ? fmodf(g_anim.outerPos - (float)i + fO, fO)
                 : fmodf((float)i - g_anim.outerPos + fO, fO);
    float t = (fO - 1.0f - dist) / (fO - 1.0f);
    if (t < 0.0f) t = 0.0f;
    uint8_t bri = (uint8_t)((float)g_anim.maxBri * t * t + 0.5f);
    if (bri == 0) bri = 1;
    if (g_anim.rainbow) {
      uint16_t hue = (uint16_t)((uint32_t)i * 65536 / OUTER_RING);
      uint32_t c = strip.gamma32(strip.ColorHSV(hue, 255, 255));
      g_ledActive[i] = { (uint8_t)((c >> 16) & 0xFF),
                         (uint8_t)((c >>  8) & 0xFF),
                         (uint8_t)( c        & 0xFF), bri };
    } else {
      g_ledActive[i] = { g_anim.r, g_anim.g, g_anim.b, bri };
    }
  }

  for (int i = 0; i < INNER_RING; i++) {
    float dist = (g_anim.innerDir > 0)
                 ? fmodf(g_anim.innerPos - (float)i + fI, fI)
                 : fmodf((float)i - g_anim.innerPos + fI, fI);
    float t = (fI - 1.0f - dist) / (fI - 1.0f);
    if (t < 0.0f) t = 0.0f;
    uint8_t bri = (uint8_t)((float)g_anim.maxBri * t * t + 0.5f);
    if (bri == 0) bri = 1;
    int led = OUTER_RING + i;
    if (g_anim.rainbow) {
      uint16_t hue = (uint16_t)((uint32_t)i * 65536 / INNER_RING);
      uint32_t c = strip.gamma32(strip.ColorHSV(hue, 255, 255));
      g_ledActive[led] = { (uint8_t)((c >> 16) & 0xFF),
                           (uint8_t)((c >>  8) & 0xFF),
                           (uint8_t)( c        & 0xFF), bri };
    } else {
      g_ledActive[led] = { g_anim.r, g_anim.g, g_anim.b, bri };
    }
  }
  g_ledDirty = true;
}

void tickAnimation() {
  switch (g_anim.mode) {
    case ANIM_BREATHE: animBreatheTick(); break;
    case ANIM_WAKE:    animWakeTick();    break;
    case ANIM_SPIN:    animSpinTick();    break;
    default: break;
  }
}

// ---------------------------------------------------------------------------
void processDebugText(const char* line) {
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

  } else if (strncmp(line, "THR,", 4) == 0) {
    if (sscanf(line, "THR,%d", &val) == 1) {
      g_touchThr = (uint16_t)constrain(val, 1, 2000);
      touchAttachInterruptArg(TOUCH_PIN, onTouchISR, nullptr, g_touchThr);
      Serial.printf("OK THR %d\r\n", g_touchThr);
    } else {
      Serial.print("ERR format: THR,1-2000\r\n");
    }

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
    na.outerDir = 1; na.innerDir = 1;
    const char* p = line + 4;
    if (*p == ',') {
      p++;
      if (strncmp(p, "RAINBOW", 7) == 0) {
        na.rainbow = true; p += 7;
        int od = 0, id = 0, tbri = 200;
        int n = sscanf(p, ",%d,%d,%d", &od, &id, &tbri);
        if (n >= 1) na.outerDir = (int8_t)((od == 0) ? 1 : -1);
        if (n >= 2) na.innerDir = (int8_t)((id == 0) ? 1 : -1);
        if (n >= 3) na.maxBri  = (uint8_t)constrain(tbri, 0, 255);
      } else {
        int tr, tg, tb, tod = 0, tid = 0, tbri = 200;
        int n = sscanf(p, "%d,%d,%d,%d,%d,%d", &tr, &tg, &tb, &tod, &tid, &tbri);
        if (n < 3) { Serial.print("ERR format: SPIN[,R,G,B[,ODIR,IDIR[,BRI]]|RAINBOW[,ODIR,IDIR[,BRI]]]\r\n"); return; }
        na.r = (uint8_t)constrain(tr,0,255); na.g = (uint8_t)constrain(tg,0,255); na.b = (uint8_t)constrain(tb,0,255);
        if (n >= 4) na.outerDir = (int8_t)((tod == 0) ? 1 : -1);
        if (n >= 5) na.innerDir = (int8_t)((tid == 0) ? 1 : -1);
        if (n >= 6) na.maxBri  = (uint8_t)constrain(tbri, 0, 255);
      }
    }
    clearActiveLeds(); na.mode = ANIM_SPIN; g_anim = na;
    Serial.printf("OK SPIN %s outer=%s inner=%s bri=%d\r\n",
      g_anim.rainbow ? "RAINBOW" : "COLOR",
      g_anim.outerDir > 0 ? "CW" : "CCW",
      g_anim.innerDir > 0 ? "CW" : "CCW",
      g_anim.maxBri);

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
        g_anim.mode == ANIM_WAKE    ? "WAKE"    : "SPIN");
    }

  } else if (strcmp(line, "HELP") == 0) {
    Serial.print("ALL,R,G,B,BRI            - set all 40 LEDs and apply\r\n");
    Serial.print("ONE,grp,idx,R,G,B,BRI   - set one LED and apply\r\n");
    Serial.print("BRI,val                 - set all LEDs brightness\r\n");
    Serial.print("OFF                     - turn off all LEDs\r\n");
    Serial.print("THR,val                 - set touch threshold (1-2000)\r\n");
    Serial.print("RAINBOW[,BRI]           - rainbow gradient (default BRI=200)\r\n");
    Serial.print("BREATHE[,R,G,B[,BRI]|RAINBOW[,BRI]] - breathing effect\r\n");
    Serial.print("WAKE[,R,G,B[,BRI]|RAINBOW[,BRI]]    - wake-up animation (inner->outer)\r\n");
    Serial.print("SPIN[,R,G,B[,ODIR,IDIR[,BRI]]|RAINBOW[,ODIR,IDIR[,BRI]]] - spinning comet\r\n");
    Serial.print("  ODIR/IDIR: 0=CW(default), 1=CCW  (outer/inner ring directions)\r\n");
    Serial.print("STOP                    - stop animation (state saved)\r\n");
    Serial.print("RESUME                  - resume last stopped animation\r\n");
    Serial.print("HELP                    - show this help\r\n");

  } else {
    Serial.printf("ERR unknown: %s\r\n", line);
  }
}