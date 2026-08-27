# C701 `laterscan.csv` Analysis

The capture contains 1,000,000 samples in this column order:

1. CLK
2. D-IN
3. DATA
4. CE

## Measured Protocol

- Idle state: `CLK=1, D-IN=0, DATA=1, CE=0`
- CE is active high.
- There are 21 CE windows, each containing exactly 24 rising CLK edges.
- D-IN carries the command bits and changes 352 times.
- DATA remains high throughout every frame.
- D-IN is LSB-first within each byte.
- Rising- and falling-edge decoding agree for all stable frames.

Each tuning action produces three frames. The first byte of the first frame
increments by one; the following two frames are fixed:

| Tuning action | Display | Frequency frame | Follow-up 1 | Follow-up 2 |
| ---: | ---: | --- | --- | --- |
| 1 | 90.05 MHz | `E1 07 2A` | `5F 32 11` | `5F 32 11` |
| 2 | 90.10 MHz | `E2 07 2A` | `5F 32 11` | `5F 32 11` |
| 3 | 90.15 MHz | `E3 07 2A` | `5F 32 11` | `5F 32 11` |
| 4 | 90.20 MHz | `E4 07 2A` | `5F 32 11` | `5F 32 11` |
| 5 | 90.25 MHz | `E5 07 2A` | `5F 32 11` | `5F 32 11` |
| 6 | 90.30 MHz | `E6 07 2A` | `5F 32 11` | `5F 32 11` |
| 7 | 90.35 MHz | `E7 07 2A` | `5F 32 11` | `5F 32 11` |

The unit displayed 90.00 MHz when capture started. The first tuning action was
90.00 to 90.05 MHz, producing `E1 07 2A`. The changing divider is
little-endian, so the captured values are `0x07E1` through `0x07E7`. This
establishes the decoder equation:

```text
RF_kHz = (divider * 50) - 10800
```

The seven captured frequency frames therefore decode as 90.05 through
90.35 MHz in 50 kHz steps.