# LC72130 Sanyo CCB Serial Protocol

## Overview

The LC72130 frequency synthesizer communicates with the Sony ST-SA3ES controller over a three-wire Sanyo CCB (Common Channel Bus) serial interface. This document describes the protocol in detail based on reverse engineering of captured transactions.

## Physical Interface

### Signals

| Signal | Direction | Level | Purpose |
|--------|-----------|-------|---------|
| CE | Input | 5V (Sony) | Chip Enable; active HIGH |
| CLK | Input | 5V (Sony) | Serial Clock; idles HIGH |
| DATA | Input | 5V (Sony) | Data input (MOSI); idle HIGH |
| D-IN | Output | 5V (Sony) | Data output (MISO/DO); status/response |

All signals require proper level conversion for ESP32-S3 (3.3V):
- Inputs: Use 74LVC245 or similar buffer
- Outputs: Use 74AHCT125 or similar driver

## Transaction Timing

### Write Transaction (Sony → LC72130)

```
         CE (active HIGH)
         ┌────────────────────┐
         │                    │
     ────┘                    └──────
     
         CLK (8 periods)
         ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐
     ────┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─────
     
         DATA (samples on falling CLK)
         ┌─────┐       ┌─────┐ 
     ────┤ b0  ├─┬─┬─┬─┤ b1  ├─...
         └─────┘ │ │ │ └─────┘
                  ↓ ↓ ↓ (capture points)
     
     Bit extraction:
     - CLK cycle 0: capture DATA → bit 0 of byte 0
     - CLK cycle 1: capture DATA → bit 1 of byte 0
     - ...
     - CLK cycle 7: capture DATA → bit 7 of byte 0
     - CLK cycle 8: capture DATA → bit 0 of byte 1
     - ...
```

**Timing:**
- CLK frequency: ~200 kHz nominal (5 µs per cycle)
- CE assertion duration: ~120 µs (covers 24 bits @ 200 kHz)
- Setup/hold times: Minimal; data stable during falling edge

**Bit Capture:**
- Sampling occurs on **CLK falling edge**
- DATA bit value is read at that moment
- Bit order within byte: LSB-first (bit 0 is transmitted first)
- Byte order: byte0 first, then byte1, then byte2

### Read Transaction (LC72130 → Sony)

```
         CE (active HIGH)
         ┌────────────────────┐
         │                    │
     ────┘                    └──────
     
         CLK (8 periods)
         ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐ ┌─┐
     ────┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─┘ └─────
     
         D-IN (driven by LC72130/emulator)
         ┌─────┐       ┌─────┐
     ────┤ b0  ├─┬─┬─┬─┤ b1  ├─...
         └─────┘ │ │ │ └─────┘
                  ↑ ↑ ↑ (driven on CLK rising edge)
     
     Response:
     - Same bit order as write (LSB-first within each byte)
     - Emulator must drive D-IN before or coincident with CLK rising edge
     - Sony samples D-IN during the CLK high period
```

## Frame Format

### 24-Bit Frame Structure

```
Byte 0: [b7 b6 b5 b4 b3 b2 b1 b0] - Low divider
Byte 1: [b7 b6 b5 b4 b3 b2 b1 b0] - High divider
Byte 2: [b7 b6 b5 b4 b3 b2 b1 b0] - Control/mode
```

**Combined divider value (16-bit):**
```
divider_16bit = (byte1 << 8) | byte0
```

### Known Frame Types

#### Type 1: FM Frequency Write
```
Byte 0: Low frequency divider (0x00–0xFF)
Byte 1: High frequency divider (0x07–0x0B for FM band)
Byte 2: 0x2A (control byte; observed in Sony captures)
        
Example: 1E 09 2A
  divider = 0x091E = 2334 decimal
  frequency = 2334 × 0.05 − 10.70 = 106.00 MHz
```

#### Type 2: Control/Configuration
```
Byte 0: Address or register ID
Byte 1: Data/parameter
Byte 2: Control bits (e.g., 0xE3, 0x4C, 0x88 observed in captures)
        
Note: Exact meaning unknown; requires datasheet or further reverse engineering.
```

#### Type 3: IF-Counter / Status Read
```
Byte 0–2: Format unknown
        
Note: Would appear as a read request during a read transaction.
```

## Frequency Calculation

### Formula

```
divider = (byte1 << 8) | byte0
frequency_MHz = divider × 0.05 − 10.70
```

### Inverse (Encoding)

```
frequency_MHz = desired frequency in MHz
divider = round((frequency_MHz + 10.70) / 0.05)
byte0 = divider & 0xFF
byte1 = (divider >> 8) & 0xFF
```

### Validation Examples

| Divider | Bytes | Frequency |
|---------|-------|-----------|
| 0x091C | 1C 09 2A | 105.90 MHz |
| 0x091E | 1E 09 2A | 106.00 MHz |
| 0x091F | 1F 09 2A | 106.05 MHz |
| 0x0922 | 22 09 2A | 106.20 MHz |
| 0x07B6 | B6 07 2A | 88.00 MHz |

**Frequency range:** 88.00–108.35 MHz (FM band)
- Minimum divider: 0x07B0 (88.00 MHz)
- Maximum divider: 0x0B0E (108.35 MHz)

## D-IN Status Response Byte

When the Sony controller performs a read/status transaction, the LC72130 (or emulator) drives D-IN with an 8-bit status byte:

```
Bit 7: Test / Reserved (always 0)
Bit 6: PLL Lock Status (1 = locked, 0 = not locked)
Bit 5: Tuner Ready (1 = ready, 0 = not ready)
Bit 4: Input Port 2 (GPIO or feature bit; typically 0)
Bit 3: Input Port 1 (GPIO or feature bit; typically 0)
Bit 2: IF-Counter / Additional Status (configurable; typically 0)
Bit 1: Reserved (always 0)
Bit 0: Reserved (always 0)
```

### Default Response

```
0x60 = 0110 0000 binary
Bit 6: 1 (PLL locked)
Bit 5: 1 (tuner ready)
All others: 0
```

This response indicates:
- PLL is in lock (frequency synchronized)
- Tuner is ready for commands
- No error or status indication

### Alternative Responses

```
0x40 = 0100 0000  (PLL not locked, tuner ready)
0x20 = 0010 0000  (PLL locked, tuner not ready)
0x00 = 0000 0000  (All bits off; typically seen on error)
```

## Transaction Sequences

### Initialization / Startup

```
1. Sony sends 0x?? ?? ?? (initialization frame; exact format TBD)
2. LC72130 returns status on read
3. Sony sends 0x7B 07 2A (tune to 88.00 MHz)
4. Sony sends read request
5. LC72130/emulator returns 0x60 (locked and ready)
```

### Frequency Tuning

```
1. Sony sends 0xNN NN 2A (tune to new frequency)
2. Sony sends read request (status check)
3. LC72130 returns status (may have PLL lock delay)
   - If not locked yet: 0x40 (PLL unlocked but ready)
   - After lock achieved: 0x60 (PLL locked)
4. If not locked, Sony may retry with same or adjusted frequency
5. Once locked, Sony can proceed or tune again
```

### AFC / Fine-Tuning Sequence (Observed)

```
1. Sony sends frequency = 106.00 MHz (initial)
   Frame: 1E 09 2A → divider 0x091E
2. [50 ms pause]
3. Sony sends frequency = 106.05 MHz (small positive offset)
   Frame: 1F 09 2A → divider 0x091F
4. [50 ms pause]
5. Sony sends frequency = 106.20 MHz (larger positive offset)
   Frame: 22 09 2A → divider 0x0922
6. [50 ms pause]
7. Sony sends frequency = 106.00 MHz (return to initial)
   Frame: 1E 09 2A → divider 0x091E

Note: This sequence may indicate AFC (automatic frequency control) or Sony's internal
      fine-tuning algorithm. It is not performed by the LC72130 itself.
```

## Clock Tolerance

The CCB clock frequency is approximately 200 kHz but can vary due to:
- Crystal tolerance (±50 ppm typical)
- Temperature variations
- Component aging

The emulator must tolerate CLK variations of at least ±30% around nominal without frame loss or misalignment.

**Recommended implementation:** Time between CLK edges and reject if variance exceeds 50% of median period.

## Read vs. Write Detection

**Current assumption:** All transactions are writes (Sony → ESP32).

**Unknown criteria for distinguishing reads:**
- Different bit pattern in frame?
- Timing difference in CE or CLK edges?
- Separate read-enable signal (not yet observed)?
- Timing relationship between CE rising and first CLK pulse?

**To implement:** Capture and analyze transactions where Sony expects a response on D-IN.

## Open Questions

1. What is the exact format of control frames (byte2 ≠ 0x2A)?
2. Does the LC72130 support AM band tuning? (If so, divider formula may differ)
3. How does Sony detect whether a transaction is read or write?
4. What IF-counter value should the emulator return?
5. Are there extended-length transactions (>24 bits)?
6. What happens if the emulator does not respond (D-IN stays high/open)?
7. Is there a startup/reset sequence required after power-on?

## Comparison with Standard SPI

The CCB protocol is **similar but not identical** to SPI:

| Aspect | SPI | CCB |
|--------|-----|-----|
| Bit order | Configurable | Always LSB-first within byte |
| Clock idle | CPOL=0 (LOW) or 1 (HIGH) | Always HIGH (CPOL=1) |
| Sampling edge | CPHA=0 (rising) or 1 (falling) | Falling (CPHA=1) |
| Frame length | Flexible | Fixed 24 bits per transaction |
| Chip select | Active LOW | Active HIGH (CE) |
| Bidirectional | Yes (MOSI + MISO) | Separate: DATA (write), D-IN (read) |

**Key difference:** SPI assumes simultaneous transmit/receive within a byte; CCB separates the directions entirely (write transaction vs. read transaction).

## Implementation Notes for Emulator

1. **ISR-driven capture:** Use GPIO edge interrupts to capture on CLK falling edge
2. **Bit buffer:** Accumulate 24 bits in a ring buffer
3. **Frame boundary:** Detect CE falling edge to mark end of transaction
4. **Validation:** Reject frames with <24 or >24 bits
5. **D-IN response:** Must be driven deterministically (RMT or bit-banging with precise timing)
6. **Logging:** Use deferred queue-based logging to avoid ISR blocking
7. **Watchdog recovery:** Detect transaction timeout and reset capture state if needed

---

**Last Updated:** 2026-08-26
