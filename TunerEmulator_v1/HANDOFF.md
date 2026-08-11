# IC701 ESP32 Monitor Handoff

## Purpose

`TunerEmulator_v1` is the active ESP32-S3 tuner-bus receiver and Stage 1 DIN emulator. It passively receives C701 tuner commands and leaves DIN released. The immediate next use is to provide decoded tuning state to an internet-radio channel controller.

Do not use `esp32/ic701_sniffer/ic701_sniffer.ino` as a protocol reference. It contains stale signal direction, edge, marker, and FM offset assumptions.

## Verified Hardware

Target board: ESP32-S3-N16R8, 16 MB flash, 8 MB PSRAM.

| C701 signal | ESP32-S3 GPIO | Direction |
| --- | --- | --- |
| CE | GPIO11 | input |
| CLK | GPIO10 | input |
| DATA | GPIO18 | input |
| DIN | GPIO17 | open-drain status output |
| GND | GND | common ground |

The C701 bus is 5 V. The tested inputs use resistor dividers:

```text
C701 signal -> 6K -> ESP32 GPIO -> 10K -> GND
```

The divider output is about 3.125 V for a 5 V high. It has been validated by both logic-analyzer captures and the ESP32 LCD-CAM receiver. Do not replace it with 1K/2K solely to solve capture errors.

DIN must remain electrically isolated through the intended transistor/MOSFET open-drain interface. Firmware must never drive GPIO17 high. Stage 1 leaves DIN released.

## Protocol Facts

- CE is active high.
- CLK idles high.
- DATA is sampled on falling CLK edges.
- A transfer is exactly 24 bits.
- Canonical assembly is `byte |= bit << (7 - offset)`.
- FM marker is `0x54`.
- FM PLL decode: `N = reverse_bits(b0) | (reverse_bits(b1) << 8)`.
- FM frequency: `N * 0.05 - 10.7` MHz.

Confirmed persistent control frames:

```text
E3 4C 88 -> FM, Antenna A
E3 CC 88 -> FM, Antenna B
EF 4D 88 -> MW
ED 4F 88 -> LW
```

Memory selection is internal to C701. The tuner bus has no Memory 1/2 identifier; it carries the selected result (frequency, band, and antenna state).

## Capture Architecture

The initial GPIO falling-edge ISR missed clock edges. The working receiver uses ESP32-S3 LCD-CAM plus GDMA:

```text
GPIO10 CLK  -> CAM_PCLK
GPIO18 DATA -> CAM_DATA_IN0
GPIO11 CE   -> CAM_DATA_IN1 and CAM_VSYNC
```

LCD-CAM starts sampling two clocks after the CE/VSYNC boundary. The firmware captures the first two falling-edge DATA bits using a tiny ISR, then uses LCD-CAM DMA for the remaining bits. This combination successfully reconstructed known memory frames:

```text
Memory 1: 7B E0 54 -> 90.00 MHz
Memory 2: 8B 10 54 -> 102.15 MHz
```

The relevant source is:

- `TunerEmulator_v1/lcd_cam_capture.cpp`
- `TunerEmulator_v1/lcd_cam_capture.h`
- `TunerEmulator_v1/tuner_emulator.ino`

The implementation uses ESP32 Arduino core 3.3.11 bundled ESP-IDF private LCD-CAM/GDMA headers. Pin the core version unless the low-level backend is revalidated after an upgrade.

## Continuous Monitor

After flashing, start the serial monitor and enable automatic capture rearming:

```bash
arduino-cli monitor --port /dev/ttyACM0 --config baudrate=115200
```

Then enter:

```text
lcdcam monitor on
```

It prints recognized frequency state changes, for example:

```text
MONITOR FM 90.00MHz | RX 7B E0 54
MONITOR FM 102.15MHz | RX 8B 10 54
```

Disable it with:

```text
lcdcam monitor off
```

The current monitor automatically rearms after a completed frame. It is suitable for memory recalls and coarse state observation, but it is not yet a production-quality multi-frame transaction receiver.

## Important Accuracy Limitation

Memory recalls have decoded correctly. Arbitrary manual FM tuning can produce a wrong first byte while the trailing bytes are correct. This is a remaining CE/VSYNC boundary-alignment weakness in the hybrid capture path.

Observed examples:

```text
Displayed 102.90 MHz -> expected frame 07 10 54
Displayed 101.75 MHz -> expected frame 93 10 54
```

Do not use every raw monitor frame as an authoritative fine-tuning frequency. Treat 50 kHz / 100 kHz end digits with caution, especially `.05` and sometimes `.10` display increments. For the next internet-radio controller, prefer one of these policies until the receiver is improved:

1. Use validated recalled-memory frequencies as channel presets.
2. Quantize or debounce observed frequencies before selecting a station.
3. Require repeated identical decoded frames before changing the internet-radio channel.
4. Maintain an application mapping from accepted C701 frequency ranges/presets to station URLs rather than assuming every single monitor update is exact.

The root fix is to capture a complete CE transaction sequence with a multi-frame LCD-CAM/GDMA queue, preserving every frame and selecting a corroborated final frequency frame. Do not revert to the old per-edge GPIO receiver: it missed 111 of 696 expected clock edges on a Memory 1 recall.

## Build and Validation

From repository root:

```bash
sh TunerEmulator_v1/tests/run_tests.sh
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi TunerEmulator_v1
arduino-cli upload --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi --port /dev/ttyACM0 TunerEmulator_v1
```

Desktop tests cover confirmed FM, MW, LW, control, transition, canonical assembly, incomplete, and overflow vectors.

## Next Agent Priorities

1. Keep DIN released unless new evidence requires a status pulse.
2. Preserve the validated LCD-CAM/GDMA mapping and rising-edge configuration.
3. Implement a multi-frame DMA queue or descriptor strategy to capture an entire recall transaction rather than one frame at a time.
4. Decode and retain persistent control frames to determine Antenna A/B.
5. Add an internet-radio mapping layer only after defining its tolerance/debounce policy for manual fine tuning.
6. Test MW and LW with the hardware capture path before treating them as production inputs.
