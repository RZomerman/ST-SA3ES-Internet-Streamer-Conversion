# Sony ST-SA3ES ESP32 RDS Interface Specification

**Revision:** 1.3  
**Status:** Ready for first ESP32 bench implementation  
**Project boundary:** Post-demodulator RDS injection into the original Sony controller

## 1. Objective

Keep the Sony controller and original VFD operational while replacing the RDS source with an ESP32-generated synchronous clock/data stream.

The ESP32 does **not** generate the FM multiplex or 57 kHz RDS subcarrier. It emulates the recovered outputs normally produced by the SAA6579T.

## 2. Verified physical interface

| Sony connection | Signal | Function |
|---|---|---|
| CN701 pin 5 | RDS-C | ESP32 GPIO5 clock output to the Sony controller |
| CN702 pin 6 | RDS-D | ESP32 GPIO6 data output to the Sony controller |

Capture column order:

1. RDS-D data
2. RDS-C clock

The measured Sony logic-high level is approximately 4.8 V. Do not connect a Sony output directly to an ESP32 GPIO. For injection, isolate the SAA6579T output path and drive the Sony side through a suitable 3.3 V-to-5 V buffer or source selector.

## 3. Recovered protocol

- Nominal recovered clock: 1187.5 Hz
- One transmitted bit per RDS-C clock period
- One block: 16 information bits + 10 check bits
- One group: four blocks = 104 bits
- One group duration: approximately 87.6 ms
- Bit order: MSB first
- Data changes only after a falling clock edge and remains stable across the
	following rising edge

### 3.1 CRC and block offsets

- Generator polynomial: `0x5B9`
- Block A offset: `0x0FC`
- Block B offset: `0x198`
- Block C offset: `0x168`
- Block C-prime offset: `0x350`
- Block D offset: `0x1B4`

Every generated block must contain a valid checkword. Plain ASCII without RDS framing is not sufficient.

## 4. Veluwe FM validation

Verified PI: `0x89FF`

Full PS reconstruction from CRC-valid Group 0A messages:

| Address | Characters |
|---:|---|
| 0 | `VE` |
| 1 | `LU` |
| 2 | `WE` |
| 3 | `FM` |

Result: `VELUWEFM`

Verified Group 2A RadioText fragments include `M, V`, `oor `, `jk, `, `Erme`, `lo e`, and `tten`. Addresses 12 through 15 contained spaces.

## 5. RADIO538 validation

The longer RADIO538 capture produced 212 complete CRC-valid groups.

- PI: `0x83C7` in every decoded group
- Group 0A: 118 groups
- Group 2A: 71 groups
- Group 3A: 23 groups

### 5.1 Full PS

| Address | Block D | Characters |
|---:|---:|---|
| 0 | `0x5241` | `RA` |
| 1 | `0x4449` | `DI` |
| 2 | `0x4F35` | `O5` |
| 3 | `0x3338` | `38` |

Result: `RADIO538`

### 5.2 Full RadioText field

| Address | Characters |
|---:|---|
| 0 | `Radi` |
| 1 | `o 53` |
| 2 | `8\r  ` |
| 3-15 | spaces |

Meaningful text: `Radio 538`, followed by carriage return and space padding to 64 characters.

### 5.3 Observed service mix

The capture interleaves Group 0A, Group 2A and periodic Group 3A messages. Group 0A and Group 2A are sufficient for the first display-injection prototype. Group 3A remains out of scope until a Sony dependency is demonstrated.

## 6. ESP32 implementation baseline

Active profile:

- PI: `0x83C7`
- PS: continuous `Blog.AzureInfra.com` marquee in the 8-character field
- RadioText: `Blog.AzureInfra.com`

### 6.1 Scheduler

The first implementation shall:

1. Transmit continuously at the recovered RDS rate.
2. Produce one 104-bit group approximately every 87.6 ms.
3. Cycle all four Group 0A PS addresses.
4. Cycle all sixteen Group 2A RadioText addresses.
5. Insert carriage return after meaningful RadioText when capacity remains.
6. Space-pad the remainder of the 64-character field.
7. Toggle the RadioText A/B flag whenever the text changes.
8. Keep PI consistent across every group for the active station.
9. Interleave Group 0A and Group 2A messages.

The starter scheduler uses five Group 0A messages and three Group 2A messages per eight generated groups. This is an implementation profile, not a claim that the Sony receiver requires this exact ratio.

### 6.2 Hardware safety

- Do not drive RDS-C/RDS-D in parallel with an active SAA6579T output.
- Use a reversible source selector or disconnect the original source during bench testing.
- Use a 3.3 V-to-5 V logic buffer.
- Keep the external buffer disabled during ESP32 boot.
- Confirm buffer polarity and GPIO assignments before enabling output.

### 6.3 First bench milestone

1. Wire the buffered ESP32 outputs to the isolated Sony-side RDS-C and RDS-D inputs.
2. Leave output disabled during boot.
3. Start with the built-in `AZUREINF` profile.
4. Enable the buffer only after verifying clock and data at the buffer input.
5. Confirm that the Sony controller reconstructs `AZUREINF` and
	`Blog.AzureInfra.com`.
6. Only then connect dynamic internet-radio metadata.

## 7. Project structure

```text
sony_st_sa3es_rds_esp32/
├── platformio.ini
├── include/rds_encoder.h
├── src/rds_encoder.cpp
├── src/main.cpp
├── test/test_rds.cpp
└── docs/RDS_INTERFACE_SPEC.md
```

## 8. Open items

- Confirm the final GPIO assignment.
- Finalize the 5 V source-selector/level-buffer circuit.
- Test whether the Sony controller needs any quality/status signal in addition to RDS-C and RDS-D.
- Observe a live RadioText change and confirm Sony refresh behaviour.
- Decide whether Group 3A support is needed.
- Replace the placeholder AF words with station-specific or neutral AF handling if required.

## 9. Captured reference values

RADIO538 Group 0A B words observed for PS addresses 0 to 3:

```text
0x0548 0x0549 0x054A 0x054F
```

First observed RADIO538 Group 0A C words:

```text
0x8F90 0x9293 0x9495 0x9697
```

The starter encoder uses these C words as a replay-compatible baseline. They are not yet a general Alternative Frequency implementation.
