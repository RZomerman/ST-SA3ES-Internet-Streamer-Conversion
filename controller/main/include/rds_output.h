#ifndef RDS_OUTPUT_H
#define RDS_OUTPUT_H

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    RADIO_PLAYBACK_IDLE = 0,
    RADIO_PLAYBACK_SEARCH,
    RADIO_PLAYBACK_PLAYING,
    RADIO_PLAYBACK_ERROR,
} radio_playback_state_t;

typedef struct {
    char station[33];
    char now_playing[161];  /* longer than the 64-char RDS RT field; scrolled if it overflows */
    char genre[25];
    char bitrate[17];
    char stream_url[193];
    uint8_t pty;
    radio_playback_state_t playback_state;
} radio_metadata_t;

esp_err_t rds_output_init(void);
esp_err_t rds_output_update(const radio_metadata_t *metadata);
const char *rds_output_playback_state_name(radio_playback_state_t state);
uint8_t rds_output_pty_for_genre(const char *genre);
bool rds_output_get_clock_state(void);
bool rds_output_get_data_state(void);
uint32_t rds_output_get_isr_tick_count(void);
bool rds_output_is_running(void);
uint16_t rds_output_get_pi(void);

#define RDS_DECODED_GROUP_COUNT 33

typedef struct {
    uint16_t pi;
    uint16_t block_b;
    uint16_t block_c;
    uint16_t block_d;
    uint8_t group_type;   /* 0, 2, or 4 (0A/2A/4A); other values not generated */
    char version;         /* 'A' or 'B' */
    uint8_t address;      /* PS address 0-3, or RT address 0-15 */
    char text[5];          /* decoded PS (2 chars) or RT (4 chars) segment */
    char clock_text[36];   /* decoded "YYYY-MM-DD HH:MM (UTC+H:MM)" for group 4A */
    bool valid;            /* true if all four block checkwords match */
} rds_decoded_group_t;

/**
 * Decode the currently active generated RDS bitstream back into its 33
 * groups, recomputing and validating each block's CRC-10 checkword. Mirrors
 * what an external RDS decoder listening on RDS-C/RDS-D would reconstruct.
 */
void rds_output_decode_groups(rds_decoded_group_t *groups_out, size_t max_groups, size_t *count_out);

#endif