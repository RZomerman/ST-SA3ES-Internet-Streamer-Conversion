# Sony ST-SA3ES Internet Tuner Replacement Project

## Design Proposal v1.0

### Design Philosophy

This project is not intended to convert the Sony ST-SA3ES into a generic network streamer.

Instead, the objective is to preserve the original tuner experience while replacing the radio receiver with a modern Internet-radio capable platform.

From the user's perspective, the unit should continue to behave like a Sony ES tuner.

The tuning knob, frequency display, presets, memory functions and station browsing remain intact.

The only difference is that the received stations no longer originate from FM or AM broadcasts.

Instead, the "stations" are Internet radio streams managed by an ESP32.

The goal is that a user unfamiliar with the modification should still believe they are operating a traditional tuner.

---

# Project Objectives

Preserve:

- Original Sony ES chassis
- Original front panel
- Original FL701 VFD display
- Original keypad
- Original rotary tuning encoder
- Original power supply
- Original tuner-style user experience

Replace:

- FM tuner circuitry
- AM tuner circuitry
- IF processing
- RDS decoder
- RF front end

Add:

- ESP32-S3 controller
- Wi-Fi networking
- Internet radio station database
- Metadata processing
- Modern audio playback

---

# System Architecture

```text
                Internet Radio

                       │
                       ▼

                 Wi-Fi Network

                       │
                       ▼

                    ESP32-S3

                       │
                       ▼

                IC701/IC801/Etc

                       │
        ┌──────────────┼──────────────┐
        │              │              │
        ▼              ▼              ▼

    FL701 VFD      Keypad        Rotary Encoder
     Display
```

The ESP32 replaces the complete tuner subsystem while maintaining all user interaction through the original Sony hardware.

---

# Operating Concept

## Original Sony Operation

```text
User Turns Tuning Knob

      88.50 MHz
      88.60 MHz
      88.70 MHz

Sony Receives FM Station
```

---

## New Operation

```text
User Turns Tuning Knob

      88.50 MHz
      88.60 MHz
      88.70 MHz

ESP32 Selects
Internet Radio Stations
```

From the user's perspective nothing changes.

The display continues to show a frequency.

The tuning knob continues to browse stations.

Presets continue to operate normally.

---

# Virtual Frequency Plan

The displayed frequency becomes a station identifier.

Example:

| Displayed Frequency | Station |
|---------------------|----------|
| 88.50 MHz | Radio Paradise |
| 88.55 MHz | NPO Radio 1 |
| 88.60 MHz | NPO Radio 2 |
| 88.65 MHz | Sky Radio |
| 88.70 MHz | BBC Radio 1 |
| 88.75 MHz | BBC Radio 2 |

The station database is maintained by the ESP32.

No actual RF tuning occurs.

---

# Tuning Behaviour

The original tuning encoder remains the primary navigation method.

## Clockwise

```text
88.50
88.55
88.60
88.65
```

Move to next station.

## Counter Clockwise

```text
88.65
88.60
88.55
88.50
```

Move to previous station.

The interaction remains completely natural to anyone familiar with a tuner.

---

# Presets

Preset operation remains identical to the original Sony implementation.

Users continue to store and recall stations using:

```text
MEMORY

SHIFT

1-9
```

For example:

```text
Preset 1
Radio Paradise

Preset 2
NPO Radio 2

Preset 3
BBC Radio 1
```

The ESP32 simply associates preset numbers with Internet radio streams.

---

# RDS Replacement

The original RDS functionality is repurposed to display Internet metadata.

## Original

```text
NPO RADIO

TOP 40
```

## New

```text
RADIO PARADISE

Pink Floyd
Comfortably Numb
```

or

```text
BBC RADIO 2

Queen
Innuendo
```

The large dot-matrix area of the FL701 display is ideal for scrolling artist and track information.

---

# Numeric Keypad

The original numeric keypad remains functional.

The user may directly enter a virtual frequency.

Example:

```text
10130
```

Result:

```text
101.30 MHz
```

The ESP32 translates:

```text
101.30 MHz
        ↓
Station Database Lookup
        ↓
Classic Rock Radio
```

The display continues to show the entered frequency, preserving the tuner experience.

---

# TUNE MODE

The existing TUNE MODE button can be repurposed.

## Preset Mode

```text
Browse Stored Presets
```

## Station Mode

```text
Browse Entire Station Database
```

## Search Mode

```text
Direct Frequency Entry
```

---

# Display System

The original FL701 VFD remains installed.

Display content is generated entirely by the ESP32 by sending RDS data to the RDS decoder (IC801).

---


# Audio Path

The ESP32 becomes responsible for:

- Network connectivity (wifi / ethernet)
- Station selection (Mhz -> Internet Radio mapping)
- Audio streaming
- Metadata retrieval (to send to RDS)

Audio output can be provided by:

```text
ESP32
    ↓
I²S
    ↓
External DAC
    ↓
Original Sony Analog Outputs
```

Future revisions may support:

- SPDIF output
- AES/EBU output
- High-resolution streaming
- Local media playback

---

# End Goal

The completed unit should feel like an original Sony ST-SA3ES tuner.

The user should:

- Turn the tuning knob
- Browse frequencies
- Store presets
- Recall presets
- Read station information
- View track metadata

exactly as they would on the original tuner.

The only difference is that every displayed "station" corresponds to an Internet radio stream rather than a terrestrial FM or AM broadcast.

In short:

> The ST-SA3ES remains a tuner.
>
> The tuner simply happens to tune the Internet instead of the airwaves.
