#include "rds_clock.h"

#include <stdio.h>
#include <time.h>

#include "esp_log.h"
#include "esp_sntp.h"

static const char *TAG = "RDS_CLOCK";
static bool sntp_started;

/* RDS spec (IEC 62106 Annex G) Modified Julian Day formula. */
static uint32_t gregorian_to_mjd(int year, int month, int day)
{
    int l = (month == 1 || month == 2) ? 1 : 0;
    int y = year - 1900 - l;
    int m = month + 1 + l * 12;
    return 14956 + day + (uint32_t)(y * 365.25) + (uint32_t)(m * 30.6001);
}

void rds_clock_mjd_to_date(uint32_t mjd, int *year_out, int *month_out, int *day_out)
{
    int y = (int)((mjd - 15078.2) / 365.25);
    int m = (int)((mjd - 14956.1 - (int)(y * 365.25)) / 30.6001);
    int d = (int)mjd - 14956 - (int)(y * 365.25) - (int)(m * 30.6001);
    int k = (m == 14 || m == 15) ? 1 : 0;
    y += k;
    m -= 1 + k * 12;

    *year_out = y + 1900;
    *month_out = m;
    *day_out = d;
}

void rds_clock_init(void)
{
    if (sntp_started) {
        return;
    }
    sntp_started = true;

    sntp_setoperatingmode(SNTP_OPMODE_POLL);
    sntp_setservername(0, "pool.ntp.org");
    sntp_init();
    ESP_LOGI(TAG, "SNTP time sync started");
}

bool rds_clock_get_mjd_hm(uint32_t *mjd_out, uint8_t *hour_out, uint8_t *minute_out,
                          bool *offset_negative_out, uint8_t *offset_half_hours_out)
{
    time_t now = time(NULL);
    if (now < 1735689600) {  /* before 2025-01-01: clock not yet synced */
        return false;
    }

    int offset_half_hours = CONFIG_LC72130_RDS_UTC_OFFSET_HALF_HOURS;
    *offset_negative_out = offset_half_hours < 0;
    *offset_half_hours_out = (uint8_t)(offset_half_hours < 0 ? -offset_half_hours : offset_half_hours);

    /* Per IEC 62106 Annex G, MJD/hour/minute must carry UTC; compliant
     * receivers add the offset field themselves to derive local time. */
    struct tm utc;
    gmtime_r(&now, &utc);

    *mjd_out = gregorian_to_mjd(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday);
    *hour_out = (uint8_t)utc.tm_hour;
    *minute_out = (uint8_t)utc.tm_min;
    return true;
}

bool rds_clock_get_utc_string(char *buffer, size_t size)
{
    time_t now = time(NULL);
    if (now < 1735689600) {
        snprintf(buffer, size, "not synced");
        return false;
    }

    struct tm utc;
    gmtime_r(&now, &utc);
    strftime(buffer, size, "%Y-%m-%d %H:%M:%S", &utc);
    return true;
}
