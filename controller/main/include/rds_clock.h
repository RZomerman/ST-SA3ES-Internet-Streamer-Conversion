#ifndef RDS_CLOCK_H
#define RDS_CLOCK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/**
 * Start SNTP time sync over Wi-Fi. Safe to call more than once; only the
 * first call takes effect.
 */
void rds_clock_init(void);

/**
 * Get the current UTC date/time encoded for an RDS Group 4A (Clock Time)
 * block: Modified Julian Day plus UTC hour/minute (per IEC 62106 Annex G,
 * this field always carries UTC), and the configured local UTC offset
 * sign/magnitude that receivers add to derive local time.
 * @return true if the system clock has been synced and values are valid
 */
bool rds_clock_get_mjd_hm(uint32_t *mjd_out, uint8_t *hour_out, uint8_t *minute_out,
                          bool *offset_negative_out, uint8_t *offset_half_hours_out);

/**
 * Convert a Modified Julian Day back to a Gregorian calendar date, for
 * display/validation purposes (inverse of the RDS spec's MJD formula).
 */
void rds_clock_mjd_to_date(uint32_t mjd, int *year_out, int *month_out, int *day_out);

/**
 * Format the current UTC time as "YYYY-MM-DD HH:MM:SS".
 * @return true if the system clock has been synced
 */
bool rds_clock_get_utc_string(char *buffer, size_t size);

#endif
