# Sony ST-SA3ES Internet Radio and RDS Emulator

This project turns an ESP32-S3 N16R8 into an internet-radio and RDS source for
the Sony ST-SA3ES tuner. The integrated firmware reads the frequency selected
by the original Sony controller, maps it to an internet stream, decodes AAC,
sends digital audio to a TOSLINK transmitter, and returns station and track
metadata through the recovered RDS clock/data interface.

> **Important RDS boundary:** this firmware does not synthesize an FM signal or
> a 57 kHz RDS subcarrier. It replaces the post-demodulator clock and data
> outputs normally supplied by the SAA6579T RDS decoder.

## Program Overview

The firmware has four main paths:

1. **Tuner control:** a core-0 task captures 24-bit CN701 transactions and maps
	 the requested FM frequency through the station reference table.
2. **Internet radio:** core 1 runs ESP32Radio-V2, follows stream redirects,
	 receives AAC audio, and extracts Icecast station/title metadata.
3. **Digital audio:** Helix decodes AAC to stereo PCM. The software S/PDIF
	 encoder generates a digital stream on GPIO7 for a powered TOSLINK optical
	 transmitter module.
4. **RDS emulation:** station and title updates are converted into valid RDS
	 Group 0A and Group 2A messages. A hardware timer transmits their 104 bits on
	 GPIO6, synchronized to the 1187.507 Hz clock on GPIO5.

Local selector, push-button, rotary, IR, and display inputs are disabled. The
Sony front panel remains the user-facing control and display. Playback starts
only when the selected Sony frequency has a station-map entry.

## Connections

| ESP32-S3 pin | Signal | Destination | Purpose |
| --- | --- | --- | --- |
| GPIO5 | RDS-C | Sony controller RDS clock input | 1187.507 Hz recovered RDS clock |
| GPIO6 | RDS-D | Sony controller RDS data input | Synchronous RDS group bits |
| GPIO7 | S/PDIF | Powered TOSLINK module data input | Digital stereo audio |
| GPIO16 | CE | CN701 pin 9 | Active-high transaction boundary at C701 |
| GPIO15 | CLK | CN701 pin 6 | 24 serial clocks per transaction |
| GPIO17 | D-IN | CN701 pin 8 | Held high |
| GPIO18 | DATA | CN701 pin 7 | LSB-first Sony tuning commands |
| GPIO8 | MUTE | Sony mute control | High unless a stream is playing |
| GPIO3 | AST | Sony status input | High when frequency is mapped |
| GPIO2 | ST | Sony stereo status input | High when stream is playing |
| GPIO9 | SIG | Sony signal status input | High when frequency is mapped |
| GPIO40 | Controller UART RX | Controller GPIO6 (TX) | Receives `FREQ_MHZ=<frequency>` at 115200 8N1 |
| GPIO41 | Controller UART TX | Controller GPIO5 (RX) | Sends `OK` and `ERR` replies at 115200 8N1 |
| GPIO42 | AUDIO_RDY | Controller ready input | Low during boot; high after idle service and UART status are ready |
| GND | Ground | Sony and TOSLINK module ground | Common reference |

The ESP32-S3 uses 3.3 V logic while the measured Sony RDS logic-high level is
approximately 4.8 V. Isolate the original SAA6579T output and use a suitable
3.3 V-to-5 V buffer or source selector. Never drive the Sony bus in parallel
with an active SAA6579T output. A bare optical socket is not sufficient for
GPIO7; use a powered TOSLINK transmitter circuit with compatible input logic.

GPIO15 cannot be both CLK and S/PDIF, so TOSLINK moved to GPIO7. The requested
GPIO46 cannot drive ST because it is input-only on ESP32-S3; ST therefore uses
GPIO2.

## Station Map and Controller UART

Open `http://<radio-ip>/stations` to edit up to 16 mappings. Each row contains
the source frequency, station name, and stream URL. Saving writes the map to
NVS, so it survives resets without a separate SPIFFS upload.

The controller UART uses GPIO40 for RX and GPIO41 for TX by default. These pins
can be changed through `CONFIG_RADIO_CONTROLLER_UART_RX_GPIO` and
`CONFIG_RADIO_CONTROLLER_UART_TX_GPIO`. Connect controller GPIO6 (TX) to radio
GPIO40 (RX), controller GPIO5 (RX) to radio GPIO41 (TX), and connect grounds.
Connect radio GPIO42 to the controller's `AUDIO_RDY` input. Add a 10 kOhm
pulldown from `AUDIO_RDY` to ground so it remains low during reset and ROM boot,
before the application configures GPIO42 as an output.

After initialization, the radio sends a complete status snapshot, flushes the
UART, and then raises `AUDIO_RDY`. Later field changes are sent once as
newline-terminated ASCII:

```text
STATION: RADIO538
NOW_PLAYING: Artist - Title
GENRE: Pop
BITRATE: 128 KBPS
STREAM_URL: playerservices.streamtheworld.com/...
PLAYING: TRUE
```

Empty values are sent to clear stale controller fields. `PLAYING: IDLE` means
no station is selected, `PLAYING: SEARCH` means a mapped URL is selected but
audio is not streaming yet, and `PLAYING: TRUE` means audio is playing. The
metadata values are sanitized to remain one line. GPIO42 can be changed through
`CONFIG_RADIO_CONTROLLER_AUDIO_RDY_GPIO`.

Whenever a valid `FREQ_MHZ=` command is received, the radio immediately sends
its current IP address, before the two-second pending window applies:

```text
IP: 172.16.5.21
```

Commands use `FREQ_MHZ=106.00` followed by a newline. Frequencies must be from
76.00 through 108.00 MHz on 0.05 MHz channels. A valid command becomes active
only after remaining unchanged for two seconds. The radio returns
`OK FREQ_MHZ=106.00`, or `ERR` with a reason when the command is invalid,
superseded, pending, or has no station mapping.

## Sony Tuner Control

While CE is high, the SPI2 peripheral captures DATA over 24 CLK edges without
per-edge CPU interrupts. Bits are LSB-first inside each byte. Bytes 0 and 1
form a little-endian PLL divider; byte 2 is retained as the control byte. The
selected frequency is:

```text
RF_kHz = (divider * 50) - 10800
```

Only complete frames in the `87.50..108.00 MHz` range are accepted. Duplicate
frequencies and the observed invalid fixed follow-up words do not restart the
stream. See [docs/TUNER_CONTROL.md](docs/TUNER_CONTROL.md) for the state machine,
output truth table, and wiring notes.

The reference station list is
[esp32radio/src/station_map.cpp](esp32radio/src/station_map.cpp):

| Sony frequency | Internet station |
| ---: | --- |
| 90.00 MHz | RADIO538 |
| 91.00 MHz | BNR Nieuwsradio |

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
| `esp32radio/src/station_map.cpp` | Frequency-to-name/URL reference table |
| `esp32radio/src/sony_tuner_control.cpp` | CN701 SPI2 capture, decoder, core-0 task, and status outputs |

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
- CN701 D-IN is sampled on the rising CLK edge; verify this edge against the
	final level-shifted hardware capture.
