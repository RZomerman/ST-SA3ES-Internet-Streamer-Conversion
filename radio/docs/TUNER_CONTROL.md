# Sony CN701 Tuner Control

The ESP32 observes the frequency commands sent by the original Sony IC701 and
maps selected FM frequencies to internet streams. The original tuning wheel,
presets, direct entry, VFD, and front-panel behavior remain under Sony control.

## Core Ownership

| Core | Application work |
| --- | --- |
| Core 0 | CN701 GPIO capture, frame decoding, frequency lookup, and status outputs |
| Core 1 | Arduino loop, AsyncTCP stream transport, Helix AAC decoding, and S/PDIF output |

SPI2 captures CLK and DATA into 32 prequeued receive buffers. The pool absorbs
the short zero-clock CE pulses observed around tuning commands. The core-0 task
validates and maps each completed transaction, then overwrites a one-entry
station selection queue. Core 1 remains the sole owner of ESP32Radio `String`
objects, the current URL, and the radio command queue.

## CN701 Input

| CN701 signal | ESP32-S3 | Direction |
| --- | --- | --- |
| CE, pin 9 | GPIO16 | Sony to ESP32 |
| CLK, pin 6 | GPIO15 | Sony to ESP32 |
| D-IN, pin 8 | GPIO17 | ESP32 to Sony, held high |
| DATA, pin 7 | GPIO18 | Sony to ESP32 |

CE is active high and CLK idles high. SPI2 uses mode 2, with CE inverted
internally through the GPIO matrix for its active-low chip-select input. A valid
transaction contains exactly 24 clocks and DATA is LSB-first within each byte.
Incomplete or overlength frames are rejected.

```text
decoded[0] = reverse8(wire bits 23..16)
decoded[1] = reverse8(wire bits 15..8)
decoded[2] = reverse8(wire bits  7..0)

divider = decoded[0] | (decoded[1] << 8)
RF_kHz  = (divider * 50) - 10800
```

The decoder performs a startup self-check with captured wire word `0x87E054`,
which must decode to bytes `E1 07 2A`, divider `0x07E1`, and 90.05 MHz.

## Station Reference

Edit `esp32radio/src/station_map.cpp` to add or change mappings. Frequencies use
integer kHz values to avoid floating-point comparisons.

```cpp
constexpr StationMapEntry kStationMap[] = {
    {90000, "RADIO538", "playerservices.streamtheworld.com:80/..."},
    {91000, "BNR Nieuwsradio", "stream.bnr.nl:80/bnr_aac_96_20"},
};
```

The BNR URL intentionally uses HTTP without analytics query parameters. It
redirects to the BNR StreamTheWorld AAC endpoint and is compatible with the
firmware's non-TLS `AsyncClient` transport.

## Status Outputs

| Signal | ESP32-S3 | No mapping | Mapped, connecting | Playing |
| --- | --- | ---: | ---: | ---: |
| D-IN | GPIO17 | High | High | High |
| SIG | GPIO9 | Low | High | High |
| AST | GPIO3 | Low | High | High |
| ST | GPIO2 | Low | Low | High |
| MUTE | GPIO8 | High | High | Low |

Changing to another mapped frequency immediately lowers ST and raises MUTE.
They change to the playing state only after the new stream has reached the
decoder. An unmapped valid FM frequency stops the current stream and clears
SIG, AST, and ST.

## Pin Substitutions

- TOSLINK moved from GPIO15 to GPIO7 because GPIO15 is now CLK.
- ST uses GPIO2 because GPIO46 is input-only on ESP32-S3 and cannot be driven.

Measure CN701 voltage levels before connection. Use input protection or level
translation if Sony logic exceeds ESP32-S3 limits, and ensure status outputs are
compatible with the Sony-side logic voltage.