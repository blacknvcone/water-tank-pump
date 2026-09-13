# Water Tank Controller — Wiring Diagram (v2.2.0)

## DS1302N RTC Module (5-pin) → NodeMCU

```
  DS1302N Module                NodeMCU ESP8266
  ┌─────────────┐
  │  ┌───────┐  │
  │  │DS1302N│  │
  │  └───────┘  │
  │ [CR2032]    │
  ├─────────────┤
  │ VCC  ●──────┼──────→ 3.3V (or VIN 5V)
  │ GND  ●──────┼──────→ GND
  │ CLK  ●──────┼──────→ D6 (GPIO12)
  │ DAT  ●──────┼──────→ D7 (GPIO13)
  │ RST  ●──────┼──────→ D3 (GPIO0)
  └─────────────┘
```

The CR2032 battery holder is soldered directly on the PCB — no external battery wiring needed.

## Pin Mapping Summary

```
  NodeMCU Pin    GPIO    Direction   Connected To
  ───────────    ────    ─────────   ────────────
  D1             5       INPUT       High Water Level Sensor
  D2             4       INPUT       Low Water Level Sensor
  D3             0       OUTPUT      DS1302 RST (CE)
  D4             2       OUTPUT      Built-in LED (active low)
  D5             14      OUTPUT      Relay IN (pump control)
  D6             12      OUTPUT      DS1302 CLK
  D7             13      BIDIR       DS1302 DAT
```

## Full System Wiring

```
                        ┌──────────────────────┐
                        │    NodeMCU ESP8266    │
                        │                       │
    ┌───────────┐       │   D1 (GPIO5)  ●──────┤────── Low Water Sensor
    │  Low Water│───────│   D2 (GPIO4)  ●      │       (float switch, 1 leg)
    │  Sensor   │  GND  │   D3 (GPIO0)  ●──────┤────── DS1302 RST/CE
    └───────────┘───────│   D4 (GPIO2)  ●      │       Built-in LED
                        │   D5 (GPIO14) ●──────┤────── Relay IN
    ┌───────────┐       │   D6 (GPIO12) ●──────┤────── DS1302 CLK
    │ High Water│───────│   D7 (GPIO13) ●──────┤────── DS1302 DAT
    │  Sensor   │  GND  │                       │
    └───────────┘───────│   3.3V        ●──────┤────── DS1302 VCC
                        │   VIN (5V)    ●──────┤────── Relay VCC
                        │   GND         ●──────┤────── Common GND
                        └──────────────────────┘

    ┌───────────┐       ┌──────────────────────┐
    │  Relay    │       │   DS1302N RTC Module  │
    │  Module   │       │                       │
    │  VCC  ●───┤←──────│   VIN 5V              │
    │  GND  ●───┤←──────│   GND                 │
    │  IN   ●───┤←──────│   D5 (GPIO14)         │
    │           │       │   D6 (GPIO12) → CLK   │
    │  NO   ●───┤──────→│   D7 (GPIO13) → DAT  │
    │  COM ●───┤──────→│   D3 (GPIO0)  → RST   │
    │  NC      │       │                        │
    └─────┬─────┘       │   [CR2032 Battery]    │
          │             └──────────────────────┘
    ┌─────┴─────┐
    │   Water   │
    │   Pump    │
    └───────────┘
```

## Sensor Wiring Detail

Both water level sensors (float switches) use a simple 2-wire connection:

```
    Sensor          NodeMCU
    ──────          ───────
    Pin 1  ●────────● D2 (GPIO4)   [Low sensor]
              │
            10kΩ pull-down to GND (optional, for noise immunity)
              │
    Pin 2  ●────────● GND

    Same pattern for high sensor on D1 (GPIO5).
```

The sensors are wired as **normally-open contacts**:
- Water present  → switch closes → GPIO reads HIGH
- Water absent   → switch opens  → GPIO reads LOW (pulled down)
