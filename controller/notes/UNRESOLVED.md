# Unresolved Protocol Questions

This document lists open questions that require additional investigation, reverse engineering, or datasheet research.

## Critical Unknowns (Blocking Full Implementation)

### 1. Read vs. Write Transaction Detection
**Question:** How does Sony distinguish a read request from a write transaction?

**Impact:** CRITICAL
- D-IN response not yet implemented
- Emulator cannot respond to Sony queries
- Tuner may not work properly without responses

**Possible mechanisms:**
```
a) Different frame content
   - Specific bit pattern?
   - Specific control byte value?
   - Length variation?
   
b) Different timing
   - Fewer CLK cycles before CE falls?
   - Different CLK frequency?
   - Longer/shorter CE pulse?
   - Timing relationship between CLK and DATA?
   
c) Separate signal
   - Read-enable or mode pin not yet identified?
   - Strobe or handshake signal?
   
d) Combination
   - Multiple signals or timing cues?
```

**Investigation methods:**
- Capture multiple Sony read requests (if possible)
- Analyze frame patterns vs. known writes
- Compare CLK timing in read vs. write
- Use logic analyzer to detect any hidden signals
- Study LC72130 datasheet (if found)

**Current workaround:** Assume all frames are writes; treat all as writes.

---

### 2. D-IN Response Format During Read Transactions
**Question:** What exactly should be driven on D-IN in response to a read request?

**Impact:** HIGH
- Emulator may send wrong response
- Sony may not recognize emulator as LC72130
- Tuning may fail or behave incorrectly

**Unknowns:**
```
a) Byte length
   - Single 8-bit byte?
   - Multiple bytes?
   - Variable length?
   
b) Bit format
   - Same LSB-first as writes?
   - Byte0 = status, Byte1 = additional data?
   - Checksum or parity bits?
   
c) Timing
   - When should D-IN be driven?
   - What alignment with CLK?
   - How long should response hold?
   
d) Content
   - PLL lock + tuner ready bits (assumed 0x60)?
   - IF-counter value?
   - Input port states?
   - Additional flags?
```

**Current assumption:** 0x60 (8-bit status byte, bit 6 & 5 only used)

**Investigation methods:**
- Capture D-IN during Sony read with real LC72130
- Compare with expected LC72130 datasheet behavior
- Test emulator response and monitor Sony behavior
- Use oscilloscope to measure D-IN timing

---

### 3. Read Request Detection Method (Detailed)
**Question:** What is the EXACT mechanism Sony uses to initiate a read vs. write?

**Impact:** CRITICAL (directly related to question #1)

**Captured data needed:**
```
Read request example:
- Frame bytes?
- CE timing (duration)?
- CLK frequency and edge count?
- DATA signal (high/low/transitioning)?
- Any unused signal transitions?

Comparison:
- How differs from frequency write?
- What is the distinctive marker?
```

**Hypotheses to test:**
1. **Bit count variant:** Read might use 8 bits instead of 24?
   - Sony sends just address byte
   - Waits for response on D-IN
   
2. **Timing variant:** Read might have different CLK timing?
   - Longer pause before CLK starts?
   - Different CLK frequency?
   - CLK not toggling?
   
3. **Control byte:** Specific byte2 value indicates read?
   - e.g., 0xFF or 0x00 for read requests
   - Overlaps with write frames?
   
4. **DATA line behavior:** DATA might be held LOW or HIGH for read?
   - Different level than write transactions?
   - Transition at specific time?

**Investigation:**
- Use logic analyzer to trigger on CE rising
- Capture minimum 10 examples of each (read and write)
- Zoom in on timing and bit patterns
- Compare side-by-side

---

## Medium-Priority Unknowns

### 4. Control Frame Sequence (0xE3, 0x4C, 0x88)
**Question:** What do the control frames do, and in what order are they sent?

**Impact:** MEDIUM
- Currently logged but ignored
- May affect tuning behavior
- Could be startup/configuration

**Observed pattern** (from user memory):
```
Tx1: Frequency frame (e.g., 0x1E 0x09 0x2A)
Tx2: 0xE3 0x4C 0x88
Tx3: (possibly another transaction?)
```

**Unknowns:**
```
a) Purpose of 0xE3 0x4C 0x88
   - Configuration bytes?
   - Test sequence?
   - Initialization?
   - Band selection?
   
b) Order and frequency
   - Always sent in this order?
   - Sent once per startup?
   - Sent after every frequency write?
   - Sent on demand from Sony?
   
c) Meaning of each byte
   - 0xE3: Address or mode?
   - 0x4C: Data or parameter?
   - 0x88: Control or checksum?
   
d) Sony interpretation
   - Does Sony expect responses?
   - Are there status replies?
   - Does Sony check for success/failure?
```

**Investigation:**
- Capture full startup sequence
- Monitor if sequence repeats during operation
- Vary frequency and check if control bytes change
- Consult LC72130 datasheet

**Workaround:** Log but ignore; tuning still works without decoding

---

### 5. AM Band Support
**Question:** Does the LC72130 support AM frequencies, and if so, how?

**Impact:** MEDIUM
- May affect frequency formula
- May require different divider ranges
- May use different control bytes

**Unknowns:**
```
a) IC capability
   - Is LC72130 AM/FM or FM-only?
   - If AM, what frequency range?
   - Are there separate IC variants?
   
b) Frequency formula
   - Same divider formula as FM?
   - Different offset or scale?
   - AM band: ~540–1600 kHz
   
c) Sony usage
   - Does Sony ST-SA3ES support AM?
   - Are AM frequencies ever sent?
   - Different control byte for AM?
   
d) Divider values
   - What dividers represent AM frequencies?
   - Range: 0x0000–0xFFFF?
   - Specific band edge dividers?
```

**Investigation:**
- Attempt to tune Sony to AM (if radio supports it)
- Capture AM tuning sequences
- Check IC datasheet for AM support
- Calculate expected divider ranges

**Current status:** No AM frequencies observed in captures

---

### 6. IF-Counter (Intermediate Frequency Counter)
**Question:** What are the purpose, format, and values of the IF-counter in D-IN response?

**Impact:** MEDIUM
- Affects status response accuracy
- May be required for AFC or tuning feedback
- May indicate IF centering or lock quality

**Unknowns:**
```
a) Purpose
   - Measures IF frequency alignment?
   - Indicates signal strength?
   - AFC feedback value?
   - Generic status bit?
   
b) Format
   - Single bit (bit 2 of status byte)?
   - Multiple bits or separate field?
   - Multi-byte counter value?
   
c) Values
   - Range of IF-counter result?
   - What indicates good centering?
   - What indicates off-frequency?
   
d) Sony usage
   - Does Sony read IF-counter?
   - Does Sony use it for AFC?
   - Required for proper tuning?
   - Affects audio quality?
```

**Investigation:**
- Capture real LC72130 IF-counter values during tuning
- Monitor Sony behavior at different IF-counter values
- Check datasheet for IF-counter range and purpose
- Test emulator with various IF-counter responses

**Current workaround:** Emulator returns 0 for IF-counter

---

### 7. Input Port Bits (Bits 3 & 4 of D-IN Response)
**Question:** What are the input port bits for, and what do they indicate?

**Impact:** MEDIUM
- May affect Sony behavior or mode detection
- May enable/disable features
- May indicate IC variant or configuration

**Unknowns:**
```
a) Purpose
   - GPIO input state feedback?
   - Feature or capability bits?
   - Band selection or mode?
   - Antenna type indication?
   
b) Meanings
   - Bit 3 (Input Port 1): ???
   - Bit 4 (Input Port 2): ???
   - Set by IC based on external inputs?
   - Set by Sony via control frames?
   
c) Sony behavior
   - Does Sony read these bits?
   - Does Sony change behavior based on values?
   - Required for any feature?
   
d) Default values
   - Should they be 0 or 1?
   - Do they change during operation?
   - Affects tuning or audio?
```

**Investigation:**
- Monitor Sony behavior with varying input port bits
- Check LC72130 datasheet for port definitions
- Capture if Sony ever checks these bits
- Look for external connections to IC pins

**Current status:** Assumed always 0; no variation observed

---

### 8. Startup/Initialization Sequence
**Question:** Is there a required sequence when Sony powers on or after reset?

**Impact:** MEDIUM
- Emulator may need to initialize properly
- Sony may expect responses in specific order
- May affect first tuning after startup

**Unknowns:**
```
a) Startup sequence
   - First frame sent by Sony?
   - Any IC configuration before tuning?
   - Initialization handshake?
   
b) Timing
   - How long between power-on and first command?
   - Any delays or waiting periods?
   - Watchdog or alive-signal requirements?
   
c) Sony checks
   - Does Sony verify IC is present?
   - Any test or identification frames?
   - Expected responses for startup?
   
d) Emulator requirements
   - Must initialize before accepting tuning?
   - Must respond differently at startup?
   - Any reset or error recovery?
```

**Investigation:**
- Capture Sony from power-on
- Monitor all initial transactions
- Check for initialization patterns
- Test emulator behavior at startup

**Current status:** No special startup handling implemented

---

## Low-Priority Unknowns (Nice-to-Have)

### 9. AFC (Automatic Frequency Control) Mechanism
**Question:** Who implements AFC, and what is the exact tuning sequence?

**Impact:** LOW
- Affects tuning accuracy
- May be Sony firmware, not IC
- Currently logged but not acted upon

**Observations:**
- Sony sends tuning offsets (+0.05 MHz, +0.20 MHz, −0.20 MHz)
- Pattern is repeatable
- Appears deliberate

**Unknowns:**
```
a) Actor
   - Does LC72130 perform AFC internally?
   - Is AFC done by Sony firmware?
   - Is external component involved?
   
b) Purpose
   - Center the station frequency?
   - Test PLL response?
   - Calibrate or auto-tune?
   
c) Frequency sweep pattern
   - Why these specific offsets?
   - Why this order?
   - How are results used?
   
d) Feedback
   - Does Sony use IF-counter for AFC?
   - Does Sony read status during AFC?
   - How does Sony know when AFC is complete?
```

**Investigation:**
- Monitor IF-counter values during AFC sequence
- Check Sony firmware or schematic for AFC implementation
- Test tuning accuracy with/without AFC
- Measure final frequency error

**Current status:** Logged as "tuning sequence"; not labeled as AFC

---

### 10. PLL Lock Time
**Question:** How long does the LC72130 take to lock to a new frequency?

**Impact:** LOW
- Affects emulator timing accuracy
- Impacts user experience (perceived tuning speed)
- May affect AFC sequence

**Unknowns:**
```
a) Lock time
   - Typical: 10 µs? 100 µs? 1 ms?
   - Range: min–max?
   - Frequency-dependent?
   
b) Unlock time
   - How long does unlock take during frequency change?
   - Is there overshoot?
   - Any transients?
   
c) Lock indicator
   - How does IC indicate lock status?
   - Via D-IN response bit 6?
   - Glitch-free?
   
d) Lock quality
   - Does lock quality vary?
   - Better lock at some frequencies?
   - Related to IF-counter?
```

**Investigation:**
- Capture real LC72130 D-IN during tuning
- Measure PLL unlock/relock duration
- Correlate with frequency changes
- Test emulator timing

**Current status:** Emulator assumes instant lock (oversimplification)

---

### 11. Clock Rate Variation Tolerance
**Question:** How much CLK rate variation can the protocol tolerate?

**Impact:** LOW
- Affects robustness and reliability
- May impact different Sony revisions
- Useful for tolerance design

**Unknowns:**
```
a) Tolerance range
   - ±10%, ±20%, ±30%, ±50%?
   - Device-dependent?
   - Specified in datasheet?
   
b) Edge timing
   - Edge jitter acceptable?
   - Runt pulses (very short CLK period)?
   - Stretched CLK periods?
   
c) Rate stability
   - Must rate stay constant per frame?
   - Can rate change between frames?
   - Drift limits?
```

**Investigation:**
- Test emulator with varying CLK rates
- Deliberately stretch/compress CLK timing
- Monitor for frame loss or corruption
- Document tolerance limits

**Current status:** Assumed ±30% (empirical); not formally tested

---

## Questions Awaiting Datasheet

### 12. LC72130 Complete Pinout
- Which pins are address/data/control?
- Are there external inputs for AFC or band selection?
- Crystal frequency for reference oscillator?
- Supply voltage and decoupling requirements?

### 13. Sanyo CCB Protocol Specification
- Formal definition of CCB bus?
- Timing diagrams and min/max specifications?
- Multi-IC daisy-chaining possible?
- Compatibility with other ICs on same bus?

### 14. Sony ST-SA3ES Schematic
- Exact wiring of LC72130 control lines
- Any external components affecting timing?
- Reset or initialization circuits?
- AFC feedback implementation?

---

## Investigation Checklist

- [ ] Obtain LC72130 datasheet (Sanyo archives?)
- [ ] Obtain Sony ST-SA3ES service manual
- [ ] Capture read request examples
- [ ] Capture D-IN response with real IC
- [ ] Test various CLK rates (±30%)
- [ ] Monitor startup sequence
- [ ] Test input port bit effects
- [ ] Verify AF-C tuning sequence purpose
- [ ] Measure PLL lock/unlock timing
- [ ] Document tolerance limits

---

**Last Updated:** 2026-08-26
**Critical unknowns:** 3 (read detection, D-IN response, exact read criteria)
**Medium unknowns:** 5
**Low priority:** 3
**Datasheet-dependent:** 3
