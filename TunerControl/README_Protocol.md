# Sony IC701 Logic Analyzer Protocol

## Scope

This document describes the front-panel serial bus observed between the Sony IC701 control logic and tuner circuitry. It also documents `analysis/final_frequency.py`, a small decoder that extracts the final tuned frequency from a sigrok `.sr` capture or a four-column CSV export.

The decoder is intended for offline analysis of a completed capture. It does not control PulseView, drive the bus, or decode every intermediate tuning step for display.

## Confirmed Signal Mapping

The logic analyzer channels are ordered as follows:

| Channel | Signal | Function |
| --- | --- | --- |
| D0 | CE | Chip enable / frame boundary |
| D1 | DIN | Present in the capture, not used for frequency decoding |
| D2 | DATA | Serial data decoded by this tool |
| D3 | CLK | Serial clock |

The CSV decoder assumes its first four columns are exactly:

```text
CE,DIN,DATA,CLK
```

The `.sr` decoder assumes the standard sigrok archive sample bytes, where the same signals occupy bits 0 through 3.

## Electrical and Timing Protocol

The observed bus has these properties:

- CE is idle low and active high.
- CLK is idle high.
- DATA is sampled on the falling edge of CLK.
- This corresponds to SPI mode 2: `CPOL=1`, `CPHA=0`.
- Each CE assertion contains 24 data bits.
- Each 24-bit transfer is three 8-bit bytes.
- The captured byte representation is shown in wire order, with each byte assembled from the sampled bit stream.
- The first two bytes contain the frequency number with their bit order reversed.
- The third byte of a frequency frame is the fixed marker `54`.

A normal tuning action contains three CE transfers:

```text
Transfer 1: frequency frame
Transfer 2: E3 4C 88
Transfer 3: E3 4C 88
```

The two control frames have remained fixed in the captures examined so far. The decoder ignores them.

## Frequency Encoding

For a frequency frame:

```text
b0 b1 54
```

Reverse the bits in the first two bytes independently, then combine them little-endian:

```text
frequency_MHz = (N * 0.05) - 10.7
```

The inverse calculation is:

```text
N = round((frequency_MHz + 10.7) / 0.05)
```

Each increment of one in `N` represents 0.05 MHz. The frequency frame can therefore be generated as:

```text
b0 = reverse_bits(N & 0xff)
b1 = reverse_bits((N >> 8) & 0xff)
frame = b0 b1 54
```

### Example: 92.05 MHz

```text
N = (92.05 + 10.7) / 0.05
N = 2055 = 0x0807
```

Bit reversal gives:

```text
reverse_bits(07) = E0
reverse_bits(08) = 10
```

Expected frame:

```text
E0 10 54
```

This matches the `toB.csv` capture, which decoded to 92.05 MHz.

### Example: 107.90 MHz

```text
N = (107.90 + 10.7) / 0.05
N = 2372 = 0x0944
```

Expected frame:

```text
22 90 54
```

This matches the 16 MHz `to1.csv` capture.

## Capture Quality

Use at least 8 MHz for routine captures. Use 16 MHz when resolving the upper end of the band or when a single bit matters.

The earlier 2 MHz and 4 MHz captures sometimes produced a frame that was exactly two count units high, which appears as a 0.10 MHz error. For example:

```text
Expected for 108.00 MHz: 22 90 54
Observed at lower resolution: 62 90 54
```

The difference is one captured bit in the first byte. A 16 MHz capture correctly produced `22 90 54`.

Recommended capture settings:

- Sample rate: 8 MHz minimum for general work
- Sample rate: 16 MHz for confirmation captures
- Channels: D0 through D3
- Logic threshold: appropriate for the IC701 signal voltage
- Ground: analyzer ground connected to the IC701 signal ground
- Capture long enough to include the final CE transfer and its falling edge

## Decoder Script

The decoder is located at:

```text
analysis/final_frequency.py
```

### Basic usage

Decode a CSV capture:

```bash
python3 analysis/final_frequency.py captures/toB.csv
```

Decode a sigrok session:

```bash
python3 analysis/final_frequency.py captures/toX4.sr
```

Override the IF calibration for comparison:

```bash
python3 analysis/final_frequency.py captures/toA.csv --if-mhz 10.6
```

The default is currently `10.7` MHz.

### Output

A successful decode looks like this:

```text
Final frequency: 92.05 MHz
Frame: E0 10 54
Complete frames: 21
```

`Complete frames` counts all 24-bit CE transfers, including control transfers. Only the last valid frequency frame is reported.

If no valid frequency frame is found, the script returns exit status 1 and prints the number of complete frames it did decode. Input and archive errors return exit status 2.

## How the Script Works

### 1. Select an input reader

`sample_stream()` selects a reader based on the file extension:

- `.csv`: `csv_samples()` reads rows one at a time.
- `.sr`: `sr_samples()` opens the sigrok ZIP archive and streams each `logic-1-*` data member in chunks.

Neither reader loads a large CSV or full `.sr` sample stream into memory.


The decoder then reads:

```text
CE   = sample bit 0
DATA = sample bit 2
CLK  = sample bit 3
### 3. Detect a CE transaction

A rising CE edge starts a transaction and clears the bit buffer. A falling CE edge ends it. A transaction is considered complete only when exactly 24 clocked bits were collected.

### 4. Sample DATA on the falling clock edge

When CE is high, the state machine detects:

```text
previous CLK = 1
current CLK  = 0
### 5. Assemble three bytes

Every group of eight sampled bits is assembled into one displayed protocol byte. The resulting three-byte frame is checked for the fixed `54` marker.

### 6. Retain only the newest frequency frame

When a frame matches the marker and decodes to a frequency in the 76.0 to 108.0 MHz range, it replaces the previously retained result. At end of input, the retained frame is printed as the final frequency.

## Publishing Assessment

### Suitable for a wiki now

The protocol description is sufficiently clear for a wiki page if it is presented as a reverse-engineering result based on captured evidence. The following points are well supported by repeated captures:

- Four-channel mapping D0/D1/D2/D3
- CE active-high framing
- CLK idle-high operation
- 0.05 MHz frequency-number increments
- The 10.7 MHz calibration matching the recent display-confirmed captures

### Caveats to retain in the wiki

This is not yet a complete vendor specification. The wiki should state that:

- The protocol was inferred from logic-analyzer captures, not an official Sony service document.
- The decoder assumes the CSV column order and sigrok sample layout described above.
- The 10.7 MHz constant is an empirical calibration and should be rechecked against another known display frequency if the hardware variant changes.
- The decoder reports the last valid bus frame, which may differ from the final display state if the capture continued during additional tuning or if the display was read before the last transfer settled.
- A low-rate capture can contain single-bit errors even when the frame structure looks correct.
- The script currently decodes only the DATA channel and does not validate DIN, electrical levels, clock duty cycle, or control-frame contents.

### Small improvements before a polished public release

The script is ready for a practical wiki companion, but a polished public release should add:

1. A short automated test file covering bit reversal, known frames, and the 10.7 MHz formula.
2. An explicit CSV header check instead of accepting any four-column header.
3. Optional channel-column arguments for CSV exports with a different order.
4. Metadata validation for the `.sr` samplerate and channel names.
5. A mode that prints all valid frequency frames when investigating capture/display timing.
5. Export CSV only if needed; the `.sr` file is more compact.
6. Run `analysis/final_frequency.py` on the capture.
7. Record the printed frame, frequency, sample rate, and displayed frequency.
- The protocol was inferred from logic-analyzer captures, not an official Sony service document.
