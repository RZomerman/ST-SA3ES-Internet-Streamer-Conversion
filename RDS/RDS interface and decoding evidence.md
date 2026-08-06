# Sony ST-SA3ES ESP32 RDS Interface and Decoding Evidence

**SONY ST-SA3ES**

**ESP32 RDS Interface Reference**

*Experimental decoding, protocol baseline and injection design*

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Purpose</strong></p>
<p>Document the verified RDS clock/data interface between the SAA6579T demodulator and the Sony controller, preserve the decoded evidence, and define the baseline required to implement an ESP32-generated RDS stream for the original Sony display.</p></th>
</tr>
</thead>
</table>

| **Item** | **Value** |
|----|----|
| Status | Experimentally decoded at the RDS group level |
| Verified sample | 5965 consecutive samples from the 20 kHz capture |
| Station PI | 0x89FF |
| Verified PS evidence | Block D = 0x464D = "FM" |
| Verified RadioText evidence | Blocks C/D = 0x4D2C 0x2056 = "M, V" |
| Primary next step | ESP32 replay of valid captured groups through a safe 3.3 V-to-5 V interface |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Decision on more capture data</strong></p>
<p>No additional capture is required to establish this implementation baseline. The current sample proves valid RDS CRC framing, PI 0x89FF, a Group 0A PS segment and a Group 2A RadioText segment. A second non-overlapping sample is recommended later to validate all four PS segments, more RadioText positions and repetition behaviour before final firmware acceptance.</p></th>
</tr>
</thead>
</table>

# 1. Scope and design boundary

The goal is not to generate an FM multiplex signal or a 57 kHz RDS subcarrier. The Sony tuner already uses an SAA6579T demodulator. That device outputs recovered RDS data and a recovered RDS clock for processing by the Sony controller. The ESP32 implementation will target this post-demodulator interface.

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Core design rule</strong></p>
<p>Generate the same recovered clock/data stream that the SAA6579T presents to the Sony controller. Do not implement FM modulation, 57 kHz subcarrier generation, biphase decoding or differential decoding in the injection path.</p></th>
</tr>
</thead>
</table>

This note complements the existing FM frequency-control baseline. That separate interface lets the ESP32 observe the frequency requested by the original controller, while the RDS interface provides a route for presenting station names and stream metadata through the Sony controller and original display.

# 2. Physical interface

| **Connector** | **Pin** | **Sony label** | **RDS role** |
|:--:|----|----|----|
| CN701 | 5 | RDS-C | Recovered RDS clock input to the Sony controller |
| CN702 | 6 | RDS-D | Recovered RDS data input to the Sony controller |

Logic-analyser capture mapping: the first exported column is RDS-D and the second exported column is RDS-C. The clock column toggles regularly while the data column remains stable across the sampling transition.

| **Source signal** | **SAA6579T pin** | **Function**     |
|:-----------------:|------------------|------------------|
|       RDDA        | 2                | RDS data output  |
|       RDCL        | 16               | RDS clock output |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Electrical caution</strong></p>
<p>The Sony logic was measured at approximately 4.8 V. Do not connect the Sony RDS outputs directly to ESP32 GPIO pins. For active ESP32 injection, isolate or select away the SAA6579T outputs and use a 3.3 V-to-5 V logic buffer. Avoid two active outputs driving the same line.</p></th>
</tr>
</thead>
</table>

# 3. Capture and extraction method

| **Parameter** | **Value** | **Assessment** |
|:--:|----|----|
| Sample rate | 20 kHz | Sufficient oversampling for the recovered clock |
| Original capture | 500,000 samples | 25 seconds total |
| Decoded test subset | 5,965 samples | Approximately 0.298 seconds |
| Recovered clock edges | 354 | 354 recovered data bits |
| Data polarity | Normal | Inverted data did not produce valid complete groups |
| Column order | RDS-D, RDS-C | Confirmed by edge regularity and CRC-valid groups |

The decoder tested both columns as possible clock/data assignments, both rising and falling clock edges, and normal/inverted data. Only column 1 as data, column 2 as clock, and normal polarity produced repeated complete A-B-C-D RDS groups. Rising and falling edge extraction produced the same decoded groups because the data was stable across each clock transition in the examined sample.

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>For each clock transition:<br />
bit = read(RDS_D)<br />
shift bit into a 26-bit window<br />
calculate CRC syndrome using polynomial 0x5B9<br />
compare syndrome with RDS offset words<br />
synchronize blocks as A -> B -> C/C' -> D</th>
</tr>
</thead>
</table>

# 4. RDS framing baseline

A complete RDS group contains four 26-bit blocks. Every block contains 16 information bits followed by a 10-bit checkword. The offset word identifies the block position and supports synchronization.

| **Block** | **Offset syndrome** | **Primary content** |
|:--:|----|----|
| A | 0x0FC | Programme Identification (PI) |
| B | 0x198 | Group type, version and control fields |
| C | 0x168 | Group-dependent data |
| C' | 0x350 | Alternative Block C for version B groups |
| D | 0x1B4 | Group-dependent data, including PS or RadioText characters |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th><p><strong>Decoder constants</strong></p>
<p>Generator polynomial: 0x5B9. Complete group length: 104 bits. The implementation must produce valid checkwords and offset words; sending plain ASCII characters without valid RDS framing will not be accepted as a normal RDS stream.</p></th>
</tr>
</thead>
</table>

# 5. Experimentally decoded groups

Two complete, CRC-valid groups were recovered from the 5,965-sample subset. Both carried PI 0x89FF, matching the known station information.

## 5.1 Group 0A: Programme Service evidence

| **Block** | **Information word** | **Decoded meaning**                 |
|:---------:|----------------------|-------------------------------------|
|     A     | 0x89FF               | PI = 89FF                           |
|     B     | 0x042F               | Group type 0A; PS segment address 3 |
|     C     | 0xCACD               | Group 0A application data           |
|     D     | 0x464D               | ASCII 0x46 0x4D = "FM"              |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>Expected eight-character PS name: VELUWEFM<br />
Segment 0: VE<br />
Segment 1: LU<br />
Segment 2: WE<br />
Segment 3: FM &lt;- directly observed as Block D 0x464D</th>
</tr>
</thead>
</table>

This does not by itself recover all eight characters from the examined subset. It does directly verify the final PS segment, and that segment is consistent with the expected station name VELUWEFM.

## 5.2 Group 2A: RadioText evidence

| **Block** | **Information word** | **Decoded meaning**                        |
|:---------:|----------------------|--------------------------------------------|
|     A     | 0x89FF               | PI = 89FF                                  |
|     B     | 0x2422               | Group type 2A and RadioText segment fields |
|     C     | 0x4D2C               | ASCII 0x4D 0x2C = "M,"                     |
|     D     | 0x2056               | ASCII 0x20 0x56 = " V"                     |

<table>
<colgroup>
<col style="width: 100%" />
</colgroup>
<thead>
<tr>
<th>Recovered four-character RadioText fragment: "M, V"<br />
Expected text: "Veluwe FM, Voor Harderwijk, Ermelo en Putten"<br />
^^^^ observed fragment fits this boundary</th>
</tr>
</thead>
</table>

The recovered fragment is positioned consistently with the known RadioText. The sample does not establish the entire RadioText string, but it proves that standard Group 2A text data is present on the interface.

# 6. What is verified, supported and still open

| **Classification** | **Item** |
|----|----|
| Verified | CN701 pin 5 carries RDS-C and CN702 pin 6 carries RDS-D. |
| Verified | The first capture column is data and the second capture column is clock. |
| Verified | Normal data polarity produces complete CRC-valid RDS groups. |
| Verified | PI 0x89FF appears in both decoded Block A words. |
| Verified | A Group 0A Block D carries 0x464D, the characters "FM". |
| Verified | A Group 2A carries the characters "M, V" across Blocks C and D. |
| Supported interpretation | The Sony controller consumes standard post-demodulator RDS groups from the SAA6579T interface. |
| Open | Exact Sony sampling edge. Both edges decoded identically in this subset because data remained stable. |
| Open | Complete recovery of PS segments VE, LU and WE from additional non-overlapping data. |
| Open | Complete RadioText assembly, A/B toggle behaviour and text update timing. |
| Open | Whether the Sony controller requires QUAL or other status behaviour during active injection. |
| Open | Final line isolation/multiplexer and level-shifter circuit. |
