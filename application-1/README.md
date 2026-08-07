# Sony ST-SA3ES Internet Radio and RDS Emulator

This project turns an ESP32-S3 N16R8 into an internet-radio and RDS source for
the Sony ST-SA3ES tuner. The integrated firmware connects to Wi-Fi, starts the
RADIO538 AAC stream, decodes it with the Helix decoder, sends digital audio to
a TOSLINK transmitter, and supplies station and track metadata to the original
Sony controller through its recovered RDS clock/data interface.

> **Important RDS boundary:** this firmware does not synthesize an FM signal or
> a 57 kHz RDS subcarrier. It replaces the post-demodulator clock and data
> outputs normally supplied by the SAA6579T RDS decoder.

## Program Overview

The firmware has three main paths:

1. **Internet radio:** ESP32Radio-V2 joins the configured Wi-Fi network,
	 follows the RADIO538 stream redirect, receives AAC audio, and extracts
	 Icecast station/title metadata.
2. **Digital audio:** Helix decodes AAC to stereo PCM. The software S/PDIF
	 encoder generates a digital stream on GPIO15 for a powered TOSLINK optical
	 transmitter module.
3. **RDS emulation:** station and title updates are converted into valid RDS
	 Group 0A and Group 2A messages. A hardware timer transmits their 104 bits on
	 GPIO6, synchronized to the 1187.507 Hz clock on GPIO5.

Local selector, push-button, rotary, IR, and display inputs are disabled. The
Sony front panel remains the user-facing display while the ESP32 starts the
configured station automatically.

## Connections

| ESP32-S3 pin | Signal | Destination | Purpose |
| --- | --- | --- | --- |
| GPIO5 | RDS-C | Sony controller RDS clock input | 1187.507 Hz recovered RDS clock |
| GPIO6 | RDS-D | Sony controller RDS data input | Synchronous RDS group bits |
| GPIO15 | S/PDIF | Powered TOSLINK module data input | Digital stereo audio |
| GND | Ground | Sony and TOSLINK module ground | Common reference |

The ESP32-S3 uses 3.3 V logic while the measured Sony RDS logic-high level is
approximately 4.8 V. Isolate the original SAA6579T output and use a suitable
3.3 V-to-5 V buffer or source selector. Never drive the Sony bus in parallel
with an active SAA6579T output. A bare optical socket is not sufficient for
GPIO15; use a powered TOSLINK transmitter circuit with compatible input logic.

## Essential RDS Parts

### Recovered Clock and Data

- Nominal bit clock: **1187.507 Hz**
- One bit is transferred per complete RDS-C clock period.
- GPIO6 changes only after a falling clock edge, while GPIO5 is low.
- GPIO6 remains stable across the following rising edge.
- The first data bit is installed before the first rising edge.
- Bits are transmitted most-significant bit first.

The timer interrupt runs at twice the bit-clock rate because it handles both
clock edges. With an 80 MHz timer source, divider 4, and alarm value 8421, the
half-period is approximately 421.05 microseconds.

### RDS Framing

An RDS group contains four blocks:

```text
Group:  Block A        Block B        Block C/C'      Block D
				26 bits        26 bits        26 bits         26 bits
				-----------------------------------------------------
															104 bits
```

Each block consists of a 16-bit information word followed by a 10-bit
checkword. One complete group takes approximately 87.6 ms at 1187.507 bit/s.
Sending plain ASCII bytes is therefore not sufficient; every character must be
placed in an RDS group and protected by a valid checkword.

### CRC and Offset Words

The encoder uses generator polynomial `0x05B9`. It shifts the 16-bit
information word left by ten bits, calculates the polynomial remainder, XORs
that remainder with the block's offset word, and appends the resulting
10-bit checkword.

| Block | Offset word |
| --- | ---: |
| A | `0x0FC` |
| B | `0x198` |
| C | `0x168` |
| C-prime | `0x350` |
| D | `0x1B4` |

The offset identifies the block position and lets the Sony controller recover
group alignment in a continuous bitstream.

### Group 0A: Station Identity

Group 0A is the essential station-name group:

| Block | Contents used here |
| --- | --- |
| A | PI station identifier |
| B | Group type 0A, TP, PTY, TA, Music/Speech, DI, and PS segment address |
| C | Alternative Frequency word |
| D | Two Program Service characters |

Four segment addresses reconstruct the eight-character **Program Service
(PS)** field. Longer internet station names are displayed as an eight-character
marquee. The window advances by one character after two complete PS frames so
the Sony display has time to accept every segment.

The default PI is `0x83C7`, matching the captured RADIO538 service. The current
Alternative Frequency words are replay-compatible values from that capture:
`0x8F90`, `0x9293`, `0x9495`, and `0x9697`. They are not a general AF list.

### Group 2A: RadioText

Group 2A carries the longer **RadioText (RT)** value:

| Block | Contents used here |
| --- | --- |
| A | PI station identifier |
| B | Group type 2A, TP, PTY, Text A/B flag, and RT segment address |
| C | First two characters of the segment |
| D | Last two characters of the segment |

Sixteen segments provide a 64-character field. The encoder inserts a carriage
return after meaningful text when room remains, then pads the rest with spaces.
Whenever RadioText changes, the Text A/B flag toggles and transmission restarts
at segment zero. This tells the receiver to discard the old text and assemble
the new message.

### PI, PTY, TP, and TA

- **PI:** 16-bit Program Identification code, kept identical in every group.
- **PTY:** 5-bit Program Type in the range `0..31`; the integrated profile uses
	PTY 10.
- **TP:** Traffic Programme flag; enabled in the default profile.
- **TA:** Traffic Announcement flag; disabled in the default profile.
- **Music/Speech:** set to music in Group 0A.

### Group Scheduler

The encoder continuously interleaves five Group 0A messages and three Group 2A
messages in each eight-group schedule:

```text
0A, 2A, 0A, 0A, 2A, 0A, 2A, 0A
```

Group 0A cycles addresses `0..3`; Group 2A cycles addresses `0..15`. Captured
RADIO538 traffic also contains Group 3A, but that service is intentionally
omitted until the Sony controller demonstrates a dependency on it.

## RDS Runtime Architecture

The encoder and real-time transmitter are deliberately separated:

```text
Icecast metadata
			 |
			 v
submitMetadata()  -- critical-section protected, newest update wins
			 |
			 v
process()         -- sanitizes metadata, updates PS/RT, fills inactive buffer
			 |
			 v
Encoder           -- builds 0A/2A words, CRC checkwords, and 104 MSB-first bits
			 |
			 v
double group buffer
			 |
			 v
timer ISR         -- toggles GPIO5, advances GPIO6, swaps completed buffers
```

`submitMetadata()` immediately copies its inputs, truncates them to RDS limits,
replaces unsupported control characters with spaces, and rejects PTY values
above 31. It is safe to call from the host while RDS transmission is active.

`process()` must run frequently from the Arduino loop. It performs all string
handling and group generation outside the interrupt. The ISR only changes GPIO
levels, advances the bit index, and swaps precomputed buffers. If the inactive
buffer is not ready, the ISR holds the final bit rather than reading incomplete
data.

The RDS service uses hardware timer 1, leaving timer 0 available for
ESP32Radio-V2's 100 ms service timer.

## Metadata Integration

ESP32Radio-V2 submits metadata when it receives or changes:

- `icy-name` becomes the PS station name.
- `StreamTitle` or playlist `EXTINF` text becomes Group 2A RadioText.
- A station change clears the previous title before new metadata arrives.

Minimal standalone use:

```cpp
#include "rds_input.h"

void setup() {
	rds::begin(5, 6);
}

void loop() {
	rds::process();
}

void metadataChanged() {
	rds::submitMetadata("RADIO538", "Artist - Track", 10);
}
```

See [docs/ESP32RADIO_V2_INTEGRATION.md](docs/ESP32RADIO_V2_INTEGRATION.md) for
the exact host integration points and
[docs/RDS_INTERFACE_SPEC.md](docs/RDS_INTERFACE_SPEC.md) for the measured Sony
interface and captured reference values.

## Source Layout

| Path | Responsibility |
| --- | --- |
| `include/rds_encoder.h` | RDS constants, station model, group model, and encoder API |
| `src/rds_encoder.cpp` | Group 0A/2A construction, CRC, segmentation, and scheduling |
| `include/rds_input.h` | Thread-safe metadata service API |
| `src/rds_input.cpp` | Metadata validation, sanitization, and mailbox |
| `src/rds_service.cpp` | PS marquee, double buffering, timer ISR, and GPIO output |
| `test/test_rds.cpp` | Native checks for block syndromes, PI, PS, and RadioText |
| `esp32radio/` | Complete ESP32Radio-V2 host with RDS and S/PDIF integration |

## Build and Test

Build the standalone RDS firmware and run its native encoder test:

```bash
pio run -e esp32-s3-n16r8
pio test -e native
```

Build the complete internet-radio firmware:

```bash
cd esp32radio
pio run -e esp32-s3-n16r8
```

The serial monitor runs at 115200 baud:

```bash
pio device monitor --port COM5 --baud 115200
```

On the tested ESP32-S3, manual ROM download mode may be required before upload:
hold **BOOT**, tap **RESET**, then release **BOOT**. The integrated image uses
DIO flash mode because QIO did not boot reliably on this board.

## Current Limitations

- Group 3A is not generated.
- Alternative Frequency words are fixed RADIO538 capture values.
- Final Sony-side level shifting/source selection remains hardware-specific.
- The implementation assumes the Sony controller accepts the recovered
	clock/data interface without an additional quality/status signal.
