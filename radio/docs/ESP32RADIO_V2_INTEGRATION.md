# ESP32Radio-V2 Integration

This RDS service can run in Edzelf/ESP32Radio-V2 on the same ESP32-S3. It uses
hardware timer 1; ESP32Radio-V2 keeps timer 0 for `timer100()`.

## Add the RDS sources

Copy these files into the ESP32Radio-V2 project:

- `include/rds_encoder.h`
- `include/rds_input.h`
- `src/rds_encoder.cpp`
- `src/rds_input.cpp`
- `src/rds_service.cpp`

Do not copy this project's `src/main.cpp`; ESP32Radio-V2 remains the owning
application.

## Reserve GPIO5 and GPIO6

GPIO5 is RDS-C and GPIO6 is RDS-D. Ensure no `pin_*`, `gpio_*`, or `touch_*`
preference assigns either pin.

In the ESP32-S3 `progpin[]` table, mark GPIO5 reserved. GPIO6 is already
reserved upstream:

```cpp
{  5, true,  false,  "", false },
{  6, true,  false,  "", false },
```

In the ESP32-S3 `touchpin[]` table, mark both pins reserved:

```cpp
{   5, true, false, "", false, 0 },
{   6, true, false, "", false, 0 },
```

## Add the metadata bridge

Add the include near the other local includes in ESP32Radio-V2 `src/main.cpp`:

```cpp
#include "rds_input.h"
```

Add this helper after the global `icyname`, `icystreamtitle`, and `presetinfo`
declarations:

```cpp
static constexpr uint8_t RDS_PTY = 10;

void updateRdsMetadata(const char* title = nullptr) {
  const String& station = icyname.length() ? icyname : presetinfo.hsym;
  const char* radioText = title ? title : icystreamtitle.c_str();
  rds::submitMetadata(station.c_str(), radioText, RDS_PTY);
}
```

After the `icy-name:` parser has assigned, decoded, trimmed, and applied its
fallback to `icyname`, clear the previous station's title on RDS:

```cpp
updateRdsMetadata("");
```

In both places where `showstreamtitle(...)` returns `true`, submit the changed
title. This covers normal Icecast metadata and playlist `#EXTINF` metadata:

```cpp
if (showstreamtitle(metalinebf)) {
  mqttpub.trigger(MQTT_STREAMTITLE);
  updateRdsMetadata();
}
```

Apply the same added call inside the existing playlist form:

```cpp
if (showstreamtitle(metaline.substring(inx + 1).c_str(), true)) {
  mqttpub.trigger(MQTT_STREAMTITLE);
  updateRdsMetadata();
}
```

## Start and service RDS

In `setup()`, start RDS after ESP32Radio has read and reserved its I/O pins but
before it starts normal playback:

```cpp
if (!rds::begin(5, 6)) {
  ESP_LOGE(TAG, "RDS initialization failed");
}
```

Add this near the top of ESP32Radio-V2 `loop()`:

```cpp
rds::process();
```

The existing final `delay(10)` is acceptable. One 104-bit group takes about
87.6 ms, leaving ample time to refill the inactive group buffer.

## Wiring

| ESP32-S3 | Sony signal | Direction |
| --- | --- | --- |
| GPIO5 | RDS-C | ESP32 to tuner |
| GPIO6 | RDS-D | ESP32 to tuner |
| GND | GND | Common ground |

Verify voltage levels before connecting. The ESP32-S3 GPIOs are 3.3 V only.