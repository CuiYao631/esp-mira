# esp-mira — 圆形 LED 灯环 · 串口控制指令文档

## 硬件配置

| 参数 | 值 |
|------|----|
| 控制器 | ESP32 |
| LED 类型 | WS2812 NeoPixel（NEO_GRB, 800KHz） |
| 串口 | USB Serial，115200 baud，文本格式，每条指令以 `\n` 结尾 |

### 4 路独立灯环

| 环编号 (grp) | 引脚 | 灯珠数 | 位置 |
|:---:|:---:|:---:|------|
| 0 | GPIO 3 | 36 | 外圈 (ring 1) |
| 1 | GPIO 6 | 30 | 中外圈 (ring 2) |
| 2 | GPIO 7 | 24 | 中内圈 (ring 3) |
| 3 | GPIO 10 | 18 | 内圈 (ring 4) |
| **合计** | | **108** | |

### 触摸

| 参数 | 值 |
|------|----|
| 芯片 | BS8112A（I2C） |
| I2C 引脚 | SDA=GPIO 4, SCL=GPIO 5 |
| 使用按键 | KEY 10 |
| 事件 | PRESS / HOLD（~500ms 间隔）/ RELEASE |

---

## 指令速查

| 指令 | 格式 | 说明 |
|------|------|------|
| ALL | `ALL,R,G,B,BRI` | 全部 108 颗设为同一颜色 |
| ONE | `ONE,grp,idx,R,G,B,BRI` | 点亮单颗，其余熄灭 |
| BRI | `BRI,val` | 整体调亮度，颜色不变 |
| OFF | `OFF` | 熄灭全部 |
| RAINBOW | `RAINBOW[,BRI]` | 静态彩虹渐变（默认亮度 200） |
| BREATHE | `BREATHE[,R,G,B[,BRI]\|RAINBOW[,BRI]]` | 呼吸灯（循环淡入淡出） |
| WAKE | `WAKE[,R,G,B[,BRI]\|RAINBOW[,BRI]]` | 唤醒：内圈→外圈依次淡入 |
| SPIN | `SPIN[,R,G,B[,ODIR,IDIR[,BRI]]\|RAINBOW[,ODIR,IDIR[,BRI]]]` | 旋转彗星流光（含泛光） |
| THINK | `THINK[,BRI]` | 思考流体特效（洋红↔蓝↔青） |
| STOP | `STOP` | 暂停动画（自动保存状态） |
| RESUME | `RESUME` | 从上次 STOP 恢复动画 |
| HELP | `HELP` | 串口打印指令列表 |

> **通用规则**：`R/G/B` 范围 0\~255；`BRI` 亮度 0\~255，默认 200；`RAINBOW` 替代固定颜色时按环内位置分配色相。

---

## 指令详情

### ALL / ONE / BRI / OFF

```
ALL,255,0,0,200          → 全部红色，亮度 200
ONE,0,12,0,255,0,200     → 外圈第 13 颗绿色，其余熄灭
ONE,3,0,0,0,255,200      → 内圈第 1 颗蓝色，其余熄灭
BRI,128                  → 当前颜色半亮度
OFF                      → 全部熄灭
```

### RAINBOW

```
RAINBOW          → 彩虹，亮度 200
RAINBOW,150      → 彩虹，亮度 150
```

---

## 预制灯光效果

### BREATHE — 呼吸灯

全部 108 颗 LED 循环淡入淡出（510 步三角波周期）。

```
BREATHE                  → 白色呼吸
BREATHE,0,0,255,150      → 蓝色呼吸，最大亮度 150
BREATHE,RAINBOW          → 彩虹呼吸
```

### WAKE — 唤醒动画

从内圈到外圈依次淡入：ring 4 → ring 3 → ring 2 → ring 1，每环约 1.6s，结束后保持全亮。

```
WAKE                 → 白色唤醒
WAKE,0,200,255       → 青色唤醒
WAKE,RAINBOW,150     → 彩虹唤醒，亮度 150
```

### SPIN — 旋转彗星流光

4 环各一道彗星持续旋转，头部最亮、拖尾渐暗，首尾无缝衔接。**含泛光优化**：彗星头部向周围灯珠扩散光晕，全局相邻灯珠颜色混合产生柔光效果。

`ODIR` 作用于外圈组（ring 0, 2），`IDIR` 作用于内圈组（ring 1, 3），可实现内外对转。`0`=顺时针（默认），`1`=逆时针。

```
SPIN                      → 白色同向旋转
SPIN,255,0,0,0,1          → 红色，外顺内逆
SPIN,RAINBOW,0,1,180      → 彩虹，外顺内逆，亮度 180
```

### THINK — 思考流体特效

双正弦波叠加驱动色相在洋红→蓝→青之间流动，亮度/饱和度随波形动态变化，营造有机的流体呼吸感。4 环独立计算，可通过 `RING_ANGLE_OFFSET` 微调各环物理角度对齐。

```
THINK           → 流体特效，默认亮度 150
THINK,200       → 流体特效，亮度 200
```

### STOP / RESUME

`STOP` 暂停动画并保存当前状态；`RESUME` 从该状态继续。
使用 ALL / ONE / BRI / OFF / RAINBOW 也会停止动画，但**不**保存状态。

---

## 触摸事件

BS8112A 芯片仅监控 KEY 10，串口自动上报：

| 事件 | 响应 | 说明 |
|------|------|------|
| 按下 | `TOUCH,PRESS` | 触摸开始 |
| 按住 | `TOUCH,HOLD` | 持续按住（约每 500ms 一次） |
| 松开 | `TOUCH,RELEASE` | 触摸结束 |

---

## 响应格式

### 成功响应

| 命令 | 响应示例 |
|------|------|
| ALL | `OK ALL 255,0,0,200` |
| ONE | `OK ONE grp=0 idx=12 0,255,0,200` |
| BRI | `OK BRI 128` |
| OFF | `OK OFF` |
| RAINBOW | `OK RAINBOW bri=200` |
| BREATHE | `OK BREATHE COLOR bri=200` 或 `OK BREATHE RAINBOW bri=150` |
| WAKE | `OK WAKE COLOR bri=200` 或 `OK WAKE RAINBOW bri=150` |
| SPIN | `OK SPIN COLOR outer=CW inner=CCW bri=200` 或 `OK SPIN RAINBOW outer=CW inner=CCW bri=180` |
| THINK | `OK THINK bri=150` |
| STOP | `OK STOP` |
| RESUME | `OK RESUME BREATHE` / `WAKE` / `SPIN` / `THINK` |

### 错误响应

| 响应 | 触发条件 |
|------|------|
| `ERR format: ALL,R,G,B,BRI` | ALL 参数不足 |
| `ERR format: ONE,grp,idx,R,G,B,BRI` | ONE 参数不足 |
| `ERR bad index` | ONE 的 grp/idx 超出范围 |
| `ERR format: BRI,0-255` | BRI 参数缺失 |
| `ERR format: BREATHE[,R,G,B[,BRI]\|RAINBOW[,BRI]]` | BREATHE 参数不足 |
| `ERR format: WAKE[,R,G,B[,BRI]\|RAINBOW[,BRI]]` | WAKE 参数不足 |
| `ERR format: SPIN[,R,G,B[,ODIR,IDIR[,BRI]]\|RAINBOW[,ODIR,IDIR[,BRI]]]` | SPIN 参数不足 |
| `ERR no animation to resume` | RESUME 时无已保存的动画状态 |
| `ERR unknown: <cmd>` | 无法识别的命令 |

---

## 代码结构

```
esp-mira/
├── esp-mira.ino    # 主程序：setup/loop、串口行缓冲
├── led.h           # LED 模块公开 API
├── led.cpp         # LED 模块：4 路 WS2812、全部动画、命令解析
├── touch.h         # 触摸模块公开 API
├── touch.cpp       # 触摸模块：BS8112A I2C 驱动、按键状态机
└── README.md
```

### 可调参数（`led.cpp` 顶部 `#define`）

| 参数 | 默认值 | 说明 |
|------|:---:|------|
| `SPIN_SPEED_F` | 0.20 | 旋转速度（每 tick 位移） |
| `SPIN_TAIL_POWER` | 0.55 | 拖尾衰减指数（越小拖尾越长，0.3\~0.8） |
| `SPIN_HEAD_BOOST` | 1.35 | 彗星头部亮度增益（1.0\~2.0） |
| `SPIN_GLOW_RANGE` | 3 | 头部泛光半径（颗，1\~5） |
| `SPIN_GLOW_FALLOFF` | 0.25 | 泛光衰减系数（越小扩散越远） |
| `GLOBAL_GLOW_SELF` | 0.70 | 全局泛光自身保留比例 |
| `GLOBAL_GLOW_NEIGHBOR` | 0.15 | 全局泛光邻居混入比例 |
| `SIRI_SPEED_F` | 0.08 | THINK 流体动画速度 |
| `RING_ANGLE_OFFSET[4]` | 0,0,0,0 | 各环物理角度对齐偏置（度） |
