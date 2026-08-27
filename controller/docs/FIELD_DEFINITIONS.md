# LC72130 Register and Field Definitions

## Frequency Frame (Write)

### Format
```
Byte 0: [b7 b6 b5 b4 b3 b2 b1 b0]  Low Frequency Divider
Byte 1: [b7 b6 b5 b4 b3 b2 b1 b0]  High Frequency Divider
Byte 2: [b7 b6 b5 b4 b3 b2 b1 b0]  Control Byte
```

### Byte 0: Low Frequency Divider
```
[7] [6] [5] [4] [3] [2] [1] [0]
 |   |   |   |   |   |   |   |
 └───────────────────────────────  Frequency Divider (low byte)
                                    Range: 0x00–0xFF
                                    Combined with byte1 to form 16-bit divider
```

### Byte 1: High Frequency Divider
```
[7] [6] [5] [4] [3] [2] [1] [0]
 |   |   |   |   |   |   |   |
 └───────────────────────────────  Frequency Divider (high byte)
                                    Range: 0x00–0xFF
                                    Combined with byte0 to form 16-bit divider
                                    Typical FM: 0x07–0x0B
```

### Byte 2: Control Byte
```
[7] [6] [5] [4] [3] [2] [1] [0]
 |   |   |   |   |   |   |   |
 |   └───────────────────────────  Known Values:
 |       0x2A = FM Frequency Write (Sony ST-SA3ES)
 |       0x54 = Alternative Frequency Write (sigrok captures)
 |       0xE3 = Control/Configuration (user memory)
 |       0x4C = Control/Configuration (user memory)
 |       0x88 = Control/Configuration (user memory)
 |
 └───────────────────────────────  Unknown/Reserved Bits
```

## Frequency Calculation

### Formula
```
divider_16 = (byte1 << 8) | byte0
f_MHz = divider_16 × 0.05 − 10.70
```

### Divider Ranges
```
FM Band (88.00–108.35 MHz):
  Minimum divider: 0x07B0 (88.00 MHz)
  Maximum divider: 0x0B0E (108.35 MHz)
  Typical Sony writes: 0x0700–0x0B00 range

AM Band (unknown):
  May use different divider formula (not yet confirmed)
  Not observed in Sony ST-SA3ES captures
```

### Verified Examples
| Bytes | Divider | Frequency |
|-------|---------|-----------|
| 1C 09 2A | 0x091C | 105.90 MHz |
| 1E 09 2A | 0x091E | 106.00 MHz |
| 1F 09 2A | 0x091F | 106.05 MHz |
| 22 09 2A | 0x0922 | 106.20 MHz |
| B6 07 2A | 0x07B6 | 88.00 MHz |

## Status Frame (Read Response)

### Format
```
D-IN Output Byte (from LC72130 to Sony during read transaction)

[7] [6] [5] [4] [3] [2] [1] [0]
 |   |   |   |   |   |   |   |
 0   PLL READY Port2 Port1 IF  0  0
     Lock Ready      GPIO1 Counter
```

### Bit Definitions

#### Bit 7: Test / Reserved
```
Value: Always 0
Purpose: Reserved for test modes (not used in normal operation)
Sony Behavior: Typically ignores this bit
```

#### Bit 6: PLL Lock Status
```
Name: PLL_LOCK (Bit 6)
Value: 1 = Locked, 0 = Not Locked
Purpose: Indicates whether the PLL has achieved frequency lock
Timing: Should be 0 during tuning, 1 after lock achieved (~100–500 µs)
Sony Behavior: Polls this bit to verify frequency is stable before audio
Default (Emulator): 1 (assumed locked)
```

#### Bit 5: Tuner Ready
```
Name: TUNER_READY (Bit 5)
Value: 1 = Ready, 0 = Not Ready
Purpose: Indicates the synthesizer is ready to accept new frequency commands
Timing: Typically 1 during normal operation; 0 during startup or error
Sony Behavior: May delay sending commands until tuner reports ready
Default (Emulator): 1 (ready)
```

#### Bit 4: Input Port 2
```
Name: INPUT_PORT_2 (Bit 4)
Value: Configurable (typically 0)
Purpose: May indicate GPIO input state, feature presence, or status
Meaning: Unknown; likely used for feature detection or band switching
Sony Behavior: May read this to detect LC72130 variant or configuration
Default (Emulator): 0 (not used)
Configuration: Settable via lc72130_emulator_set_input_ports()
```

#### Bit 3: Input Port 1
```
Name: INPUT_PORT_1 (Bit 3)
Value: Configurable (typically 0)
Purpose: May indicate GPIO input state, feature presence, or status
Meaning: Unknown; possibly AM/FM band select or tuning mode
Sony Behavior: May read this during initialization or band switching
Default (Emulator): 0 (not used)
Configuration: Settable via lc72130_emulator_set_input_ports()
```

#### Bit 2: IF-Counter / Status
```
Name: IF_COUNTER or STATUS_BIT (Bit 2)
Value: Configurable (typically 0)
Purpose: May indicate IF-counter value, signal lock, or additional status
Meaning: Unknown; could be:
         - LSB of IF-counter result
         - Additional lock indicator (AFC lock vs PLL lock)
         - Status flag for specific circuit condition
Sony Behavior: May read this for advanced tuning or diagnostics
Default (Emulator): 0 (not used)
Configuration: Derived from lc72130_emulator_set_if_counter()
```

#### Bit 1: Reserved
```
Value: Always 0
Purpose: Reserved for future use
Sony Behavior: Typically ignores this bit
```

#### Bit 0: Reserved
```
Value: Always 0
Purpose: Reserved for future use
Sony Behavior: Typically ignores this bit
```

### Response Byte Values

#### Standard Response (0x60)
```
Binary: 0110 0000
Hex:    0x60
Bits:   Bit6=1 (PLL locked) + Bit5=1 (tuner ready)
Meaning: Tuner is locked and ready for commands
Status: Normal operation
```

#### Unlocked (0x40)
```
Binary: 0100 0000
Hex:    0x40
Bits:   Bit6=0 (PLL not locked) + Bit5=1 (tuner ready)
Meaning: Tuner is ready but frequency not yet locked
Status: Transient (during frequency tuning)
Timeout: Should return to 0x60 within ~500 µs
```

#### Not Ready (0x20)
```
Binary: 0010 0000
Hex:    0x20
Bits:   Bit6=1 (PLL locked) + Bit5=0 (tuner not ready)
Meaning: Frequency is locked but tuner cannot accept new commands
Status: Unusual; may indicate internal error or initialization
Recovery: Typically requires reset
```

#### Error (0x00)
```
Binary: 0000 0000
Hex:    0x00
Bits:   All status bits off
Meaning: No status available or error condition
Status: Indicates problem or no response
Sony Behavior: May trigger fault state or retry
```

## Control Frames (Non-Frequency Writes)

### Known Control Values

#### Control Byte 0xE3 (User Memory)
```
Byte: [E3]
Context: Observed in sigrok captures as part of 3-byte control sequence
Frame: E3 4C 88 (typical sequence from user memory notes)
Purpose: Unknown; possibly:
         - Configuration register
         - Test mode
         - Band/mode selection
Status: Needs datasheet verification
```

#### Control Byte 0x4C (User Memory)
```
Byte: [4C]
Context: Observed as middle byte of E3 4C 88 sequence
Purpose: Unknown; part of control transaction
Status: Needs datasheet verification
```

#### Control Byte 0x88 (User Memory)
```
Byte: [88]
Context: Observed as part of E3 4C 88 sequence
Purpose: Unknown; may indicate end of control frame or specific command
Status: Needs datasheet verification
```

### Control Frame Structure
```
Byte 0: Address or parameter ID
Byte 1: Data or sub-parameter
Byte 2: Control byte or checksum

Typical Sony sequence (needs verification):
  - Control frame 1: E3 ?? ??
  - Control frame 2: 4C ?? ??
  - Control frame 3: 88 ?? ??

Or possibly a single 3-byte control transaction:
  - All three bytes transmitted as one frame: E3 4C 88
```

## Frequency Division Steps

### Resolution
```
Step size: 0.05 MHz
Range: 88.00–108.35 MHz
Total steps: (0x0B0E − 0x07B0) / 1 ≈ 1022 steps
```

### Common Frequencies
```
88.00 MHz: divider = 0x07B6
88.05 MHz: divider = 0x07B7
88.10 MHz: divider = 0x07B8
...
105.90 MHz: divider = 0x091C
106.00 MHz: divider = 0x091E
106.05 MHz: divider = 0x091F
106.10 MHz: divider = 0x0920
106.15 MHz: divider = 0x0921
106.20 MHz: divider = 0x0922
...
107.90 MHz: divider = 0x0950
108.00 MHz: divider = 0x0952
108.35 MHz: divider = 0x095C
```

## Unresolved Fields

### Bit 4: Input Port 2
- **Status:** Unknown
- **Observed values:** Always 0 in Sony writes
- **Hypothesis:** May be:
  - GPIO input from Sony controller (band select)
  - Feature bit (IC variant detection)
  - Antenna tuning indicator
- **Required:** Datasheet or protocol capture showing bit transitions

### Bit 3: Input Port 1
- **Status:** Unknown
- **Observed values:** Always 0 in Sony writes
- **Hypothesis:** May be:
  - GPIO input (tuning mode, AFC enable, etc.)
  - Feature bit
  - Filter selection
- **Required:** Datasheet or protocol capture showing bit transitions

### Bit 2: IF-Counter / Status
- **Status:** Unknown meaning
- **Observed values:** Always 0 in Sony reads (assumed)
- **Hypothesis:** May be:
  - Single bit of IF-counter result (lower 8 bits in byte2 of read response)
  - AFC lock indicator (separate from PLL lock)
  - AGC level
- **Required:** Datasheet or IF-counter capture data

### 0xE3, 0x4C, 0x88 Control Bytes
- **Status:** Unknown purpose
- **Context:** Observed in user's earlier sigrok captures as repeated 3-byte sequence
- **Hypothesis:** May be:
  - Initialization sequence
  - Band/mode selection (AM vs FM)
  - Sensitivity or AGC configuration
  - Test or calibration commands
- **Required:** Datasheet, or correlation with Sony controller behavior

## Implementation Guidance

### Minimum Emulation (Current)
```
1. Decode frequency from bytes 0–1
2. Calculate frequency using divider formula
3. Maintain current/previous frequency state
4. Respond to read with 0x60 (locked + ready)
5. Log all transactions
```

### Enhanced Emulation (Future)
```
1. Implement PLL lock timing (unlock on frequency write, relock after 100–500 µs)
2. Detect and respond to control frames
3. Implement input port detection (bits 3–4)
4. Return realistic IF-counter values
5. Simulate AM band if supported
```

### Complete Emulation (With Datasheet)
```
1. All above
2. Implement register read/write if multi-byte commands are used
3. Proper clock and frequency correction loops
4. Error handling and timeout detection
5. Any undocumented but critical behavior from datasheet
```

---

**Last Updated:** 2026-08-26
**Verification Status:** Frequency frame (0x2A) validated against 5 examples. Control frames TBD.
