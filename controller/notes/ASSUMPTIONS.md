# Current Assumptions about LC72130 & Sony ST-SA3ES

This document lists working assumptions that have NOT yet been independently verified. These guide implementation but should be re-evaluated as more data is collected.

## Frequency Programming Assumptions

### 1. All Captured Frames Are Write Transactions
**Status:** ASSUMPTION (not yet verified)
**Reason:** No observed difference in captured frame structure to distinguish reads
**Impact:** D-IN response not yet tested; read transaction detection not implemented
**To verify:** 
- Capture Sony reading status mid-tuning
- Analyze CLK timing or frame content for read markers
- Compare D-IN activity during suspected reads vs. writes

---

### 2. Frequency Divider Range Limits FM Band Only
**Status:** ASSUMPTION
**Reason:** All observed captures are in 88–108 MHz range; no AM captures yet
**Impact:** May reject valid AM frequencies if they use same IC
**Range assumed:**
```
Minimum: 0x07B0 (88.00 MHz)
Maximum: 0x0B0E (108.35 MHz)
```
**To verify:**
- Capture Sony tuning to AM band (if LC72130 supports it)
- Check if divider formula differs for AM
- Consult LC72130 datasheet for AM capability

---

### 3. Byte 2 = 0x2A Indicates FM Frequency Frame
**Status:** ASSUMPTION (partial verification)
**Reason:** All Sony FM writes observed with 0x2A; no counter-examples in Sony captures
**Impact:** Control frames (0xE3, 0x4C, 0x88) not yet decoded
**Limitation:** Other Sony models or IC variants may use different control bytes
**To verify:**
- Capture Sony with alternative modules
- Consult LC72130 datasheet for control byte definitions
- Test if changing byte2 affects Sony behavior

---

### 4. Byte 2 Controls Are Not Critical for Basic FM Tuning
**Status:** ASSUMPTION
**Reason:** Pattern 0x2A repeats for all FM writes; no variation observed
**Impact:** Control frames (0xE3, etc.) are logged but not decoded
**To verify:**
- Analyze control frame sequences in captures
- Test emulator response if Sony varies byte2
- Document exact meaning of each control byte value

---

## PLL Lock Timing Assumptions

### 5. PLL Locks Immediately After Frequency Write
**Status:** ASSUMPTION (oversimplification)
**Reason:** Simplifies emulation; assumes fast lock time
**Impact:** Emulator reports PLL locked instantly; real IC may take 100–500 µs
**Real behavior:** LC72130 should unlock during frequency change, then relock
**To verify:**
- Capture real LC72130 D-IN during tuning
- Measure lock/unlock timing
- Implement timing-accurate PLL state machine

---

### 6. Sony Does Not Require IF-Counter Values
**Status:** ASSUMPTION
**Reason:** No observed Sony behavior dependent on IF-counter bits
**Impact:** Emulator returns 0 for IF-counter status
**D-IN response:** 0x60 (only PLL lock + tuner ready bits used)
**To verify:**
- Capture Sony behavior with IF-counter variations
- Monitor if Sony changes tuning based on IF response
- Consult datasheet for IF-counter purpose

---

## Status Bit Assumptions

### 7. Default PLL Lock Status = Always Locked
**Status:** ASSUMPTION
**Reason:** Simplifies operation; assumes Sony expects locked state
**Impact:** Emulator always reports bit 6 = 1
**Real behavior:** Should reflect actual frequency lock status
**To verify:**
- Capture Sony behavior during lock/unlock transitions
- Test if Sony changes behavior based on lock bit
- Implement proper PLL state machine

---

### 8. Input Port Bits (Bits 3–4) Are Always 0
**Status:** ASSUMPTION
**Reason:** Purpose of these bits unknown; no observed variation
**Impact:** Emulator ignores these bits; Sony may not require them
**Real behavior:** Bits may indicate:
- AM/FM band selection
- Antenna tuning state
- Sensitivity mode
- IC variant detection
**To verify:**
- Analyze what Sony does with these bits
- Capture if Sony ever reads them differently
- Consult datasheet for port definitions

---

### 9. Tuner Ready Status = Always Ready
**Status:** ASSUMPTION
**Reason:** Simplifies operation; Sony appears to always receive 0x60
**Impact:** Emulator always reports bit 5 = 1
**Real behavior:** Should be 0 during startup/error, 1 during normal operation
**To verify:**
- Capture Sony startup sequence
- Monitor bit 5 transitions
- Implement ready state machine

---

## Read Request Detection Assumptions

### 10. Read Transactions Are Detectable (Method Unknown)
**Status:** ASSUMPTION
**Reason:** Sony must distinguish read from write; method not yet identified
**Impact:** Emulator currently treats all frames as writes
**Possible detection methods:**
- Timing difference (different CLK edge count?)
- Bit pattern in frame (specific value or structure?)
- Separate signal (not yet observed)
- CLK timing variation (different frequency or edge spacing?)
**To verify:**
- Capture Sony explicitly requesting status
- Compare frame patterns of writes vs. reads
- Analyze CLK timing for differences

---

### 11. D-IN Must Be Driven During Read Requests
**Status:** ASSUMPTION
**Reason:** Standard serial interface behavior; Sony must read status
**Impact:** Emulator attempts to drive D-IN on read frames
**Current limitation:** Read detection not implemented
**To verify:**
- Capture Sony sending read request
- Confirm D-IN is expected to be driven
- Test emulator response

---

## Tuning Sequence Assumptions

### 12. AFC/Fine-Tuning Is Sony Firmware, Not LC72130
**Status:** ASSUMPTION
**Reason:** Tuning offset sequence appears deliberate and controlled
**Impact:** Emulator logs patterns but doesn't participate
**Pattern observed:**
```
1. Tune to 106.00 MHz (initial)
2. Offset to 106.05 MHz (+0.05 MHz)
3. Offset to 106.20 MHz (+0.20 MHz total)
4. Return to 106.00 MHz (−0.20 MHz)
```
**Questions:**
- Is this AFC attempting to center station?
- Is this Sony testing PLL response?
- Is this part of startup calibration?
**To verify:**
- Monitor if sequence repeats per frequency
- Check if station centering is optimal after sequence
- Compare with other FM radio designs

---

## Level Conversion Assumptions

### 13. Sony Logic Is 5V; ESP32 Is 3.3V
**Status:** ASSUMPTION (highly probable)
**Reason:** Standard IC voltage levels; Sony is vintage 1980s–90s design
**Impact:** Level shifters (74LVC245 in, 74AHCT125 out) are required
**Current design:** Assumes this and documents it
**To verify:**
- Measure actual Sony bus voltages with oscilloscope
- Confirm 74LVC245 and 74AHCT125 are sufficient
- Test for signal integrity across frequency range

---

### 14. Common Ground Is Essential
**Status:** ASSUMPTION
**Reason:** Standard analog/digital interface practice
**Impact:** All level converters share ESP32 and Sony ground
**Critical:** VFD supply (−30V) must NOT connect to this ground plane
**To verify:**
- Verify Sony connector pin assignments
- Confirm −30V supply isolation
- Test with oscilloscope before connecting

---

## Safety & Operational Assumptions

### 15. D-IN Can Be Safely Inactive During PASSIVE_MONITOR
**Status:** ASSUMPTION
**Reason:** GPIO18 pulled LOW or left floating should not interfere with Sony
**Impact:** Safe mode for initial testing without emulation
**Real behavior:** Open-drain pull-up on Sony side?
**To verify:**
- Test Sony behavior with D-IN high, low, and floating
- Confirm Sony continues to function in PASSIVE_MONITOR mode
- No damage or errors occur

---

### 16. Watchdog Timer Requirement: 15 Seconds
**Status:** ASSUMPTION
**Reason:** Typical RTOS task watchdog; allows reasonable recovery time
**Impact:** Tasks must pet watchdog every <15 seconds
**Configuration:** Settable in sdkconfig via CONFIG_TASK_WDT_TIMEOUT_S
**To verify:**
- Monitor task timing in production
- Adjust if legitimate tasks exceed 15 sec
- Test watchdog recovery

---

### 17. Frame Timeout: 1 Second
**Status:** ASSUMPTION
**Reason:** CE should complete within 150 µs; 1 second is safe margin for error recovery
**Impact:** If CE remains high >1 second, reset capture state
**Configuration:** TRANSACTION_TIMEOUT_MS = 1000
**To verify:**
- Monitor CE timing in real operation
- Adjust if Sony uses longer transactions
- Test timeout recovery

---

## Emulator State Assumptions

### 18. PLL Lock Transitions in <500 µs
**Status:** ASSUMPTION
**Reason:** Real PLLs relock quickly; conservative estimate
**Impact:** Emulator assumes instant lock (current); could be made timing-accurate
**To verify:**
- Measure real LC72130 PLL lock time
- Implement delay if needed
- Monitor Sony behavior if lock timing is wrong

---

### 19. All Frequencies 0x0700–0x0B00 Are Valid
**Status:** ASSUMPTION
**Reason:** Standard FM band; outside this range is rejected as malformed
**Impact:** May miss edge frequencies or AM band
**To verify:**
- Test Sony with minimum/maximum tuning
- Confirm it stays within assumed range
- Adjust if edge cases found

---

## Capture Implementation Assumptions

### 20. FreeRTOS Queues Are Sufficient for Event Logging
**Status:** ASSUMPTION
**Reason:** ISR posts to queue; task drains queue periodically
**Impact:** No real-time blocking in ISRs; log may lag behind events
**Queue size:** 256 events
**To verify:**
- Monitor queue depth in production
- Increase if overflows occur
- Ensure logging lag doesn't cause confusion

---

### 21. RMT Peripheral Can Drive D-IN at 200 kHz
**Status:** ASSUMPTION
**Reason:** RMT is flexible; 1 MHz resolution should handle 200 kHz easily
**Impact:** D-IN response timing should be accurate
**Timing:** 5 µs per bit @ 200 kHz = RMT achieves with 1 µs ticks
**To verify:**
- Measure D-IN output with oscilloscope
- Verify timing aligns with CLK edges
- Test at varying CLK rates (±30%)

---

## Datasheet Assumptions

### 22. LC72130 Datasheet Exists and Is Accessible
**Status:** ASSUMPTION
**Reason:** Sanyo IC from 1980s–90s should have documentation
**Impact:** Many protocol details can be verified against datasheet
**Current status:** Not yet located
**To verify:**
- Search Sanyo archives
- Check electronics engineering sites
- Contact original equipment manufacturers

---

### 23. Sony ST-SA3ES Schematic Is Available
**Status:** ASSUMPTION
**Reason:** Service manuals exist for vintage audio equipment
**Impact:** Could reveal exact Sony commands and expected LC72130 behavior
**Current status:** Not yet located
**To verify:**
- Search service manual databases
- Check vintage radio repair forums
- Contact Sony service (unlikely to help)

---

## Verification Priority

### High Priority (affects core operation)
- [x] Frequency formula
- [x] Byte order (LSB-first)
- [x] CLK edge (falling)
- [x] CE timing
- [ ] Read vs. write detection
- [ ] PLL lock timing

### Medium Priority (affects response accuracy)
- [ ] Input port bits usage
- [ ] IF-counter format
- [ ] Control byte meanings
- [ ] D-IN tri-state requirements

### Low Priority (nice-to-have, not blocking)
- [ ] AFC/tuning sequence purpose
- [ ] AM band support
- [ ] Startup initialization sequence
- [ ] Extended status responses

---

**Last Updated:** 2026-08-26
**Assumption Count:** 23 active
**High-confidence assumptions:** 5/23
**Needs verification:** 18/23
