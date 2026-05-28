// led.ino - LED 控制、动画和命令处理

// ---------- LED 基础操作 ----------
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

// ---------- 动画帧函数 ----------
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
  if (g_anim.wkStep >= 2) { g_anim.mode = ANIM_NONE; return; }

  bool isInner  = (g_anim.wkStep == 0);
  int  start    = isInner ? OUTER_RING : 0;
  int  count    = isInner ? INNER_RING : OUTER_RING;
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

// ---------- LED 初始化（在 setup 中调用）----------
void setupLED() {
  strip.begin();
  strip.setBrightness(255);
  strip.show();
  for (int i = 0; i < NUM_LEDS; i++) {
    g_ledStage[i]  = {0, 0, 0, 0};
    g_ledActive[i] = {0, 0, 0, 0};
  }
  renderActiveLeds();
}

// ---------- LED 命令处理 ----------
void processLedCommand(const char* line) {
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
      if (led < 0) { Serial.print("ERR bad index\r\n"); return; }
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
      for (int i = 0; i < NUM_LEDS; i++) g_ledStage[i].bri = (uint8_t)val;
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
      uint32_t c = strip.gamma32(strip.ColorHSV(hue, 255, 255));
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
    Serial.print("ALL,R,G,B,BRI                                  - 设置全部 40 颗 LED\r\n");
    Serial.print("ONE,grp,idx,R,G,B,BRI                          - 设置单颗 LED\r\n");
    Serial.print("BRI,val                                        - 设置亮度\r\n");
    Serial.print("OFF                                            - 关闭所有 LED\r\n");
    Serial.print("THR,val                                        - 设置触摸阈值 (1-2000)\r\n");
    Serial.print("RAINBOW[,BRI]                                  - 彩虹渐变\r\n");
    Serial.print("BREATHE[,R,G,B[,BRI]|RAINBOW[,BRI]]           - 呼吸效果\r\n");
    Serial.print("WAKE[,R,G,B[,BRI]|RAINBOW[,BRI]]              - 唤醒动画（内环→外环）\r\n");
    Serial.print("SPIN[,R,G,B[,ODIR,IDIR[,BRI]]|RAINBOW[,...]]  - 旋转彗星\r\n");
    Serial.print("  ODIR/IDIR: 0=CW（默认）, 1=CCW\r\n");
    Serial.print("STOP                                           - 停止动画（状态已保存）\r\n");
    Serial.print("RESUME                                         - 恢复上次动画\r\n");
    Serial.print("HELP                                           - 显示此帮助\r\n");

  } else {
    Serial.printf("ERR unknown: %s\r\n", line);
  }
}
