# LC72130 Emulator State Machine

## Transaction State Diagram

```
                    ┌─────────────────────────────────┐
                    │                                 │
                    │     WAITING FOR CE RISE         │
                    │                                 │
                    └────────────┬────────────────────┘
                                 │ CE rises (active)
                                 ↓
                    ┌─────────────────────────────────┐
                    │                                 │
                    │   CAPTURING BIT STREAM          │
                    │   (CE = HIGH, CLK toggling)     │
                    │                                 │
                    │ - Initialize frame buffer       │
                    │ - Arm CLK falling-edge ISR      │
                    │ - Capture DATA on CLK fall      │
                    │ - Count bits (0–24)             │
                    │                                 │
                    └────────────┬────────────────────┘
                                 │ CE falls (after 24 bits)
                                 ↓
                    ┌─────────────────────────────────┐
                    │                                 │
                    │   FRAME CAPTURED                │
                    │   Validate & decode             │
                    │                                 │
                    └────────────┬────────────────────┘
                                 │
                    ┌────────────┴──────────────┐
                    │                           │
                    ↓                           ↓
        ┌──────────────────────┐    ┌──────────────────────┐
        │ Valid Frame          │    │ Invalid/Short Frame  │
        │ (24 bits)            │    │ (< 24 bits)          │
        └────────┬─────────────┘    └──────────┬───────────┘
                 │                             │
                 ↓                             ↓
    ┌───────────────────────┐    ┌────────────────────────┐
    │ Decode Transaction    │    │ Increment error count  │
    │ - Extract divider     │    │ - Log malformed frame  │
    │ - Decode frequency    │    │ - Post event           │
    │ - Classify type       │    │ - Return to WAITING    │
    └─────────┬─────────────┘    └────────────────────────┘
              │
              ↓
    ┌───────────────────────────┐
    │ Process Transaction       │
    │ - Update emulator state   │
    │ - Detect patterns         │
    │ - Log to event queue      │
    └─────────┬─────────────────┘
              │
    ┌─────────┴──────────────┐
    │                        │
    ↓                        ↓
┌──────────────┐      ┌──────────────┐
│ Write Frame  │      │ Read Frame   │
│ Frequency or │      │ (Status req) │
│ Control      │      │              │
└──────────────┘      └──────┬───────┘
                             │
                             ↓
                    ┌────────────────────┐
                    │ Generate D-IN      │
                    │ Response           │
                    │ - Encode PLL state │
                    │ - Encode ready bit │
                    │ - Encode I/O bits  │
                    │ - Set D-IN output  │
                    └────────┬───────────┘
                             │
                             ↓
                    ┌────────────────────┐
                    │ Drive D-IN via RMT │
                    │ (LSB-first)        │
                    │ Bits shift on CLK  │
                    │ rising edges       │
                    └────────┬───────────┘
                             │
                             ↓
                    ┌────────────────────┐
                    │ Return to WAITING  │
                    │ for next frame     │
                    └────────────────────┘
```

## Emulator State Variables

### Frequency State
```
struct {
    float current_frequency_mhz;       // Last programmed frequency
    float previous_frequency_mhz;      // Previous frequency
    uint16_t current_divider;          // Last divider value
    uint16_t previous_divider;         // Previous divider
    uint8_t current_control_byte;      // Last control byte (0x2A for FM)
    uint8_t previous_control_byte;     // Previous control byte
} frequency_state;
```

### Status Flags
```
struct {
    bool pll_locked;                   // PLL lock status (bit 6 of D-IN response)
    bool tuner_ready;                  // Tuner ready (bit 5 of D-IN response)
    bool error_flag;                   // Error condition (bit ? of D-IN response)
    uint8_t input_port_1;              // Input port state (bit 3 of D-IN response)
    uint8_t input_port_2;              // Input port state (bit 4 of D-IN response)
    uint16_t if_counter_result;        // IF counter value (bit 2 of D-IN response)
} status_state;
```

### Counters & History
```
struct {
    uint32_t transaction_count;        // Total transactions processed
    uint32_t malformed_frame_count;    // Incomplete/invalid frames
    uint32_t read_request_count;       // Read/status requests
    uint8_t last_din_response_sent;    // Last D-IN byte driven
    uint64_t last_state_change_time;   // Timestamp of last update
} counters_and_history;
```

## PLL Lock Timing

After a frequency write, the emulator assumes the PLL will lock rapidly:

```
Frequency Write (frame 1E 09 2A = 106.00 MHz)
│
├─ t=0 µs:    Sony issues write command
├─ t=120 µs:  Frame transmission complete
├─ t=130 µs:  Emulator decodes and updates state
├─ t=130 µs:  Set pll_locked = true
│
├─ t=150 µs:  Sony sends read/status request
├─ t=150 µs:  Emulator generates D-IN response (PLL locked)
│
└─ [Frequency remains stable until next write]
```

**Current implementation:** PLL is set to locked immediately upon frequency write.

**Real LC72130 behavior:** Would have a brief unlock/relock period; emulator simplification for now.

## Write Transaction Processing

```
Frame Captured: [1E 09 2A]
                  │
                  ├─ byte0 = 0x1E (low divider)
                  ├─ byte1 = 0x09 (high divider)
                  └─ byte2 = 0x2A (control = FM)
                  
Extract Divider: 0x091E = 2334 decimal
                  │
                  ├─ Valid range check: 0x0700–0x0B00 ✓
                  │
Calculate Frequency: 2334 × 0.05 − 10.70 = 106.00 MHz
                  │
                  ├─ Detect tuning pattern (if enabled)
                  │
Update Emulator State:
                  ├─ previous_frequency = current_frequency
                  ├─ current_frequency = 106.00 MHz
                  ├─ current_divider = 0x091E
                  ├─ pll_locked = true
                  └─ transaction_count++
                  
Post Event Log:
                  ├─ EVENT_FRAME_RECEIVED [timestamp, 1E 09 2A, 106.00 MHz]
                  │
Tuning Pattern Detection:
                  ├─ If last_frequency was 88.00 MHz:
                  │  └─ Log: "Pattern: Initial Select (106.00 MHz)"
                  │
                  ├─ If last_frequency was 106.00 MHz:
                  │  └─ Log: "Pattern: No pattern (same frequency)"
                  │
                  └─ [Continue monitoring for multi-frame sequences]
```

## Read Transaction Processing

```
Read Request Detected (CE high, CLK toggle, but no write data?)
                  │
                  ├─ read_request_count++
                  │
Generate D-IN Response:
                  ├─ response_byte = 0x00
                  ├─ if pll_locked:    response_byte |= (1 << 6) → 0x40
                  ├─ if tuner_ready:   response_byte |= (1 << 5) → 0x40 | 0x20 = 0x60
                  ├─ if input_port_1:  response_byte |= (1 << 3)
                  ├─ if input_port_2:  response_byte |= (1 << 4)
                  └─ response_byte = 0x60 (final; PLL locked + ready)
                  
Drive D-IN Output via RMT:
                  ├─ response_byte = 0x60 = 0110 0000 binary
                  ├─ LSB-first transmission:
                  │  ├─ bit 0 = 0 (driven first)
                  │  ├─ bit 1 = 0
                  │  ├─ bit 2 = 0
                  │  ├─ bit 3 = 0
                  │  ├─ bit 4 = 0
                  │  ├─ bit 5 = 1 (tuner ready)
                  │  ├─ bit 6 = 1 (PLL locked)
                  │  └─ bit 7 = 0
                  │
                  └─ Each bit transitions on CLK rising edge
                  
Post Event Log:
                  └─ EVENT_DIN_RESPONSE_SENT [timestamp, 0x60]
```

## Error Recovery

### Malformed Frame (Short or Overlong)

```
CE Falls (before/after bit 24)
│
├─ if bit_count < 24:
│  ├─ status = FRAME_SHORT
│  ├─ error_count++
│  ├─ Log: "MALFORMED FRAME: incomplete"
│  └─ Discard frame
│
└─ if bit_count > 24:
   ├─ status = FRAME_OVERLONG
   ├─ error_count++
   ├─ Log: "MALFORMED FRAME: overlong"
   └─ Discard frame

After Error:
├─ Clear bit buffer
├─ Reset capture state
└─ Return to WAITING for CE
```

### Clock Edge Mismatch

If CLK appears to have unexpected edge timing:

```
ClkEdge Detection:
├─ Measure time between CLK edges
├─ Compare against median period
├─ If variance > 50%:
│  ├─ status = FRAME_CLK_ERROR
│  ├─ error_count++
│  ├─ Log: "CLK timing error"
│  └─ Reset and continue (may recover on next frame)
│
└─ If variance < 50%: accept bit
```

### Transaction Timeout

If CE remains high for >1 second (TRANSACTION_TIMEOUT_MS):

```
Timeout Condition:
├─ error_count++
├─ Log: "Transaction timeout"
├─ Clear all buffers
├─ Reset capture state
└─ Return to WAITING
```

---

**Last Updated:** 2026-08-26
