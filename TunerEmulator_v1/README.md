# Sony IC701 Tuner Emulator

This is a new ESP32-S3 emulator implementation. It is separate from earlier passive sniffer work; that code is not a protocol reference.

For the current hardware-monitor status, validated memory-recall results, and next-agent integration guidance, read [HANDOFF.md](HANDOFF.md).

## Target and Wiring

Target: ESP32-S3-N16R8 with 16 MB flash and 8 MB PSRAM.

| C701 signal | ESP32-S3 GPIO | Direction |
| --- | --- | --- |
| CE | GPIO11 | input |
| CLK | GPIO10 | input |
| DATA | GPIO18 | input |
| DIN status | GPIO17 | output through open-drain interface |
| GND | GND | common reference |

The repository wiring notes confirm that GPIO17 is the DIN/status output. CE, CLK, and DATA are 5 V radio signals: use a suitable level shifter or the specified divider on every input: `C701 input -> 6K -> GPIO -> 10K -> GND`. Do not connect a 5 V signal directly to an ESP32 GPIO.

DIN must go through the intended transistor/MOSFET open-drain interface. The firmware never drives GPIO17 high: `INPUT` releases DIN and `OUTPUT LOW` requests a low level through that interface. Do not connect GPIO17 directly to a 5 V pulled-up DIN line.

## Behavior

CE is active high, CLK idles high, and DATA is sampled on CLK falling edges. The ISR captures canonical 24-bit frames into a fixed queue; decoding, logging, and state mutation occur in the main loop. Incomplete frames, overflow frames, and queue overruns are counted. DIN is released at boot and throughout normal operation.

Serial runs at 115200 baud. By default it reports state-changing frames only. Send `verbose on` or `verbose off` to control per-frame output. `din-low <milliseconds>` exists only as a controlled diagnostic command, but is disabled unless `ALLOW_DIN_DIAGNOSTIC` is changed to `true` and the hardware interface is verified.

Send `stats` for a one-shot receiver count snapshot, or `stats reset` before a controlled action. The counters include CE edges, CLK falling edges while CE is active, complete frames, incomplete frames, overflow frames, and queue overruns. These commands do not add runtime logging to the ISRs.

`lcdcam on` is an experimental ESP32-S3 hardware-capture probe. It disables the GPIO ISR receiver, routes GPIO10/11/18 into the LCD-CAM peripheral, and arms one 696-sample DMA capture for a Memory 1 or Memory 2 recall. It prints each decoded 24-bit group and a sample summary when DMA completes. `lcdcam vsync-high` repeats the probe with the static camera VSYNC gate high. `lcdcam ce-vsync` routes CE to camera VSYNC and captures a single 24-clock transfer on the C701 falling edge; `lcdcam ce-vsync-rising` tests the opposite camera clock phase. Re-arm it before each recall. This probe keeps DIN released and does not change the physical wiring.

`lcdcam monitor on` automatically rearms the verified CE-framed, rising-edge LCD-CAM capture and prints recognized frequency state changes. Use `lcdcam monitor off` to stop after the active capture finishes. The monitor can report the recalled frequency and band, but cannot identify a Memory 1/2 slot because memory selection is internal to C701 and no slot identifier is sent on this bus.

## Desktop Tests

From this directory:

```bash
./tests/run_tests.sh
```

The tests cover confirmed FM, MW, LW, persistent-control vectors, transition classification, canonical byte assembly, incomplete frames, and overflow frames.

## Arduino ESP32 Build and Upload

The repository had no ESP32 project configuration, so this sketch uses Arduino for ESP32. Install the Arduino ESP32 platform once:

```bash
arduino-cli config init
arduino-cli core update-index
arduino-cli core install esp32:esp32
```

Compile for the S3 with the desired flash/PSRAM menu settings:

```bash
arduino-cli compile --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi TunerEmulator_v1
```

Upload and monitor, replacing `/dev/ttyACM0` with the board port:

```bash
arduino-cli upload --fqbn esp32:esp32:esp32s3:FlashSize=16M,PSRAM=opi --port /dev/ttyACM0 TunerEmulator_v1
arduino-cli monitor --port /dev/ttyACM0 --config baudrate=115200
```

Use `arduino-cli board list` to find the serial port. Board-menu labels can differ by ESP32 core release; `arduino-cli board details --fqbn esp32:esp32:esp32s3` shows available options.

## Physical Hardware Checks Still Required

- Verify all three C701 inputs are at or below 3.3 V at the ESP32 pins after the dividers/level shifters are installed.
- Confirm the transistor/MOSFET DIN circuit releases high and pulls low without ever presenting 5 V to GPIO17.
- Confirm 24-bit capture during power-up, manual FM/MW/LW tuning, antenna changes, and memory recall with DIN released.
- Test seek/scan, mute behavior, and RF-dependent behavior before adding any DIN status signaling.
- Capture the original tuner for any feature that requires DIN activity; do not replay unverified historical pulses.