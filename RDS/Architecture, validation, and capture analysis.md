# Sony ST-SA3ES ESP32 RDS Architecture and Validation

# 7. ESP32 injection architecture

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>Internet-radio metadata<br />
|<br />
v<br />
RDS group builder: 0A (PS) + 2A (RadioText)<br />
|<br />
v<br />
CRC-10 and A/B/C/D offset-word encoder<br />
|<br />
v<br />
104-bit group scheduler<br />
|<br />
v<br />
1187.5 bit/s data output + recovered-style clock output<br />
|<br />
v<br />
3.3 V to 5 V buffer / source selector<br />
|<br />
v<br />
Sony RDS-D and RDS-C inputs -> Sony controller -> original display</th>
</tr>
</thead>
</table>

## 7.1 Hardware boundary

| **Requirement** | **Baseline** |
|----|----|
| Isolation | The SAA6579T and ESP32 must not drive the Sony inputs simultaneously. |
| Level translation | Convert ESP32 3.3 V outputs to the Sony 5 V logic domain. |
| Ground | Use an appropriate common digital reference. |
| Selection | Provide an original-RDS / ESP32-RDS source selection mechanism. |
| Initial prototype | Prefer a reversible, current-limited bench arrangement before permanent PCB integration. |

## 7.2 Firmware modules

| **Module** | **Responsibility** |
|----|----|
| Metadata state | PI, PTY, PS, RadioText and update flags |
| Group 0A builder | Emit four two-character PS segments |
| Group 2A builder | Emit four RadioText characters per segment |
| CRC/checkword encoder | Generate 10-bit checkword and apply block offset |
| Scheduler | Repeat PS more frequently and cycle RadioText segments |
| Bit output engine | Present data and clock with stable setup/hold timing |
| Validation logger | Record generated groups and compare against offline decoding |

## 7.3 Recommended first firmware milestone

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Replay before generation</strong></p>
<p>First replay the two verified captured groups using ESP32-generated clock and data. If the Sony controller accepts the replay, the electrical interface, polarity, bit order and timing are proven. Only then replace captured groups with dynamically generated 0A and 2A groups.</p></th>
</tr>
</thead>
</table>

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>Verified group 0A information words: 89FF 042F CACD 464D<br />
Verified group 2A information words: 89FF 2422 4D2C 2056<br />
<br />
Important: calculate and append each block's 10-bit checkword before transmission.</th>
</tr>
</thead>
</table>

# 8. Validation plan

| **Test** | **Method** | **Pass condition** |
|:--:|----|----|
| Offline encoder | Generate known 0A/2A groups and run them through the same decoder | PI, group type and character fields decode exactly |
| Captured-group replay | Transmit the two verified groups to the isolated Sony RDS inputs | Sony controller accepts the stream without bus contention |
| PS assembly | Cycle VE, LU, WE, FM Group 0A segments | Display reconstructs VELUWEFM |
| RadioText assembly | Cycle Group 2A segments for a controlled short string | Displayed text matches the controlled string |
| Metadata update | Change RadioText and toggle the RDS text A/B flag | Sony refreshes rather than mixing old and new text |
| Electrical test | Scope or analyse both buffered outputs under load | Levels and edge timing remain within the intended logic domains |
| Fallback | Switch source back to the SAA6579T | Original FM RDS operation returns cleanly |

# 9. Additional capture recommendation

More data is useful for confidence, but it is not required before writing the first ESP32 replay and encoder prototype. Before calling the implementation production-ready, decode a second non-overlapping capture containing enough repeated groups to recover all four PS segments and a larger section of RadioText.

| **Priority** | **Capture objective** | **Reason** |
|:--:|----|----|
| High | Recover VE, LU, WE and FM in valid 0A groups | Confirms segment addressing and full PS assembly |
| High | Recover consecutive 2A segments | Confirms RadioText addressing and ordering |
| Medium | Observe a RadioText change | Confirms A/B toggle and receiver refresh behaviour |
| Medium | Capture loss and reacquisition of RDS | Determines whether another status line or timeout behaviour matters |
| Low | Capture a station with clock-time data | Only required if time display will be emulated |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Engineering judgement</strong></p>
<p>Proceed with the reference document and first ESP32 implementation now. Request another 5,000-to-10,000-line non-overlapping chunk when validating full PS/RadioText assembly, not as a prerequisite for starting development.</p></th>
</tr>
</thead>
</table>

# 10. Reference decoder logic

The offline decoder used for the experiment should remain part of the project. It gives the ESP32 implementation a deterministic regression test: generated bitstreams can be sampled or exported and decoded using the same CRC and group parser.

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>offsets = {A:0x0FC, B:0x198, C:0x168, Cprime:0x350, D:0x1B4}<br />
polynomial = 0x5B9<br />
<br />
for each selected RDS-C edge:<br />
append RDS-D to bitstream<br />
<br />
for each 26-bit sliding window:<br />
syndrome = polynomial_remainder(window)<br />
if syndrome matches an offset:<br />
record block type and 16-bit information word<br />
<br />
accept a complete group only when blocks occur at 26-bit spacing:<br />
A -> B -> C or Cprime -> D</th>
</tr>
</thead>
</table>

# 11. References

- **Sony project FM baseline:** Sony_ST-SA3ES_ESP32_FM_Frequency_Control.docx, internal project document.

- **Experimental sample:** 5969lines.txt, 5,965 consecutive samples from RDS-20Khz500.

- **Full capture:** RDS-20Khz500.csv / .log, 20 kHz, 500,000 samples.

- **SAA6579T data sheet:** https://media.digikey.com/pdf/Data%20Sheets/NXP%20PDFs/SAA6579.pdf

- **RDS encoder reference:** https://github.com/hayguen/mpxgen/blob/master/src/rds.c

- **ESP32 RDS assembler reference:** https://github.com/MarcFinns/PiratESP32-FM-RDS-STEREO-ENCODER/blob/main/RDSAssembler.h

**Revision 1.1 validation update**

# 12. Extended-capture validation and repetition pattern

Nine consecutive capture chunks spanning sample indices 0 through 55,560 were decoded using the same clock-edge extraction, CRC polynomial and block-offset validation described earlier. The extended set produced 21 complete CRC-valid RDS groups: 11 Group 0A messages and 10 Group 2A messages. Every decoded Block A contained PI 0x89FF.

| **PS address** | **Characters** | **Observed sample positions** | **Repeat count** |
|----|----|----|----|
| 0 | VE | 33,359 | 1 |
| 1 | LU | 8,850; 22,851; 36,873; 50,882; 52,634 | 5 |
| 2 | WE | 26,354 | 1 |
| 3 | FM | 1,836; 15,854; 29,856; 43,880 | 4 |

Full Programme Service reconstruction is therefore directly verified: VE + LU + WE + FM = VELUWEFM. The unequal counts are a property of this extracted window and chunk boundaries; they should not be interpreted as the transmitter permanently favouring one segment.

## 12.1 Decoded RadioText segments

| **2A address** | **Characters** | **Sample position** |
|----------------|----------------|---------------------|
| 2              | M, V           | 3,587               |
| 3              | oor            | 7,098               |
| 6              | jk,            | 17,606              |
| 7              | Erme           | 21,099              |
| 8              | lo e           | 24,602              |
| 10             | tten           | 31,608              |
| 12             | four spaces    | 38,624              |
| 13             | four spaces    | 42,128              |
| 14             | four spaces    | 45,632              |
| 15             | four spaces    | 49,131              |

The recovered fragments align with the known RadioText "Veluwe FM, Voor Harderwijk, Ermelo en Putten". Addresses 12 through 15 contain space padding, confirming that the transmitted 2A field is padded through the 64-character capacity. The supplied window does not contain addresses 0, 1, 4, 5, 9 or 11, so the complete RadioText remains partly reconstructed rather than fully observed.

## 12.2 Repetition and scheduling pattern

A complete RDS group is 104 bits. At the nominal 1187.5 bit/s recovered clock, one group occupies approximately 87.6 ms. The valid groups recovered from the consecutive chunks are separated predominantly by approximately 87.5 ms, 175 ms or 350 ms. These are approximately one, two or four group durations.

The observed sequence interleaves Group 0A and Group 2A messages, but omitted groups occur where a 104-bit group crosses a file boundary or where the selected excerpts do not preserve enough surrounding bits for CRC synchronization. Accordingly, the 175 ms and 350 ms gaps should be treated as missed intermediate groups in the analysis, not proven transmitter silence.

| **Observed property** | **Implementation consequence** |
|----|----|
| PS is carried in Group 0A, two characters per addressed segment | Cycle addresses 0, 1, 2 and 3 to build the eight-character PS name. |
| RadioText is carried in Group 2A, four characters per addressed segment | Cycle addresses 0 through 15 for a 64-character field and pad unused characters with spaces. |
| 0A and 2A groups are interleaved | ESP32 scheduler should interleave PS refresh groups with RadioText groups rather than send one field only once. |
| One group duration is about 87.6 ms | Transmit continuously at the recovered RDS rate; do not insert arbitrary delays between groups. |
| All decoded groups use PI 0x89FF | Keep PI consistent across every generated group for the selected virtual station. |

## 12.3 Specification changes from Revision 1.0

- Close the open item for full PS reconstruction. VELUWEFM is now directly verified across addresses 0 through 3.

- Record that 2A addresses 12 through 15 carry space padding in this transmission.

- Define the initial scheduler around a continuous 104-bit group cadence of approximately 87.6 ms per group.

- Require interleaving of Group 0A and Group 2A messages. Exact long-term group ratios remain implementation-tunable because boundary-crossing groups are absent from the extracted chunks.

- Keep full RadioText reconstruction and A/B text-toggle behaviour open. The current data validates several addresses and padding but does not contain every address or a text-change event.

**FINAL IMPLEMENTATION SPECIFICATION \| REVISION 2.0**

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Prototype objective</strong></p>
<p>Disconnect or isolate the original SAA6579T RDCL/RDDA links on the Sony-controller side, keep the tuner receiving an existing FM station for audio, and inject an ESP32-generated post-demodulator RDS stream that presents PS text HELLO and RadioText HELLO WORLD.</p></th>
</tr>
</thead>
</table>
