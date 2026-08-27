# ESP32-S3 LC72130 Frequency Synthesizer Emulator

A production-quality ESP-IDF project that emulates the Sanyo LC72130 AM/FM PLL frequency synthesizer for a Sony ST-SA3ES tuner. The ESP32-S3 monitors and decodes commands sent by the Sony controller over the Sanyo CCB serial interface, extracts requested FM frequencies, maintains internal emulated LC72130 state, and optionally drives the D-IN output back to the Sony controller during read/status transactions.

## Overview

The original Sony ST-SA3ES tuner uses an LC72130 integrated circuit for frequency synthesis and tuning control. This project replaces the LC72130 by:

1. **Capturing** the three-wire Sanyo CCB serial interface (CE, CLK, DATA)
2. **Decoding** frequency programming frames and control transactions
3. **Maintaining** an emulated internal state (current frequency, divider, PLL lock status)
4. **Responding** with status on D-IN during read transactions (optional in EMULATOR mode)
5. **Logging** all transactions and state changes over USB serial
6. **Monitoring** live tuner status from a browser over Wi-Fi

## Hardware Wiring

### GPIO Assignments (ESP32-S3)

| Signal | GPIO | Direction | Purpose |
|--------|------|-----------|---------|
| CE | GPIO15 | Input | Chip Enable from Sony controller |
| CLK | GPIO16 | Input | Serial Clock from Sony |
| DATA | GPIO17 | Input | Data input (MOSI) from Sony |
| D-IN | GPIO18 | Output | Status/response to Sony (emulated DO/output) |
| D-IN OE (optional) | GPIO19 | Output | Output-enable for tri-state buffer (future use) |
| AST | GPIO3 | Reserved | Future tuner control |
| ST | GPIO2 | Output | HIGH while Radio ESP reports `PLAYING: [TRUE]` |
| SI | GPIO9 | Reserved | Future tuner control |
| MUTE OUT | GPIO7 | Reserved | Future mute output |
| RDS-D | GPIO11 | Reserved | Future RDS data |
| RDS-C | GPIO12 | Reserved | Future RDS clock |
| AUDIO_RDY | GPIO13 | Reserved | Future audio-ready signal |
| AUDIO_RSET | GPIO14 | Reserved | Future audio-reset signal |
| MUTE IN | GPIO8 | Reserved | Future mute input |
| BLN IN | GPIO10 | Reserved | Future blend input |
| Radio UART TX | GPIO6 | Output | Frequency command to Radio ESP RX |
| Radio UART RX | GPIO5 | Input | Data from Radio ESP TX |

### Level Conversion

**Sony → ESP32 (inputs):**
- CE, CLK, DATA: 5 V logic levels from Sony
- Use a 74LVC245 or equivalent input buffer for safe level conversion to 3.3 V ESP32 levels
- Connect common ground between Sony and ESP32 interface circuits

**ESP32 → Sony (output):**
- D-IN: 3.3 V ESP32 output
- Use a 74AHCT125 buffer to convert 3.3 V to 5 V for Sony compatibility
- Output-enable (OE) is currently assumed tied LOW (always active)
- If future tri-stating is needed, GPIO19 can control OE with small firmware changes

### Schematic Notes

```
Sony Tuner (5V logic)
    |
    +--[74LVC245 In]----> CE (GPIO15)
    +--[74LVC245 In]----> CLK (GPIO16)
    +--[74LVC245 In]----> DATA (GPIO17)
    |
    +--[74AHCT125 Out] <--- D-IN (GPIO18)
    |
   GND (common reference)
    |
   ESP32-S3
```

**CRITICAL SAFETY NOTES:**

1. **Never connect Sony bus signals directly to ESP32 GPIO.** The Sony logic levels (5V) will damage 3.3V GPIO inputs.
2. **VFD Supply (−30V):** The Sony connector also carries a −30V supply for the Vacuum Fluorescent Display. **Never connect this to the ESP32 or level converter circuits.** Use proper isolation or a separate connector for tuning control only.
3. **Common ground is essential** for all logic level conversion to work reliably.

## Operating Modes

The emulator can run in four modes (configured at compile time or runtime):

### 1. PASSIVE_MONITOR (Default)

- Listens to CE, CLK, DATA only
- Decodes all transactions
- **D-IN remains inactive (GPIO18 not driven)**
- Logs all frames and state to serial
- **Safest mode for initial testing**

Use this mode to verify that frames are being captured and decoded correctly without risking any output to the Sony bus.

### 2. EMULATOR

- Full decoding and emulation
- **D-IN is driven with status responses** during read transactions
- Maintains complete internal state (frequency, PLL lock, tuner ready flags)
- Responds to Sony controller queries
- **Requires confidence in protocol and wiring**

### 3. CAPTURE_RAW

- Records timestamped CE, CLK, DATA, and D-IN edge events into a bounded ring buffer
- Exports captured events over serial after each transaction
- Useful for protocol analysis and debugging
- Never disturbs bus timing

### 4. TEST_GENERATOR

- Loads stored bitstreams and runs decoder/protocol tests
- Does not drive physical Sony bus
- Used for unit testing and validation

## Building the Project

### Prerequisites

- ESP-IDF v5.0 or later
- ESP32-S3 development board
- USB cable for UART communication

### Build Steps

```bash
cd esp32-lc72130-emulator
idf.py set-target esp32s3
idf.py menuconfig  # Optional: adjust config
idf.py build
idf.py flash -p /dev/ttyUSB0
idf.py monitor -p /dev/ttyUSB0
```

### Remote Monitor

The firmware connects to the Wi-Fi network configured under `LC72130 network monitor`
in `idf.py menuconfig`. After DHCP completes, the serial log prints the browser URL:

```text
Remote monitor: http://192.168.x.x/
```

The dashboard refreshes every two seconds and reports frequency, PLL and tuner state,
frame and error counts, transactions, read requests, and uptime. The same data is
available as JSON from `/api/status`.

The **Simulate frequency** control creates a valid inbound LC72130 frequency frame,
runs it through the normal decoder and emulator path, and forwards the selected
frequency to the Radio ESP. It can also be invoked directly:

```bash
curl -X POST "http://192.168.x.x/api/test-frequency?mhz=106.00"
```

### Radio ESP Serial Link

UART1 runs at 115200 baud, 8 data bits, no parity, and 1 stop bit. Connect GPIO6
(TX) to the Radio ESP RX, GPIO5 (RX) to the Radio ESP TX, and connect grounds.
Each real or simulated frequency selection sends one newline-terminated ASCII command:

```text
FREQ_MHZ=106.00
```

The Radio ESP must reply over the same UART connection with its own HTTP API
address:

```text
IP: [172.16.5.21]
```

That address is cached in RAM (not persisted) and used to poll the Radio ESP's
now-playing API. It is not required before sending frequency commands, but RDS
metadata and ST/AST GPIO updates stay at their last-known values until an IP has
been received.

The UART pins and baud rate can be changed under `LC72130 network monitor` in
`idf.py menuconfig`.

### Radio Metadata and RDS Injection

Every 2 seconds, the emulator polls the Radio ESP's own HTTP API at
`http://<cached-ip>/api/now-playing` and expects a JSON response:

```json
{
  "station": "RADIO538",
  "now_playing": "Artist - Title",
  "genre": "Pop",
  "bitrate": "128 KBPS",
  "stream_url": "playerservices.streamtheworld.com/...",
  "state": "idle"
}
```

`state` must be `"idle"`, `"search"`, or `"playing"` (a boolean `"playing"` field
is also accepted as a fallback). A failed or unreachable poll is logged and
skipped; RDS output and GPIO state simply hold their last known values until the
next successful poll.

The emulator maps `station` to the eight-character RDS Programme Service field
(group 0A), `now_playing` to the RadioText field (group 2A), and recognized
`genre` values to the RDS PTY code. `now_playing` accepts up to 160 characters:
text of 64 characters or less is repeat-filled (with a gap) to avoid a long
blank stretch, same as before; longer text is broadcast as a continuously
scrolling 64-character window (advancing a couple of characters each poll
cycle, with a gap before it loops) so the full message is still conveyed to
listeners over time, since RDS RadioText itself has a hard 64-character
per-group limit (a 4-bit segment address). A changed `now_playing` value, or a
scroll step, toggles the RadioText A/B flag so receivers refresh their
display. `bitrate` and `stream_url` are retained as diagnostic metadata but
are not broadcast because they do not fit an appropriate listener-facing RDS
field.

`state: "playing"` briefly pulses ST GPIO2 HIGH for 300 ms when a stream is
first found/starts playing, then returns it to LOW; ST does not stay HIGH for
the whole playback duration. `state: "search"` holds AST GPIO3 HIGH continuously
while searching; AST also gets the same 300 ms pulse when a stream starts
playing. `state: "search"` or `"playing"` hold SI GPIO9 HIGH continuously;
`"idle"` or `"error"` hold it LOW. Manual overrides (see below) always take
priority over this automatic behavior.

### Manual GPIO Overrides

The dashboard's **Manual Overrides** section lets ST, AST, SI, and D-IN be
controlled independently of the automatic logic above:

```bash
curl -X POST "http://192.168.x.x/api/control-st?state=on|off"
curl -X POST "http://192.168.x.x/api/control-ast?state=on|off"
curl -X POST "http://192.168.x.x/api/control-si?state=on|off"
curl -X POST "http://192.168.x.x/api/control-din?state=pulse"
```

ST, AST, and SI are simple manual toggles (`on`/`off`) that override the
automatic playback-state-driven behavior above until toggled again. D-IN
normally rests HIGH; the
dashboard's **Normal HIGH → Pulse LOW** button drives it LOW for 200 ms and then
automatically returns it to HIGH. Advanced/API-only values `auto`, `high`, and
`low` remain available on `/api/control-din` for the automatic 100 ms status
response and persistent overrides; forcing D-IN requires `MODE_EMULATOR` and
working D-IN hardware, same as the automatic response. All current states, plus
the live GPIO levels, are visible in `/api/status` (`st_override`,
`ast_override`, `si_override`/`si_output`, `din_override`).

### Raw RDS Decoder

The dashboard's **Raw RDS Groups** table decodes the actual 33-group bitstream
currently being clocked out on GPIO11 (RDS-D) and GPIO12 (RDS-C). It re-parses
the generated bits back into 26-bit blocks and independently recomputes each
block's CRC-10 checkword against the received bits, exactly as an external RDS
decoder listening on those pins would. A `FAIL` in the CRC column means the
decoder could not reconstruct a valid checkword for that block.

```bash
curl "http://192.168.x.x/api/rds-raw"
```

Returns a JSON array of 33 groups: sixteen PS (0A) groups, sixteen RadioText
(2A) groups, and one Clock Time (4A) group, each with the group type, PI code,
raw Block B/C/D hex values, the PS/RadioText address, decoded text segment
(or decoded date/time for the 4A group), and CRC validity. Diagnostic-only
fields `rds_clock_state`, `rds_data_state`, `rds_isr_ticks`, and `rds_running`
are also available on `/api/status` to confirm the RDS timer is actively
toggling both pins.

### RDS Clock Time (Group 4A)

Once Wi-Fi connects, the controller starts an SNTP client (`pool.ntp.org`) to
obtain real UTC time, and broadcasts it once per RDS supercycle (~2.9 seconds)
as Group 4A, encoding the Modified Julian Day and UTC hour/minute, plus a
local UTC offset field, per the RDS specification (IEC 62106 Annex G). Per
spec, the MJD/hour/minute fields always carry **UTC**; compliant receivers
add the offset field themselves to display local time.

The offset is set via `LC72130_RDS_UTC_OFFSET_HALF_HOURS` in `idf.py menuconfig`
(units of 30 minutes; default `4` = **+2:00, Europe/Spain CEST**). Change it to
`2` for CET (+1:00) in winter, or any other time zone's offset.

`/api/status` reports `clock_synced` (whether NTP sync has completed) and
`clock_utc` (current UTC time, or `"not synced"` before the first sync). Until
synced, the 4A group is still transmitted with valid CRCs but encodes an
all-zero placeholder date/time, so external decoders are not misled by a
plausible-looking wrong date. The **Raw RDS Groups** dashboard table decodes
the broadcast UTC time and offset, e.g.
`2026-08-27 20:30 UTC, offset +2:00`.

RDS-D is generated on GPIO11 and RDS-C on GPIO12 at approximately 1187.65 bits/s
(0.0125% above the nominal 1187.5 bits/s). Groups use PI
`0x89FF`, CRC polynomial `0x5B9`, the specified A/B/C/D offset words, and MSB-first
26-bit block serialization. The 32-group cycle interleaves four PS segments with
all sixteen RadioText segments and swaps updated metadata only at a cycle boundary.
This follows the project’s
[Sony ST-SA3ES RDS injection specification](https://github.com/RZomerman/ST-SA3ES-Internet-Streamer-Conversion/blob/main/RDS/Final%20injection%20specification%20and%20Hello%20World%20implementation.md).

Without a connected Radio ESP, apply a sample record directly (bypassing UART and
HTTP polling) and inspect the result in `/api/status`:

```bash
curl -X POST "http://192.168.x.x/api/test-radio-metadata"
curl -X POST "http://192.168.x.x/api/test-playback-state?state=search"
curl "http://192.168.x.x/api/status"
```

**Do not connect GPIO11 or GPIO12 directly while the original SAA6579T remains
connected.** Isolate the original RDS-C/RDS-D source with power removed and drive
the Sony controller side through a verified 3.3 V-to-5 V buffer. Two active sources
must never drive either line. GPIO46 cannot be used for ST on ESP32-S3 because it
is input-only, so ST is assigned to GPIO2.

### Programming Over Wi-Fi (OTA)

The project uses a two-slot OTA partition table (`partitions_ota.csv`, 4 MB flash)
so firmware can be updated over the network after the initial USB flash. Upload a
newly built binary with:

```bash
curl -X POST \
  -H "X-OTA-Token: <your configured token>" \
  --data-binary @build/lc72130_emulator.bin \
  http://192.168.x.x/api/ota
```

The device validates the image, marks the inactive OTA slot as bootable, and
reboots automatically. The upload connection is always reset when the reboot
happens; this is expected and does not indicate failure. Confirm success by
polling `/api/status` until it responds again with a low `uptime_seconds`.

**Set `LC72130_OTA_TOKEN` under `LC72130 network monitor` in `idf.py menuconfig`
to a private value before deploying.** The endpoint accepts any firmware image
from anyone on the Wi-Fi network who supplies the correct token, so treat this
token like a password and do not leave the default value in place.

Because this OTA layout changes the partition table and flash size from the
project's original single-app configuration, the first flash after adopting it
must be done over USB (`idf.py -p /dev/ttyACM0 flash`). Subsequent updates can
use either USB or `/api/ota`.

### Prompt for Building the Radio ESP

Use the following prompt in the workspace where the companion Radio ESP firmware
will be developed. Replace the bracketed values when the target board, audio
hardware, and station URLs are known.

```text
Create production-quality ESP-IDF firmware for an ESP32 that acts as an Internet
radio controlled by a separate ESP32-S3 LC72130 emulator.

Controller interface:
- The controller sends newline-terminated ASCII over UART at 115200 baud, 8N1.
- Controller GPIO6 (TX) connects to this Radio ESP's chosen UART RX pin.
- Controller GPIO5 (RX) connects to this Radio ESP's chosen UART TX pin.
- The boards must share ground. Do not assume the Radio ESP uses the same GPIO
   numbers; select safe pins for [RADIO ESP BOARD MODEL] and expose them in Kconfig.
- The command grammar is exactly: FREQ_MHZ=<frequency>\n
- Example command: FREQ_MHZ=106.00\n
- Input may arrive in partial reads or contain multiple lines in one read. Use a
   bounded receive buffer and line-oriented parser. Reject overlong or malformed
   lines without overflowing buffers or blocking indefinitely.
- Accept FM values from 76.00 through 108.00 MHz in 0.05 MHz steps. Parse into an
   integer channel value in 50 kHz units; do not compare station frequencies as
   floating-point values.
- Repeated commands for the active frequency must be idempotent and must not
   unnecessarily restart the audio stream.
- Reply with `OK FREQ_MHZ=<frequency>\n` after selecting a valid station, or
   `ERR <reason>\n` for malformed, unsupported, or unmapped values.
- After processing a frequency command (success or failure), also reply with
   this Radio ESP's own current IP address so the controller can reach its API:
   `IP: [<this device's IP address>]\n`. Send this once Wi-Fi has an IP; if Wi-Fi
   is not yet connected, send it as soon as it becomes available.

Internet radio behavior:
- Connect to Wi-Fi using SSID and password values configured through ESP-IDF
   Kconfig/menuconfig. Do not hard-code credentials in source files.
- Map frequencies to stream URLs using a compile-time station table. Start with
   these entries and keep the table easy to extend:
   [FREQUENCY] -> [STATION NAME] -> [STREAM URL]
   [FREQUENCY] -> [STATION NAME] -> [STREAM URL]
- On a mapped frequency, stop the previous stream cleanly and play the selected
   HTTP or HTTPS Internet radio stream through [I2S DAC / AUDIO CODEC MODEL].
- Use an established ESP-IDF-compatible audio/streaming library where practical;
   do not hand-roll MP3/AAC decoding. Support the codecs actually required by the
   configured URLs.
- Handle redirects, reconnect after transient network/stream failures with bounded
   backoff, and keep the UART command task responsive while reconnecting or playing.
- For a valid but unmapped frequency, stop playback and return `ERR UNMAPPED`.
- Do not log Wi-Fi passwords or full credentials.

Now-playing API (polled by the controller every 2 seconds, do not push this data
over UART):
- Run an HTTP server and expose `GET /api/now-playing` returning JSON:
   `{"station":"...","now_playing":"...","genre":"...","bitrate":"...",`
   `"stream_url":"...","state":"idle|search|playing"}`.
- `state` must be exactly `idle`, `search`, or `playing`, reflecting whether the
   tuner is idle, currently searching/connecting/buffering, or actively playing.
- Keep this endpoint fast and non-blocking; never block it on network I/O for the
   stream itself. Return the last known values immediately.

Architecture and observability:
- Separate UART parsing, station selection, network connection, audio playback,
   and the now-playing HTTP API into focused modules. Pass station-change
   requests through a FreeRTOS queue.
- Publish current frequency, station name, Wi-Fi state, stream state, last error,
   and uptime through logs and a small read-only `/api/status` JSON endpoint
   (separate from `/api/now-playing`, which the controller polls specifically).
- Persist only configuration that needs to survive reboot. After reboot, wait for
   a frequency command rather than guessing a station.
- Make Wi-Fi or stream failure non-fatal: UART must continue accepting commands.
- Reserve future handshake support for controller GPIO13 `AUDIO_RDY` and GPIO14
   `AUDIO_RSET`, but do not implement or electrically assign those signals until
   their direction, polarity, and timing are specified.

Tests and delivery:
- Add unit tests for fragmented input, multiple commands per read, CRLF handling,
   malformed numbers, overlong lines, range limits, 0.05 MHz step validation,
   unmapped stations, duplicate commands, and queue-full behavior.
- Add tests for the `/api/now-playing` handler covering all three `state` values
   and confirming it never blocks on stream I/O.
- Add a test mode that replaces audio playback with logs so station selection can
   be verified without the audio hardware.
- Document the Radio ESP pin wiring, menuconfig options, station table, supported
   codecs, build/flash commands, UART protocol, the `IP:` reply, the
   `/api/now-playing` JSON shape, and an example response.
- Build the firmware and run focused tests. Do not claim physical audio or UART
   reception is verified unless the target Radio ESP and audio hardware are attached.

Before implementing, inspect the workspace and existing board/audio configuration.
If the board model, audio output hardware, or stream URLs are absent, ask me only
for those missing hardware/content choices, then proceed with the implementation.
```

### Optional: Run Unit Tests

```bash
cd main
gcc -o test_lc72130_decoder test_lc72130_decoder.c lc72130_decoder.c -lm
./test_lc72130_decoder
```

## Serial Output Format

When running, the emulator logs transactions to serial (115200 baud) in this format:

### Frequency Write
```
[timestamp us] LC72130 WRITE: raw=1E 09 2A divider=0x091E frequency=106.00 MHz control=0x2A
```

### Malformed Frame
```
[timestamp us] MALFORMED FRAME: description
```

### Read Request
```
[timestamp us] READ REQUEST detected
```

### D-IN Response Sent
```
[timestamp us] D-IN RESPONSE: 0xXX (bits: 0xBITS)
```

## LC72130 CCB Protocol Summary

### Frame Format

- **Length:** 24 bits (3 bytes) per transaction
- **Bit order:** LSB-first within each byte
- **Clock:** Idles HIGH (CPOL=1)
- **Sampling:** Data is captured on **CLK falling edge** (CPHA=0 equivalent)
- **CE:** Active HIGH

### Transaction Structure

1. CE rises (active)
2. Transmitter (Sony) clocks out 24 bits (8 periods of CLK)
3. CE falls (inactive)
4. ESP32-S3 captures complete 24-bit frame
5. D-IN response (if read) is driven LSB-first, bits transition on CLK rising edges

### Frequency Frame Format

**Byte 0 (low divider)**
- Bits [7:0] of the 16-bit frequency divider

**Byte 1 (high divider)**
- Bits [15:8] of the 16-bit frequency divider

**Byte 2 (control)**
- 0x2A for FM frequency programming (observed in Sony captures)
- Other values for control/configuration frames

### Frequency Calculation

```
divider = (byte1 << 8) | byte0  (LSB-first within bytes)
frequency_MHz = divider × 0.05 − 10.70
```

**Validated examples:**
- 0x091C → 105.90 MHz
- 0x091E → 106.00 MHz
- 0x091F → 106.05 MHz
- 0x0922 → 106.20 MHz
- 0x07B6 → 88.00 MHz

### D-IN Response Byte (Status)

Bit 7: Test/Reserved (0)  
Bit 6: PLL Lock Status (1 = locked, 0 = not locked)  
Bit 5: Tuner Ready (1 = ready, 0 = not ready)  
Bit 4: Input Port 2 state  
Bit 3: Input Port 1 state  
Bit 2: IF-Counter / additional status  
Bit 1: Reserved (0)  
Bit 0: Reserved (0)  

Default emulator response: **0x60** (PLL locked + tuner ready)

## Project Structure

```
esp32-lc72130-emulator/
├── CMakeLists.txt                  # Top-level CMake configuration
├── sdkconfig.defaults              # Default SDK configuration
├── README.md                        # This file
├── main/
│   ├── CMakeLists.txt              # Component-level CMake
│   ├── main.c                      # Application entry point and main loop
│   ├── config.c                    # Configuration management
│   ├── event_log.c                 # ISR-safe logging via FreeRTOS queue
│   ├── lc72130_bus.c               # GPIO ISR and RMT D-IN transmission
│   ├── lc72130_protocol.c          # Transaction classification and framing
│   ├── lc72130_decoder.c           # Frequency decoding and bit manipulation
│   ├── lc72130_emulator.c          # Internal state and response generation
│   ├── test_lc72130_decoder.c      # Host-based unit tests
│   └── include/
│       ├── config.h
│       ├── event_log.h
│       ├── lc72130_bus.h
│       ├── lc72130_protocol.h
│       ├── lc72130_decoder.h
│       └── lc72130_emulator.h
├── docs/
│   ├── PROTOCOL.md                 # Detailed CCB protocol documentation
│   ├── STATE_DIAGRAM.md            # Transaction state machine
│   └── FIELD_DEFINITIONS.md        # LC72130 register and field definitions
└── notes/
    ├── FACTS.md                    # Verified protocol facts
    ├── ASSUMPTIONS.md              # Current assumptions
    └── UNRESOLVED.md               # Open questions and unknowns
```

## Configuration Options

Edit [main/include/config.h](main/include/config.h) to customize:

- **GPIO pins:** `GPIO_CE_INPUT`, `GPIO_CLK_INPUT`, `GPIO_DATA_INPUT`, `GPIO_DIN_OUTPUT`
- **Operating mode:** `DEFAULT_OPERATING_MODE` (PASSIVE_MONITOR, EMULATOR, etc.)
- **Log level:** `DEFAULT_LOG_LEVEL` (NONE, ERROR, WARN, INFO, DEBUG, TRACE)
- **Safety flags:**
  - `FEATURE_DIN_DISABLED_AT_COMPILE_TIME`: Prevent D-IN from ever being driven
  - `FEATURE_TASK_WATCHDOG`: Enable task watchdog timer
- **D-IN response:** `DEFAULT_DIN_RESPONSE` and `FEATURE_DRIVE_DIN_ON_READ`

## Example Session: Tuning to 88.00 MHz

```
[0000000 us] LC72130 WRITE: raw=B6 07 2A divider=0x07B6 frequency=88.00 MHz control=0x2A
[0001234 us] D-IN RESPONSE: 0x60 (PLL locked, tuner ready)
[0005000 us] READ REQUEST detected
[0005100 us] D-IN RESPONSE: 0x60

... (further tuning adjustments if AFC active)

Status Report:
  Frames received:      4 (errors: 0)
  Transactions:         4
  Read requests:        2
  Current frequency:    88.00 MHz
  PLL state:            LOCKED
  Tuner ready:          YES
```

## Example Session: Tuning to 106.00 MHz with AFC Sequence

```
[0000000 us] LC72130 WRITE: raw=1E 09 2A divider=0x091E frequency=106.00 MHz control=0x2A
[0001000 us] D-IN RESPONSE: 0x60
[0050000 us] LC72130 WRITE: raw=1F 09 2A divider=0x091F frequency=106.05 MHz control=0x2A
[0051000 us] D-IN RESPONSE: 0x60
[0100000 us] LC72130 WRITE: raw=22 09 2A divider=0x0922 frequency=106.20 MHz control=0x2A
[0101000 us] D-IN RESPONSE: 0x60
[0150000 us] LC72130 WRITE: raw=1E 09 2A divider=0x091E frequency=106.00 MHz control=0x2A
[0151000 us] D-IN RESPONSE: 0x60

Status Report:
  Tuning pattern: Return to Initial
  Frequency sequence: Initial Select → Small Positive Offset → Large Positive Offset → Negative Offset → Return
```

## Verified Facts

### Confirmed Protocol Details

1. **Bit capture timing:** DATA is sampled on CLK **falling edge** (not rising)
2. **Frame length:** Exactly 24 bits per CE assertion (3 bytes × 8 bits)
3. **Byte order:** LSB-first; byte0 is low divider, byte1 is high divider
4. **Frequency formula:** f = N × 0.05 − 10.70 (where N is the divider)
5. **Common control byte:** 0x2A for FM frequency frames in Sony captures
6. **CE timing:** Sony controller holds CE HIGH for the entire 24-bit transfer (~120 µs @ 200 kHz)

### Hardware Behavior

1. **GPIO stability:** Tested with GPIO15, 16, 17, 18 on ESP32-S3-N16R8
2. **CLK frequency:** Approximately 200 kHz, tolerant of ±30% variations
3. **Read detection:** Requires further investigation; currently assumes all frames are writes
4. **D-IN response timing:** Must be synchronous with CLK rising edges (RMT-driven, not bit-banging)

## Current Assumptions

1. All transactions are write requests (Sony → ESP32)
   - **Needs verification:** Read/status request detection criteria
2. Frequency divider range: 0x0700–0x0B00 (88–108 MHz for FM)
   - **Assumption:** Typical FM band; may need extension for AM band
3. D-IN output enable is always active (OE tied LOW on 74AHCT125)
   - **Alternative:** If tri-stating is required, GPIO19 can control OE
4. Sony controller does not require IF-counter data on read requests
   - **Unknown:** Actual response format for complex read/status operations

## Unresolved Protocol Details

1. **Read request format:** How does Sony distinguish a read from a write? (Possibly via timing, bit pattern, or a separate signal?)
2. **AM band support:** Is the LC72130 also used for AM frequency synthesis? (Divider formula may differ)
3. **IF-counter value:** What format and value does the IC72130 return for the IF-counter status?
4. **Extended response bytes:** Does a read transaction always return 24 bits, or could it be variable length?
5. **AFC/fine-tuning mechanism:** Are the small frequency offsets part of the LC72130 protocol, or does Sony handle them externally?
6. **Startup sequencing:** Are there any startup or initialization transactions required when the tuner powers on?

## Troubleshooting

### No frames captured

1. Check GPIO connections and level conversion
2. Verify CE, CLK, DATA are actually toggling (use oscilloscope or logic analyzer)
3. Ensure common ground between Sony and ESP32 circuits
4. Try CAPTURE_RAW mode to see if any edges are detected

### Frames captured but wrong frequency

1. Check bit order: ensure LSB-first within each byte
2. Verify CLK edge detection: falling edge should sample DATA
3. Test with known frequencies (88.00, 106.00 MHz) from examples
4. Run unit tests: `./test_lc72130_decoder`

### D-IN not responding

1. Verify GPIO18 is configured as output and not disabled at compile time
2. Check mode is set to EMULATOR (not PASSIVE_MONITOR)
3. Verify Sony is actually sending read requests (may need protocol analysis)
4. Check RMT configuration and timing

### Serial output incomplete

1. Increase log level to DEBUG to see buffering operations
2. Use `idf.py monitor` with higher baud rate if available
3. Check event_log_flush() is being called (happens every 100 iterations)

## Future Enhancements

1. **Read request detection:** Implement protocol analysis to detect read vs. write
2. **AM band support:** Decode and support AM frequency ranges
3. **IF-counter emulation:** Return realistic IF-counter values in read responses
4. **Tri-state D-IN output:** Implement GPIO19 output-enable control for bus release
5. **Advanced tuning detection:** Recognize and label AFC sequences, fine-tuning algorithms
6. **Configuration over UART:** Runtime mode/frequency override via serial commands
7. **Web interface (async mode):** Expose state and logs over HTTP if WiFi is available

## Testing Checklist

Before deploying as production replacement:

- [ ] Unit tests pass: `./test_lc72130_decoder`
- [ ] PASSIVE_MONITOR mode logs correct frequencies for known tuning
- [ ] D-IN response byte is correct (0x60 by default)
- [ ] No frame loss at nominal ~200 kHz CLK rate
- [ ] No frame loss with ±30% CLK rate variations
- [ ] Transient tuning sequences correctly classified
- [ ] Event log queue never overflows (depth remains < 256)
- [ ] Firmware remains responsive for >24 hours continuous monitoring
- [ ] Graceful recovery from malformed/short frames

## References

- **LC72130 Datasheet:** Sanyo Semiconductor (search for "LC72130" + "datasheet")
- **Sony ST-SA3ES Service Manual:** Original tuner documentation
- **Sanyo CCB Protocol:** Sanyo serial interface standard (also called "Tuner CCB")
- **Previous reverse engineering:** [sony-ic701-reverse](https://github.com/user/sony-ic701-reverse) project

## License

This project is provided as-is for educational and prototyping purposes. Use at your own risk with proper level conversion and safety measures.

## Author Notes

This emulator was developed to facilitate reverse engineering and repair of vintage Sony tuners. The hardware level conversion is critical for safety; never connect 5V logic directly to the ESP32-S3. The firmware is designed to be maintainable and extensible as more protocol details are uncovered through ongoing capture and analysis.

---

**Last Updated:** 2026-08-26  
**Status:** Initial Release (Passive Monitor Mode Tested)
