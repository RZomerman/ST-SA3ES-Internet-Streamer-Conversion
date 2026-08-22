# ST-SA3ES Internet Streamer Conversion
# Interface PCB Revision A

## Overview

The board is installed between the Sony mainboard and front panel using the existing CN701 and CN702 connectors.

Two ESP32-S3 development boards are used:

| Module | Purpose |
|----------|----------|
| ESP32-S3-CONTROL | Sony interface, display control, button handling and tuner emulation |
| ESP32-S3-AUDIO | WiFi, streaming, metadata retrieval and SPDIF audio generation |

Communication between both processors is performed over UART.

---

# System Architecture

```text
Sony Mainboard
       |
       |
 CN701 / CN702
       |
       *--> CE
       *--> DATA
       *--> CLK
       *--> BLN
       *--> MUTE
       |
       V
ESP32-S3-CONTROL <--> UART <--> ESP32-S3-AUDIO
       |                            |
       |                            +--> WiFi
       |                            +--> Internet Radio
       |                            +--> Streaming Services
       |                            +--> Metadata
       |                            +--> Optical SPDIF Output
       +--> RDS-C
       +--> RDS-D
       +--> SI
       +--> AST
       +--> ST
       +--> D-IN
       |
       V
 CNP701 / CNP702
       |
       V
Sony Front Panel
```

---

# Component Overview

| Ref | Component | Purpose | Supply |
|------|------|------|------|
| J1 | to CN701 Front-Panel Side | Sony interface to front |  |
| J2 | to CNP701 Mainboard Side | Sony interface to main |  |
| J3 | to CN702 Front-Panel Side | Sony interface to front |  |
| J4 | to CNP702 Mainboard Side | Sony interface to main |  |
| J5 | SPDIF Output | Optical digital audio output | 5V |
| U1 | ESP32-S3-CONTROL | Sony interface processor | 3.3V |
| U2 | ESP32-S3-AUDIO | Audio streaming processor | 3.3V |
| U3 | SN74LVC245APW | Sony → ESP32 level translation | 3.3V |
| U3 | SN74AHCT125PW | ESP32 → Sony level translation #1 | 5V |
| U4 | SN74AHCT125PW | ESP32 → Sony level translation #2 | 5V |
| U5 | MCP1825S-3302 | Main 3.3V regulator | 5V Input |
| C1 | 100uF | Main 3.3V bulk capacitor | 3.3V |
| C2 | 10uF | MCP1825 input capacitor | 5V |
| C3 | 22uF | MCP1825 output capacitor | 3.3V |
| C4 | 100nF | SN74LVC245 decoupling | 3.3V |
| C5 | 100nF | SN74AHCT125 decoupling | 5V |
| C6 | 100nF | SN74AHCT125 decoupling | 5V |
| R1 | 470Ω | D-IN Output protection and current limiting from SN74AHCT125 to Sony logic | |
| R2 | 470Ω | RDS-C Output protection and current limiting from SN74AHCT125 to Sony logic | |
| R3 | 470Ω | SI Output protection and current limiting from SN74AHCT125 to Sony logic | |
| R4 | 470Ω | RDS-D Output protection and current limiting from SN74AHCT125 to Sony logic | |
| R5 | 470Ω | AST Output protection and current limiting from SN74AHCT125 to Sony logic | |
| R6 | 470Ω | ST Output protection and current limiting from SN74AHCT125 to Sony logic | |
| R7 | 1kΩ | CE Input protection for SN74LVC245 Sony → ESP32 translation | |
| R8 | 1kΩ | DATA Input protection for SN74LVC245 Sony → ESP32 translation | |
| R9 | 1kΩ | CLK Input protection for SN74LVC245 Sony → ESP32 translation | |
| R10 | 1kΩ | MUTE Input protection for SN74LVC245 Sony → ESP32 translation | |
| R11 | 1kΩ | BLN Input protection for SN74LVC245 Sony → ESP32 translation | |
| R12 | 10kΩ | AUDIO_READY Pull-down resistor, default LOW state | |
| JP01 | CE input Switch | Switch signal between ESP or original path |  |
| JP02 | D-IN input Switch | Switch signal between ESP or original path |  |
| JP03 | DATA input Switch | Switch signal between ESP or original path |  |
| JP04 | CLK input Switch | Switch signal between ESP or original path |  |
| JP05 | RDS-C input Switch | Switch signal between ESP or original path |  |
| JP06 | BLN input Switch | Switch signal between ESP or original path |  |
| JP07 | ST input Switch | Switch signal between ESP or original path |  |
| JP08 | RDS-D input Switch | Switch signal between ESP or original path |  |
| JP09 | AST input Switch | Switch signal between ESP or original path |  |
| JP10 | MUTE input Switch | Switch signal between ESP or original path |  |
| JP11 | SI input Switch | Switch signal between ESP or original path |  |
| JP13 | ESP32 Audio Enable | Disconnect Audio ESP 3.3V during development | 3.3V |
| JP14 | ESP32 Control Enable | Disconnect Control ESP 3.3V during development | 3.3V |


---

# Power Architecture

## Sony Supply

The Sony logic board provides a regulated 5V supply through pin 2 and 3 of CN701.

This 5V rail is used as the primary power source for the interface PCB.

---

## MCP1825S-3302

The MCP1825S-3302 was selected because:

- 1A output current
- Low dropout voltage
- Through-hole TO220 package
- Excellent transient response
- Easily supports two ESP32 modules

Power path:

```text
Sony +5V
    |
 MCP1825S-3302
    |
   3.3V
    |
    +---- ESP32-CONTROL
    |
    +---- ESP32-AUDIO
    |
    +---- SN74LVC245
```

---

## Capacitor Selection

### C1 - 100uF

Bulk storage capacitor for WiFi transmission current bursts.

Purpose:

- Prevent rail droop
- Improve transient response
- Improve ESP32 stability

### C2 - 10uF

Regulator input stabilization capacitor.

### C3 - 22uF

Regulator output stabilization capacitor.

### C4/C5/C6 - 100nF

Local decoupling capacitors.

Purpose:

- High-frequency noise suppression
- Prevent logic glitches
- Maintain stable supply rails

These are ceramic capacitors and are intentionally non-polarized.

---

# Signal Translation

The Sony circuitry operates at voltages incompatible with direct ESP32 connection.

Two translation methods are used.

---

## Sony → ESP32

### U3 - SN74LVC245APW

Supply:

```text
VCC = 3.3V
```

Configuration:

```text
DIR = HIGH
OE  = LOW
```

Operation:

```text
A -> B
```

Purpose:

```text
Sony Logic
     |
SN74LVC245
     |
ESP32 Inputs
```

Protection:

- 1k series resistors on Sony inputs

Signals translated:

| Sony Signal | Direction | Voltage |
|------------|------------|------------|
| CE | Sony → ESP32 | ~5V → 3.3V |
| DATA | Sony → ESP32 | ~5V → 3.3V |
| CLK | Sony → ESP32 | ~5V → 3.3V |
| MUTE | Sony → ESP32 | ~5V → 3.3V |
| BLN | Sony → ESP32 | ~5V → 3.3V |

---

## ESP32 → Sony

### SN74AHCT125PW

Supply:

```text
VCC = 5V
```

All OE pins:

```text
LOW (GND)
```

The AHCT family was selected because:

- 3.3V is accepted as a valid HIGH
- Outputs switch to full 5V levels

Operation:

```text
ESP32
   |
SN74AHCT125
   |
Sony Logic
```

Protection:

- 470Ω series output resistors

--

# Inter-Processor Communication

## UART

Primary communication channel between ESP32 processors.

Used for:

- Station selection
- Metadata transfer
- Playback state
- Error reporting
- Control commands

Connection:

```text
CONTROL TX ---> AUDIO RX
CONTROL RX <--- AUDIO TX
```

---

## AUDIO_READY

Optional hardware status signal.

```text
Audio ESP ---> Control ESP
```

Purpose:

- Audio processor boot status
- WiFi status indication
- Playback readiness indication

Default state:

```text
LOW
```

via 10k pull-down resistor.

---

# SPDIF Output

## J5

Optical digital audio output.

The first revision intentionally focuses on SPDIF only.

Reasons:

- Simplest architecture
- Lowest noise
- Existing DAC compatibility
- Reduced hardware complexity

Future revisions may add analogue output support.

---

# Development Considerations

Both ESP32 development boards include onboard USB interfaces.

JP13 and JP14 allow individual ESPs to be disconnected from the PCB 3.3V rail during firmware development.

This prevents power conflicts between:

- USB-powered operation
- Sony-powered operation

and allows convenient programming without removing the boards.

Each signal path has test points on the ingoing and outgoing sides. 

---


The goal is to preserve the complete Sony user experience while replacing the original RF tuner subsystem with a modern streaming architecture.

The finished device should appear and operate as though Sony had released a network-enabled ES tuner while retaining the original display, controls and industrial design.
