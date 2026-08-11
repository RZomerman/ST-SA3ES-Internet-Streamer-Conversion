# Sony IC701 Front-Panel Serial Protocol

## Purpose

This document describes the serial bus observed around the Sony IC701 tuner control logic. It is intended for someone who has a logic-analyzer capture and wants to determine:

- the tuned frequency;
- whether Antenna A or Antenna B was selected; and
- how the decoder reaches its result.

This is an evidence-based reverse-engineering result, not an official Sony protocol specification.

The repository decoder is [analysis/final_frequency.py](analysis/final_frequency.py). It accepts a sigrok `.sr` capture or a four-column CSV export and reports the newest valid frequency frame and newest recognized antenna-control frame.

## Capture Setup

Connect analyzer ground to the IC701 signal ground and assign the first four channels as follows:

| Channel | Signal | Role |
| --- | --- | --- |
| D0 | CE | Frame boundary |
| D1 | DIN | Tuner-driven active-low status candidate; not decoded here |
| D2 | DATA | Tuning and control data |
| D3 | CLK | Serial clock |

CSV exports must have this column order:

```text
CE,DIN,DATA,CLK
```

The `.sr` reader expects the same signals in sample bits 0 through 3.

### Sample rate

Use 8 MHz or higher for routine captures. Use 16 MHz for confirmation captures and single-step analysis. Earlier 2 MHz and 4 MHz captures occasionally sampled one DATA bit incorrectly, producing an apparent 0.10 MHz error.

## Bus Timing

The observed bus has these properties:

- CE is idle low and active high.
- CLK is idle high.
- DATA is sampled on the falling edge of CLK.
- The timing corresponds to SPI mode 2: `CPOL=1`, `CPHA=0`.
- Each CE assertion contains 24 clocked bits.
- Each transfer is three 8-bit bytes.

A normal tuning operation has this shape:

```text
Transfer 1: frequency frame
Transfer 2: antenna/control frame
Transfer 3: antenna/control frame
```

### DIN direction and current interpretation

With the tuner side of DIN disconnected, while the analyzer remained connected to C701, DIN stayed high for all 50,000,000 samples and had no transitions. With the tuner connected, it contained active-low intervals. CE, DATA, CLK, and all 29 decoded command frames remained identical after the disconnect.

This shows that the tuner side is responsible for bringing DIN low. The line is likely a tuner-to-C701 status signal with a pull-up on the C701 side, possibly using an open-collector or open-drain output. Its low intervals occur mostly while CE is inactive and CLK is idle, so it does not behave like synchronous serial return data in the observed captures.

All captures used for this analysis were made without an RF signal. DIN activity therefore cannot currently be attributed to signal strength, station detection, or stereo reception. An active-low tuner-busy or PLL-unlocked indication is the leading hypothesis, but the exact function is not confirmed.

## Frequency Encoding

A frequency frame has the form:

```text
b0 b1 54
```

The final byte, `54`, identifies the frequency frame. Reverse the bits in the first two bytes independently and combine them little-endian:

```text
N = reverse_bits(b0) | (reverse_bits(b1) << 8)
frequency_MHz = (N * 0.05) - 10.7
```

The 10.7 MHz constant is an empirical calibration confirmed against display readings. One increment of `N` represents 0.05 MHz.

To generate an expected frame from a frequency:

```text
N = round((frequency_MHz + 10.7) / 0.05)
b0 = reverse_bits(N & 0xff)
b1 = reverse_bits((N >> 8) & 0xff)
frame = b0 b1 54
```

## Antenna Encoding

The antenna state is carried by the second byte of a control frame. The observed control frames are:

| Frame | Meaning |
| --- | --- |
| `E3 4C 88` | Antenna A, repeated control frame |
| `E3 CC 88` | Antenna B, repeated control frame |
| `EB 4C 88` | Generic transition/startup frame with Antenna A selected |
| `EB CC 88` | Generic transition/startup frame with Antenna B selected |

The persistent selection is the high bit of byte 2:

```text
4C: bit 7 clear -> Antenna A
CC: bit 7 set   -> Antenna B
```

The first byte is normally `E3`. `EB` and a temporary third byte of `98` also appear during startup or state transitions, including a Memory A recall capture where the antenna did not change. They must not be interpreted as antenna-change flags. The `4C`/`CC` distinction remains the confirmed antenna-selection field.

The decoder recognizes both `E3` and `EB` prefixes and retains the newest antenna state independently of the frequency state.

## Worked Capture: `toAntB.csv`

The antenna-B state is visible in these frames:

```text
EB CC 88
E3 CC 88
```

The final frequency frame is `67 10 54`, which decodes to 103.20 MHz.

```text
Final frequency: 103.20 MHz
Frame: 67 10 54
Antenna: B
Antenna frame: E3 CC 88
Complete frames: 25
```

## Worked Capture: `backtoAntA.csv`

The return to Antenna A is visible in these frames:

```text
EB 4C 88
E3 4C 88
```

The final frequency remains 103.20 MHz:

```text
Final frequency: 103.20 MHz
Frame: 67 10 54
Antenna: A
Antenna frame: E3 4C 88
Complete frames: 25
```

## Memory Recall Changes

Memory selection is handled inside the C701 main controller. The downstream bus documented here does not need a Memory A/B identifier; it carries the tuner state produced by the recall operation.

In `toMemoryA.csv`, Memory A restores 90.00 MHz and Antenna A:

```text
7B E0 54   90.00 MHz
E3 4C 88   Antenna A control
```

In `toMemoryB.csv`, Memory B restores 102.15 MHz and Antenna B:

```text
8B 10 54   102.15 MHz
E3 CC 88   Antenna B control
```

The relevant observable changes are therefore the recalled frequency, band, antenna selection, and generic transition/control frames. The initial `EB ...` or third-byte `98` forms are generic state-transition patterns and are not memory-slot identifiers.

## Worked Capture: `toA.csv`

The frequency frame is:

```text
35 E0 54
```

```text
reverse_bits(35) = AC
reverse_bits(E0) = 07
N = 0x07AC = 1964
frequency = (1964 * 0.05) - 10.7
frequency = 87.50 MHz
```

The decoder result is:

```text
Final frequency: 87.50 MHz
Frame: 35 E0 54
Complete frames: 3
```

## Worked Capture: `toB.csv`

The frequency frame is:

```text
E0 10 54
```

```text
reverse_bits(E0) = 07
reverse_bits(10) = 08
N = 0x0807 = 2055
frequency = (2055 * 0.05) - 10.7
frequency = 92.05 MHz
```

The decoder result is:

```text
Final frequency: 92.05 MHz
Frame: E0 10 54
Complete frames: 21
```

If no recognized antenna-control frame is present, the script reports `Antenna: not determined`.

## Using the Decoder

Run from the repository root:

```bash
python3 analysis/final_frequency.py captures/toB.csv
python3 analysis/final_frequency.py captures/toX4.sr
```

The default IF constant is 10.7 MHz. To compare another calibration:

```bash
python3 analysis/final_frequency.py captures/toA.csv --if-mhz 10.6
```

Successful output contains:

```text
Final frequency: 92.05 MHz
Frame: E0 10 54
Antenna: B
Antenna frame: E3 CC 88
Complete frames: 21
```

Exit statuses are:

- `0`: a valid frequency frame was found.
- `1`: the input was readable, but no valid frequency frame was found.
- `2`: an input, CSV, or sigrok archive error occurred.

## How the Script Works

### 1. Stream the input

`csv_samples()` reads CSV rows one at a time. `sr_samples()` opens the sigrok archive and streams its `logic-1-*` members in chunks. This avoids loading a large CSV or full capture into memory.

### 2. Pack and read the signals

CSV columns 0 through 3 become sample bits 0 through 3. The state machine reads:

```text
CE   = sample bit 0
DATA = sample bit 2
CLK  = sample bit 3
```

### 3. Assemble a transfer

A rising CE edge starts a transaction. While CE is high, every falling CLK edge appends the current DATA level. A falling CE edge ends the transaction. Exactly 24 sampled bits are assembled into three bytes.

### 4. Classify the frame

- A frame ending in `54` is tested as a frequency frame.
- A frame with first byte `E3` or `EB`, second byte `4C` or `CC`, and third byte `88` is tested as an antenna-control frame.
- The newest valid frequency and antenna results are retained.

## Interpretation Caveats

The decoder reports the last valid bus frame in the file. That may differ from the frequency visible on the unit if the capture continued after the display was read, or if a low-rate capture sampled a DATA bit incorrectly.

The antenna result is based on the observed control-frame relationship. It has not been verified against an official Sony service document. The exact DIN status meaning, the full meaning of the `E3`/`EB` distinction, and any possible antenna states beyond A and B remain outside the current decoder scope.

## Wiki Readiness

The protocol and decoder are suitable for a technical wiki as a documented reverse-engineering result. The page should retain the assumptions above and should not present the decoder as an official Sony implementation.

Before publishing the script as a general-purpose package, useful follow-ups would be:

1. Add automated tests for bit reversal, known frequency frames, and antenna-frame classification.
2. Validate the CSV header and allow configurable column order.
3. Validate sigrok samplerate and channel metadata.
4. Add an option to print every frequency and antenna frame for investigations.
5. Warn when the capture ends with an incomplete CE transaction.

## Reproduction Checklist

1. Connect analyzer ground to IC701 signal ground.
2. Connect D0, D1, D2, and D3 as specified above.
3. Capture at 8 MHz or higher; use 16 MHz for confirmation.
4. Include the complete transfer after the tuning or antenna action.
5. Keep the original `.sr` file; export CSV only when useful.
6. Run `analysis/final_frequency.py` on the capture.
7. Record the sample rate, decoded frames, display frequency, and antenna selection.
8. If results disagree, inspect all frames and check for extra transfers or low-rate sampling errors.
