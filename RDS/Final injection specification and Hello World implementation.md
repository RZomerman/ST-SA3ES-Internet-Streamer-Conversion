# Sony ST-SA3ES ESP32 RDS Injection Final Specification

# 13. Final prototype scope

This prototype replaces only the recovered RDS clock and data presented to the Sony controller. The FM tuner and received audio remain operational. The ESP32 does not create an FM multiplex signal and does not generate a 57 kHz RDS subcarrier.

| **Element** | **Prototype value** |
|----|----|
| RDS interface | CN701 pin 5 RDS-C and CN702 pin 6 RDS-D, controller side after isolation |
| PI | 0x89FF, retained from the validated capture |
| PS | HELLO followed by three spaces, exactly eight characters |
| RadioText | HELLO WORLD followed by spaces to 64 characters |
| Group types | 0A for PS and 2A for RadioText |
| Bit rate | 1187.5 bit/s |
| Group length | 104 bits |
| Group duration | approximately 87.6 ms |
| Supercycle | 32 groups: 16 PS/RadioText pairs, 3328 bits, approximately 2.8 seconds |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Hard stop before power-up</strong></p>
<p>The SAA6579T and ESP32 must never actively drive the same RDS-C or RDS-D conductor. Verify isolation with power removed. The measured Sony logic level is approximately 4.8 V, so use a defined 3.3 V-to-5 V output stage and do not feed Sony logic into an ESP32 GPIO.</p></th>
</tr>
</thead>
</table>

# 14. Wiring and isolation specification

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>SAA6579T RDCL ---- disconnected or source-selected ---- Sony controller RDS-C<br />
SAA6579T RDDA ---- disconnected or source-selected ---- Sony controller RDS-D<br />
<br />
ESP32 GPIO CLOCK --> 3.3 V-to-5 V buffer --> Sony controller RDS-C<br />
ESP32 GPIO DATA --> 3.3 V-to-5 V buffer --> Sony controller RDS-D<br />
ESP32 GND ------------------------> Sony digital reference ground</th>
</tr>
</thead>
</table>

| **Check** | **Required result** |
|----|----|
| Continuity with power removed | Original RDCL and RDDA outputs are isolated from the controller-side injection points. |
| No back-drive path | ESP32 output cannot drive into the SAA6579T output. |
| Logic level | Buffered HIGH is compatible with the Sony 5 V logic domain. |
| Initial GPIO state | Clock and data start LOW until the complete stream buffer is ready. |
| Reversibility | Original links can be restored without damaging PCB pads or traces. |

A suitable AHCT-family buffer is a design candidate because it can accept a 3.3 V HIGH and produce a 5 V-domain output, but the selected part, enable state, supply decoupling and pinout must be checked against its actual data sheet before construction.

# 15. Hello World RDS data specification

## 15.1 Programme Service groups

| **Address** | **Characters** | **Block A** | **Block B** | **Block C** | **Block D** |
|-------------|----------------|-------------|-------------|-------------|-------------|
| 0           | HE             | 89FF        | 0428        | E2AF        | 4845        |
| 1           | LL             | 89FF        | 042D        | CACD        | 4C4C        |
| 2           | O              | 89FF        | 042A        | E2AF        | 4F20        |
| 3           |                | 89FF        | 042F        | CACD        | 2020        |

The Block-B and Block-C templates deliberately retain the patterns observed in the validated Veluwe FM capture. This minimizes variables in the first compatibility test. Later firmware can expose TP, PTY, TA, M/S, DI and AF fields as explicit configuration.

## 15.2 RadioText groups

| **Address** | **Characters** | **Block A** | **Block B** | **Block C** | **Block D** |
|-------------|----------------|-------------|-------------|-------------|-------------|
| 0           | HELL           | 89FF        | 2420        | 4845        | 4C4C        |
| 1           | O WO           | 89FF        | 2421        | 4F20        | 574F        |
| 2           | RLD            | 89FF        | 2422        | 524C        | 4420        |
| 3-15        | spaces         | 89FF        | 2423-242F   | 2020        | 2020        |

The prototype sets the RadioText A/B flag to zero. Before dynamic metadata updates are implemented, the text remains constant. Future firmware must toggle the A/B flag when replacing one complete RadioText message with a different message.

## 15.3 CRC and block serialization

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>generator polynomial = 0x5B9<br />
offset A = 0x0FC<br />
offset B = 0x198<br />
offset C = 0x168<br />
offset D = 0x1B4<br />
<br />
checkword = remainder((information_word &lt;&lt; 10) / polynomial) XOR offset<br />
transmitted_block = (information_word &lt;&lt; 10) OR checkword<br />
transmit each 26-bit block MSB first in order A, B, C, D</th>
</tr>
</thead>
</table>

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Regression vectors</strong></p>
<p>The encoder algorithm reproduces captured checkwords: 89FF/A -> 0BA, 042F/B -> 2BA, CACD/C -> 0A7 and 464D/D -> 04B. These vectors must remain automated tests in later firmware.</p></th>
</tr>
</thead>
</table>

# 16. Scheduling and timing

The prototype builds a fixed 32-group supercycle. Each RadioText address 0 through 15 is preceded by one PS group. The PS address cycles 0, 1, 2 and 3. This continually refreshes HELLO while cycling through the complete padded RadioText field.

| **Sequence item** | **Rule** |
|----|----|
| Odd group in each pair | Group 0A, PS address = RadioText address modulo 4 |
| Even group in each pair | Group 2A, RadioText address 0 through 15 |
| Clock | 1187.5 complete cycles per second |
| Half-bit interrupt | 2375 interrupts per second |
| Data setup | Present the next RDS-D bit while RDS-C is LOW, before the rising edge |
| Data hold | Do not change RDS-D while RDS-C is HIGH |
| Repeat | After 3328 bits, return to the first group without a gap |

The Arduino-ESP32 3.x implementation configures a hardware timer at 2,375,000 ticks per second and alarms every 1000 ticks. This produces a 2375 Hz half-bit event rate, two events per 1187.5 Hz clock cycle. The current Arduino-ESP32 timer API uses timerBegin(frequency), timerAttachInterrupt(...) and timerAlarm(...).

# 17. Firmware deliverable

The companion sketch ST_SA3ES_RDS_HelloWorld.ino is the normative prototype implementation. It contains:

- CRC-10 and offset-word generation

- Group 0A PS assembly

- Group 2A RadioText assembly

- Space padding to 64 RadioText characters

- A fixed interleaved 32-group stream

- MSB-first block serialization

- Hardware-timer clock generation

- Captured-checkword diagnostic output on the serial console

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Validated before delivery</strong></p>
<p>The generated stream contains 32 groups and 3328 bits. All generated blocks pass the same syndrome check used to decode the captured Sony stream. The generated PS segments are HE, LL, O-space and two spaces. The first RadioText segments are HELL, O-space-WO and RLD-space.</p></th>
</tr>
</thead>
</table>

# 18. Bench test procedure

| **Step** | **Action** | **Expected evidence** |
|----|----|----|
| 1 | With power removed, isolate both original RDS links and verify no continuity to the SAA6579T output side. | No output conflict path. |
| 2 | Connect ESP32 through the selected 5 V logic buffer to controller-side RDS-C and RDS-D. | Correct wiring and common reference. |
| 3 | Run the sketch without connecting the Sony controller; inspect buffered clock and data using the logic analyser. | Clock near 1187.5 Hz; data stable across clock edges. |
| 4 | Capture at 20 kHz and decode the generated stream offline. | PI 89FF; PS HELLO; RT HELLO WORLD; valid A/B/C/D syndromes. |
| 5 | Power the tuner on a strong FM station and connect the buffered injection outputs. | Normal audio remains; injected RDS stream reaches the controller. |
| 6 | Select the Sony display mode that normally shows PS or RadioText. | HELLO and/or HELLO WORLD appears, subject to Sony display-mode behavior. |
| 7 | If no text appears, restore the original links before changing firmware assumptions. | Original RDS operation returns, isolating hardware versus protocol faults. |

# 19. Pass, fail and stop criteria

| **Classification** | **Criteria** |
|----|----|
| Pass | Offline recapture decodes valid PI, PS and RadioText, and the Sony display shows injected text. |
| Partial pass | Offline recapture is valid but the Sony display does not show text. Investigate Sony display mode, quality/status dependencies and controller behavior. |
| Fail | CRC errors, wrong bit order, unstable timing, missing level translation or output contention. |
| Immediate stop | Unexpected current, heating, supply disturbance, tuner malfunction, or evidence that both sources are driving a line. |

# 20. Open items after Hello World

- Confirm whether Sony requires a separate RDS quality indication before displaying injected text.

- Observe and implement RadioText A/B toggling for dynamic song-title changes.

- Replace the fixed stream with double-buffered metadata updates only at group or supercycle boundaries.

- Define how PI and PTY should map to each virtual Internet-radio station.

- Replace captured AF payload templates if alternative-frequency behavior causes unwanted Sony actions.

- Add a hardware source selector so original FM RDS and ESP32 metadata can be selected without moving links.

# 21. Final decision

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Proceed to bench prototype</strong></p>
<p>No more passive capture is required before the Hello World injection test. The protocol, bit order, CRC, PS addressing, RadioText addressing, padding and cadence are sufficiently established. The next uncertainty is active Sony-controller acceptance, which can only be resolved by an isolated, level-shifted injection test.</p></th>
</tr>
</thead>
</table>

# 22. Implementation references

- Internal project reference: Sony_ST-SA3ES_ESP32_RDS_Interface_Reference_Rev1_1.docx

- Internal frequency-control baseline: Sony_ST-SA3ES_ESP32_FM_Frequency_Control.docx

- Companion sketch: ST_SA3ES_RDS_HelloWorld.ino

- Captured analysis: rds_full_analysis.txt

- SAA6579 data sheet: https://media.digikey.com/pdf/Data%20Sheets/NXP%20PDFs/SAA6579.pdf

- Arduino-ESP32 timer API: https://docs.espressif.com/projects/arduino-esp32/en/latest/api/timer.html

- RDS decoder/encoder reference: https://deepwiki.com/bastibl/gr-rds/2-core-rds-processing
