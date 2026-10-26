# Live RDS injection bench findings

Bench update: 2026-10-26. These findings describe the ESP32-S3 live-streaming
installation and supplement the earlier Hello World specification.

## Confirmed: AST is active-low

With injected RDS running, the Sony did not display the text while AST was
HIGH. After setting AST LOW, the operator confirmed that RDS information
appeared on the Sony display.

For this installation, assert AST LOW when a radio stream or AirPlay source is
active. Do not hold AST HIGH to simulate tuned status.

The deployed firmware defaults to automatic AST control:

- LOW while internet radio is decoding or an AirPlay session is connected.
- HIGH otherwise, with state checked once per second.
- Manual HIGH/LOW overrides remain available; AUTO restores source tracking.
- Reboot restores AUTO.

AirPlay connection ownership, not a nonzero audio level, is used for this
decision. A paused but connected AirPlay session therefore keeps AST LOW.
This observation does not establish every other Sony status signal's polarity.

## Current ESP32-S3 wiring

| ESP GPIO | Sony signal | Firmware default |
|----------|-------------|------------------|
| 1 | ST | HIGH |
| 42 | AST | AUTO, active-low as described above |
| 41 | SI | High-impedance, no pull |
| 40 | RDS-C | Recovered RDS clock output |
| 38 | RDS-D | Recovered RDS data output |
| 39 | D-IN | Static HIGH |
| 47 | Unused, formerly AST | High-impedance, no pull |
| GND | Sony/interface ground | Common reference |

The ESP also uses GPIO14 for S/PDIF audio. GPIO48 is assigned to its onboard
RGB LED. Do not use GPIO35-37 on this N16R8 installation: octal memory uses
these pins.

Original drivers must be isolated before driving their controller-side lines.
Use suitable voltage/interface circuitry; Sony-side voltages must not reach
ESP32 GPIO inputs directly. GPIO HIGH is approximately 3.3 V at the ESP pin.

SI is an analog signal, reportedly using a 0-4 V range on this installation.
Its optional digital HIGH/LOW controls do not provide a variable 0-4 V output.
A resistor alone is not an analog converter or a level shifter.

D-IN is currently only a static bench override. It does **not** emulate
LC72130 CCB replies, PLL lock status, IF-counter results, or a tuner set to
90.00 MHz. The exact Sony D-IN to LC72130 connection remains to be established.

## RadioText: avoid scrolling the unused field

The original implementation padded RadioText with spaces to 64 characters.
The operator reported an excessively long blank section during scrolling.
The updated encoder instead constructs:

```text
title + SPACE + SPACE + CR
```

CR is the RDS end-of-text character `0x0D`, not the printable characters
backslash and r. The encoder trims trailing title spaces, reserves room for
two spaces and CR, and limits title content to 61 characters. The remaining
bytes of the 64-byte group-2A field are still padded, but follow the terminator.
For example:

```text
HELLO WORLD  <0x0D>
```

The receiver controls scrolling speed, wrap behavior and any extra repeat
pause. Firmware deployment and the transmitted terminator were verified;
the exact requested two-character visual pause has not yet been confirmed
on the Sony display. Changing RadioText toggles the text A/B flag.

## Live RDS and EON output

The deployed encoder sends PI `0x89FF`, group 0A Programme Service and group
2A RadioText derived from current radio or AirPlay metadata. PS is padded or
truncated to eight characters. The current cycle contains 16 interleaved
0A/2A pairs followed by four group-14A EON variants (0-3), totaling 36 groups.

The synthetic other network is `TEST EON`, PI `0x89FE`. Its TP flag is OFF;
no traffic announcement, alternative frequencies or group-14B switching
requests are generated. Earlier `NO EON` indications did not establish
whether basic RDS was being received. Basic text display was subsequently
confirmed with AST LOW; EON display acceptance remains unconfirmed.

RDS-C is generated with a 421-microsecond half-bit timer, approximately
1187.65 bit/s. Data changes on the falling edge and is stable across the
rising edge. Complete encoded cycles are swapped at cycle boundaries.
Software counters/readbacks alone are not an electrical waveform capture.

## Web controls and implementation location

The receiver homepage has HIGH/LOW buttons for ST and D-IN,
AUTO/HIGH/LOW for AST, and LOW/HIGH/high-impedance for SI. These pin overrides
are RAM-only; reboot restores the defaults in the wiring table.

The separate Radio only / Allow AirPlay control is persistent. Radio only
disconnects existing AirPlay clients and rejects new sessions, retaining
internet radio and RDS. AirPlay discovery may remain visible while blocked.

The live implementation is in
[ESP-Streamer](https://github.com/RZomerman/ESP-Streamer), under
`firmware/main/audio/sony_rds.c`, `sony_rds_encoder.c`, `radio.c`, and
`firmware/data/www/radio.html`. At this bench update the implementation
changes are local/deployed, not committed or pushed to that repository.
This document records findings; it does not claim those sources are already
available in the remote repository.
