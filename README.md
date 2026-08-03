# Sony ST-SA3ES Internet Streamer Conversion

## Design Proposal v1.0

### Project Objective

The goal of this project is to convert a Sony ST-SA3ES ES-series tuner into a modern Internet radio and music streamer while preserving:

- Original Sony ES enclosure
- Original VFD display (FL701)
- Original front-panel buttons
- Original rotary tuning encoder
- Original power supply
- Original aesthetics and user experience

The original FM/AM tuner circuitry will be bypassed and replaced by an ESP32-based controller connected to Volumio running on a Raspberry Pi.

The philosophy is simple:

> Keep everything the user touches. Replace everything related to receiving radio broadcasts.

---

# High-Level Architecture

```text
                    Raspberry Pi
                       Volumio
                          │
                    MQTT / Serial
                          │
                          ▼
                     ESP32-S3
                          │
          ┌───────────────┼───────────────┐
          │               │               │
          ▼               ▼               ▼

      Keypad        Rotary Encoder     VFD Driver
                                          │
                                          ▼
                                    FL701 Display
```

The ESP32 becomes the front-panel controller.

## Responsibilities

| Function | Handled By |
|-----------|-----------|
| Streaming | Volumio |
| Audio output | Volumio |
| Front-panel buttons | ESP32 |
| Rotary encoder | ESP32 |
| Display updates | ESP32 |
| Menu navigation | ESP32 |
| MQTT / Serial bridge | ESP32 |

---

# Existing Display Architecture

The ST-SA3ES uses a multiplexed Vacuum Fluorescent Display (VFD), designated FL701.

The display consists of:

```text
16 Grid Outputs
37 Segment Outputs
```

Total:

```text
53 outputs
```

The original display is driven directly by the NEC μPD780205 controller (IC701).

Important observations:

```text
IC701 Pin 79
VLOAD = -30V
```

The display board already receives:

```text
+5V
-30V
GND
```

through connector CNP701.

Therefore the conversion can likely reuse the existing Sony display power supply without generating additional high-voltage rails.

---

# Display Replacement Strategy

## Original Sony Design

```text
IC701
 ├── G1..G16
 └── P1..P37

      │
      ▼

    FL701
```

The original μPD780205 integrates:

- Main control processor
- VFD display driver

Because of this, there is no external display driver IC available for reuse.

---

## Proposed New Design

```text
ESP32
   │
   SPI
   │
   ▼

HV Driver Chain

   │
   ├── G1..G16
   └── P1..P37

   ▼

 FL701
```

The ESP32 maintains a framebuffer and continuously scans the display.

Benefits:

- No need to emulate Sony firmware
- Complete control over display contents
- Menu system can be redesigned
- Track information can be displayed
- Artist information can be displayed
- Station names can be displayed
- Scrolling text becomes possible
- Future features remain open

---

# Why Not Reuse The Original Display Driver?

The display driver functionality is integrated into IC701.

Replacing IC701 entirely would require emulating:

- System controller
- Tuner control
- Button decoding
- Encoder decoding
- Display refresh

A cleaner approach is:

```text
Leave FL701 in place

Replace display control logic
with a dedicated ESP32 + HV driver
```

The ESP32 will directly control:

```text
G1..G16
P1..P37
```

through a modern VFD driver stage.

---

# Front Panel Buttons

Sony implemented the front panel using two resistor ladders.

## Key Ladder 1

```text
SHIFT
1
2
3
4
5
6
7
8
9
0
ENTER
```

## Key Ladder 2

```text
DISPLAY
TA
NEWS/INFO
PTY
ANTENNA
FM MODE
BAND
MEMORY
CHARACTER
MENU
RETURN
TUNE MODE
```

Each ladder terminates at a jumper on the display board.

```text
JW719
JW720
```

---

# Proposed Keypad Connection

Remove:

```text
JW719
JW720
```

These become the interface points to the ESP32.

```text
JW719 → ESP32 ADC1

JW720 → ESP32 ADC2

GND   → ESP32 GND
```

The original Sony resistor values are retained.

Benefits:

- No PCB trace cutting
- Original switches remain untouched
- Fully reversible modification
- Minimal soldering

Optional filtering:

```text
ADC
 │
100nF
 │
GND
```

for stable readings.

---

# Rotary Encoder

The ST-SA3ES uses a rotary encoder labelled:

```text
RV701
```

with outputs:

```text
R1
R2
```

These connect directly to IC701.

The encoder is expected to be a standard quadrature device.

## Proposed Wiring

```text
Encoder A → ESP32 GPIO

Encoder B → ESP32 GPIO

GND → ESP32 GND
```

No active components are expected to be required.

---

# User Interface Proposal

The front panel already contains sufficient controls to operate Volumio without a smartphone.

---

## Playback Mode

```text
Encoder CW
    Next Track

Encoder CCW
    Previous Track

ENTER
    Play / Pause

1-9
    Favourite Stations

DISPLAY
    Change Display Screen
```

---

## Menu Mode

```text
MENU
    Enter Menu

Encoder
    Navigate

ENTER
    Select

RETURN
    Back
```

This preserves the original Sony navigation concept:

```text
Turn = Navigate
Enter = Select
Return = Back
```

---

# Physical Integration Points

The conversion intentionally minimizes modifications to the tuner.

---

## Display Power

Reuse the existing Sony display supply from:

```text
CNP701
```

Signals available:

```text
+5V
-30V
GND
```

---

## Keypad

Use:

```text
JW719
JW720
```

as ADC inputs.

---

## Rotary Encoder

Connect at:

```text
RV701
```

or directly on the IC701 input traces.

---

## Display

Retain:

```text
FL701
```

No mechanical changes required.

---

# Software Architecture

## ESP32 Responsibilities

```text
Read Keypad

Read Encoder

Receive Display Updates

Maintain Framebuffer

Refresh VFD

Communicate With Volumio
```

---

## Volumio Responsibilities

```text
Internet Radio

Music Playback

Spotify

Tidal

Library Browsing

Playlists

Audio Output
```

---

# Long-Term Goal

The final unit should remain visually indistinguishable from a stock ST-SA3ES while functioning as a fully modern network streamer.

Retained:

- Sony ES chassis
- Sony ES front panel
- Original buttons
- Original encoder
- Original VFD display
- Original power supply

Added:

- Internet radio
- Spotify
- Tidal
- Network audio
- Metadata display
- Favourite presets
- Playlists
- MQTT integration
- Future firmware extensibility

The end result is a modern streamer that still feels and operates like an ES-series Sony component.
