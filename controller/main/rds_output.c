#include "rds_output.h"

#include <ctype.h>
#include <stdio.h>
#include <string.h>

#include "driver/gpio.h"
#include "driver/gptimer.h"
#include "esp_attr.h"
#include "esp_check.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include "config.h"
#include "rds_clock.h"

#define RDS_PI_CODE_DEFAULT 0x89FF
#define RDS_GROUP_BITS 104
#define RDS_GROUPS_PER_CYCLE 33
#define RDS_CYCLE_BITS (RDS_GROUP_BITS * RDS_GROUPS_PER_CYCLE)

static const char *TAG = "RDS_OUTPUT";
static uint8_t streams[2][RDS_CYCLE_BITS];
static volatile uint8_t active_stream;
static volatile bool pending_stream;
static volatile size_t bit_index;
static volatile bool clock_high;
static bool timer_started;
static bool text_ab;
static char previous_radio_text[64];
static char previous_station[33];
static char previous_scroll_source[161];
static size_t scroll_offset;
static uint16_t current_pi = RDS_PI_CODE_DEFAULT;
static gptimer_handle_t timer;
static portMUX_TYPE stream_lock = portMUX_INITIALIZER_UNLOCKED;
static volatile uint32_t g_isr_tick_count;

/* Deterministic per-station PI so each station name maps to a stable,
 * distinct-looking code rather than one fixed value for every station. */
static uint16_t pi_from_station(const char *station)
{
    uint32_t hash = 2166136261u;  /* FNV-1a 32-bit offset basis */
    for (const unsigned char *p = (const unsigned char *)station; *p; p++) {
        hash ^= *p;
        hash *= 16777619u;  /* FNV-1a prime */
    }
    uint16_t pi = (uint16_t)((hash ^ (hash >> 16)) & 0xFFFF);
    if (pi == 0x0000 || pi == 0xFFFF) {
        pi = RDS_PI_CODE_DEFAULT;
    }
    return pi;
}

uint16_t rds_output_get_pi(void)
{
    return current_pi;
}

const char *rds_output_playback_state_name(radio_playback_state_t state)
{
    switch (state) {
        case RADIO_PLAYBACK_SEARCH:  return "search";
        case RADIO_PLAYBACK_PLAYING: return "playing";
        case RADIO_PLAYBACK_ERROR:   return "error";
        default:                     return "idle";
    }
}

static uint16_t crc10(uint16_t information_word, uint16_t offset)
{
    uint32_t remainder = (uint32_t)information_word << 10;
    for (int bit = 25; bit >= 10; bit--) {
        if (remainder & (1UL << bit)) {
            remainder ^= 0x5B9UL << (bit - 10);
        }
    }
    return (remainder & 0x3FF) ^ offset;
}

static void append_block(uint8_t *stream, size_t *position, uint16_t word, uint16_t offset)
{
    uint32_t block = ((uint32_t)word << 10) | crc10(word, offset);
    for (int bit = 25; bit >= 0; bit--) {
        stream[(*position)++] = (block >> bit) & 1;
    }
}

static void append_group(uint8_t *stream, size_t *position,
                         uint16_t block_b, uint16_t block_c, uint16_t block_d)
{
    append_block(stream, position, current_pi, 0x0FC);
    append_block(stream, position, block_b, 0x198);
    append_block(stream, position, block_c, 0x168);
    append_block(stream, position, block_d, 0x1B4);
}

uint8_t rds_output_pty_for_genre(const char *genre)
{
    char normalized[25];
    size_t length = strnlen(genre, sizeof(normalized) - 1);
    for (size_t i = 0; i < length; i++) {
        normalized[i] = tolower((unsigned char)genre[i]);
    }
    normalized[length] = '\0';

    if (strstr(normalized, "news")) return 1;
    if (strstr(normalized, "sport")) return 4;
    if (strstr(normalized, "education")) return 5;
    if (strstr(normalized, "drama")) return 6;
    if (strstr(normalized, "culture")) return 7;
    if (strstr(normalized, "science")) return 8;
    if (strstr(normalized, "pop")) return 10;
    if (strstr(normalized, "top 40")) return 10;
    if (strstr(normalized, "top40")) return 10;
    if (strstr(normalized, "rock")) return 11;
    if (strstr(normalized, "easy")) return 12;
    if (strstr(normalized, "classic")) return 13;
    if (strstr(normalized, "weather")) return 16;
    if (strstr(normalized, "finance")) return 17;
    if (strstr(normalized, "children")) return 18;
    if (strstr(normalized, "religion")) return 20;
    if (strstr(normalized, "country")) return 25;
    if (strstr(normalized, "jazz")) return 24;
    return 15;
}

static void pad_text(char *destination, size_t width, const char *source)
{
    memset(destination, ' ', width);
    size_t length = strnlen(source, width);
    memcpy(destination, source, length);
}

/* RDS's G0 character set has no usable tab code, so a few spaces stand in
 * for "3 tabs" of visual breathing room between repeats. */
#define RDS_TEXT_SEPARATOR_WIDTH 12

/* Repeats short text to fill the field instead of leaving it blank, so
 * listeners see the message again quickly instead of a long blank stretch.
 * A blank gap is inserted between repeats so they read as separate
 * occurrences instead of running together. */
static void repeat_pad_text(char *destination, size_t width, const char *source)
{
    size_t text_length = strnlen(source, width);
    if (text_length == 0) {
        memset(destination, ' ', width);
        return;
    }

    size_t separator_width = RDS_TEXT_SEPARATOR_WIDTH;
    if (text_length + separator_width > width) {
        separator_width = (width > text_length) ? (width - text_length) : 0;
    }

    size_t filled = 0;
    while (filled < width) {
        size_t text_chunk = (width - filled) < text_length ? (width - filled) : text_length;
        memcpy(destination + filled, source, text_chunk);
        filled += text_chunk;
        if (filled >= width) {
            break;
        }
        size_t sep_chunk = (width - filled) < separator_width ? (width - filled) : separator_width;
        memset(destination + filled, ' ', sep_chunk);
        filled += sep_chunk;
    }
}

/* Characters advanced through the source text per RDS update (~every metadata
 * poll interval), so long text scrolls through the 64-char RT window instead
 * of being truncated. */
#define RDS_SCROLL_STEP 2

/* For text longer than the 64-char RT field, scroll a moving window through
 * it (with a gap before it repeats) instead of truncating. Resets to the
 * start whenever the source text itself changes. */
static void scroll_window_text(char *destination, size_t width, const char *source)
{
    size_t text_length = strnlen(source, sizeof(previous_scroll_source) - 1);
    if (text_length == 0) {
        memset(destination, ' ', width);
        return;
    }

    if (strncmp(previous_scroll_source, source, sizeof(previous_scroll_source)) != 0) {
        scroll_offset = 0;
        strlcpy(previous_scroll_source, source, sizeof(previous_scroll_source));
    }

    size_t total_length = text_length + RDS_TEXT_SEPARATOR_WIDTH;
    for (size_t i = 0; i < width; i++) {
        size_t pos = (scroll_offset + i) % total_length;
        destination[i] = (pos < text_length) ? source[pos] : ' ';
    }
    scroll_offset = (scroll_offset + RDS_SCROLL_STEP) % total_length;
}

/* RDS Group 4A: Clock Time and date (IEC 62106 Annex G bit layout). */
static void append_clock_time_group(uint8_t *stream, size_t *position)
{
    uint32_t mjd = 0;
    uint8_t hour = 0;
    uint8_t minute = 0;
    bool offset_negative = false;
    uint8_t offset_half_hours = 0;
    rds_clock_get_mjd_hm(&mjd, &hour, &minute, &offset_negative, &offset_half_hours);

    uint16_t block_b = 0x4000 | ((mjd >> 15) & 0x3);
    uint16_t block_c = (uint16_t)(((mjd & 0x7FFF) << 1) | ((hour >> 4) & 0x1));
    uint16_t block_d = (uint16_t)(((hour & 0xF) << 12) | ((minute & 0x3F) << 6) |
                                  ((offset_negative ? 1 : 0) << 5) | (offset_half_hours & 0x1F));
    append_group(stream, position, block_b, block_c, block_d);
}

static void build_stream(uint8_t *stream, const radio_metadata_t *metadata)
{
    char ps[8];
    char radio_text[64];
    pad_text(ps, sizeof(ps), metadata->station);
    if (strnlen(metadata->now_playing, sizeof(metadata->now_playing) - 1) > sizeof(radio_text)) {
        scroll_window_text(radio_text, sizeof(radio_text), metadata->now_playing);
    } else {
        repeat_pad_text(radio_text, sizeof(radio_text), metadata->now_playing);
    }

    if (memcmp(previous_radio_text, radio_text, sizeof(radio_text)) != 0) {
        text_ab = !text_ab;
        memcpy(previous_radio_text, radio_text, sizeof(radio_text));
    }

    if (strncmp(previous_station, metadata->station, sizeof(previous_station)) != 0) {
        current_pi = pi_from_station(metadata->station);
        strlcpy(previous_station, metadata->station, sizeof(previous_station));
        ESP_LOGI(TAG, "RDS PI recomputed: station='%s' PI=0x%04X", metadata->station, current_pi);
    }

    uint8_t pty = metadata->pty;
    static const uint16_t block_c_templates[4] = {0xE2AF, 0xCACD, 0xE2AF, 0xCACD};
    static const uint8_t di_bits[4] = {0, 1, 0, 1};
    size_t position = 0;

    for (uint8_t rt_address = 0; rt_address < 16; rt_address++) {
        uint8_t ps_address = rt_address & 3;
        uint16_t ps_b = 0x0408 | ((uint16_t)pty << 5) |
                        ((uint16_t)di_bits[ps_address] << 2) | ps_address;
        uint16_t ps_d = ((uint8_t)ps[ps_address * 2] << 8) |
                        (uint8_t)ps[ps_address * 2 + 1];
        append_group(stream, &position, ps_b, block_c_templates[ps_address], ps_d);

        uint16_t rt_b = 0x2400 | ((uint16_t)pty << 5) |
                        ((uint16_t)text_ab << 4) | rt_address;
        uint16_t rt_c = ((uint8_t)radio_text[rt_address * 4] << 8) |
                        (uint8_t)radio_text[rt_address * 4 + 1];
        uint16_t rt_d = ((uint8_t)radio_text[rt_address * 4 + 2] << 8) |
                        (uint8_t)radio_text[rt_address * 4 + 3];
        append_group(stream, &position, rt_b, rt_c, rt_d);
    }

    append_clock_time_group(stream, &position);
}

static bool IRAM_ATTR timer_callback(gptimer_handle_t timer_handle,
                                     const gptimer_alarm_event_data_t *event_data,
                                     void *user_data)
{
    g_isr_tick_count++;
    if (clock_high) {
        gpio_set_level(GPIO_RDS_CLOCK, 0);
        clock_high = false;
        bit_index++;
        if (bit_index == RDS_CYCLE_BITS) {
            bit_index = 0;
            portENTER_CRITICAL_ISR(&stream_lock);
            if (pending_stream) {
                active_stream ^= 1;
                pending_stream = false;
            }
            portEXIT_CRITICAL_ISR(&stream_lock);
        }
        gpio_set_level(GPIO_RDS_DATA, streams[active_stream][bit_index]);
    } else {
        gpio_set_level(GPIO_RDS_CLOCK, 1);
        clock_high = true;
    }

    return false;
}

esp_err_t rds_output_init(void)
{
    if (crc10(0x89FF, 0x0FC) != 0x0BA || crc10(0x042F, 0x198) != 0x2BA ||
        crc10(0xCACD, 0x168) != 0x0A7 || crc10(0x464D, 0x1B4) != 0x04B) {
        ESP_LOGE(TAG, "RDS CRC regression check failed");
        return ESP_ERR_INVALID_CRC;
    }

    gpio_config_t output_config = {
        .pin_bit_mask = (1ULL << GPIO_RDS_DATA) | (1ULL << GPIO_RDS_CLOCK),
        .mode = GPIO_MODE_INPUT_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_RETURN_ON_ERROR(gpio_config(&output_config), TAG, "Failed to configure RDS outputs");
    gpio_set_level(GPIO_RDS_DATA, 0);
    gpio_set_level(GPIO_RDS_CLOCK, 0);

    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,
        .direction = GPTIMER_COUNT_UP,
        .resolution_hz = 1000000,
    };
    ESP_RETURN_ON_ERROR(gptimer_new_timer(&timer_config, &timer), TAG,
                        "Failed to create RDS timer");
    gptimer_event_callbacks_t callbacks = {.on_alarm = timer_callback};
    ESP_RETURN_ON_ERROR(gptimer_register_event_callbacks(timer, &callbacks, NULL), TAG,
                        "Failed to register RDS timer callback");
    gptimer_alarm_config_t alarm_config = {
        .alarm_count = 421,
        .reload_count = 0,
        .flags.auto_reload_on_alarm = true,
    };
    ESP_RETURN_ON_ERROR(gptimer_set_alarm_action(timer, &alarm_config), TAG,
                        "Failed to configure RDS timer");
    ESP_RETURN_ON_ERROR(gptimer_enable(timer), TAG, "Failed to enable RDS timer");
    ESP_LOGI(TAG, "RDS outputs ready: DATA=GPIO%d CLOCK=GPIO%d", GPIO_RDS_DATA,
             GPIO_RDS_CLOCK);
    return ESP_OK;
}

esp_err_t rds_output_update(const radio_metadata_t *metadata)
{
    if (metadata == NULL || timer == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    uint8_t target = active_stream ^ 1;
    build_stream(streams[target], metadata);
    portENTER_CRITICAL(&stream_lock);
    pending_stream = true;
    portEXIT_CRITICAL(&stream_lock);

    if (!timer_started) {
        active_stream = target;
        pending_stream = false;
        bit_index = 0;
        gpio_set_level(GPIO_RDS_DATA, streams[active_stream][0]);
        ESP_RETURN_ON_ERROR(gptimer_start(timer), TAG, "Failed to start RDS output");
        timer_started = true;
    }

    ESP_LOGI(TAG, "RDS metadata: PS='%.8s' PTY=%u RT='%.64s'", metadata->station,
             metadata->pty, metadata->now_playing);
    return ESP_OK;
}

bool rds_output_get_clock_state(void)
{
    return gpio_get_level(GPIO_RDS_CLOCK);
}

bool rds_output_get_data_state(void)
{
    return gpio_get_level(GPIO_RDS_DATA);
}

uint32_t rds_output_get_isr_tick_count(void)
{
    return g_isr_tick_count;
}

bool rds_output_is_running(void)
{
    return timer_started;
}

static bool decode_block(const uint8_t *bits, size_t *position, uint16_t offset,
                         uint16_t *info_word_out)
{
    uint32_t block = 0;
    for (int bit = 25; bit >= 0; bit--) {
        block |= (uint32_t)bits[(*position)++] << bit;
    }
    uint16_t info_word = (block >> 10) & 0xFFFF;
    uint16_t checkword = block & 0x3FF;
    *info_word_out = info_word;
    return crc10(info_word, offset) == checkword;
}

void rds_output_decode_groups(rds_decoded_group_t *groups_out, size_t max_groups, size_t *count_out)
{
    uint8_t snapshot = active_stream;
    size_t position = 0;
    size_t count = 0;

    for (size_t group = 0; group < RDS_DECODED_GROUP_COUNT && count < max_groups; group++) {
        rds_decoded_group_t decoded = {0};
        bool valid_a = decode_block(streams[snapshot], &position, 0x0FC, &decoded.pi);
        bool valid_b = decode_block(streams[snapshot], &position, 0x198, &decoded.block_b);
        bool valid_c = decode_block(streams[snapshot], &position, 0x168, &decoded.block_c);
        bool valid_d = decode_block(streams[snapshot], &position, 0x1B4, &decoded.block_d);
        decoded.valid = valid_a && valid_b && valid_c && valid_d;

        decoded.group_type = (decoded.block_b >> 12) & 0xF;
        decoded.version = (decoded.block_b & 0x0800) ? 'B' : 'A';

        if (decoded.group_type == 0) {
            decoded.address = decoded.block_b & 0x3;
            decoded.text[0] = (char)((decoded.block_d >> 8) & 0xFF);
            decoded.text[1] = (char)(decoded.block_d & 0xFF);
            decoded.text[2] = '\0';
        } else if (decoded.group_type == 2) {
            decoded.address = decoded.block_b & 0xF;
            decoded.text[0] = (char)((decoded.block_c >> 8) & 0xFF);
            decoded.text[1] = (char)(decoded.block_c & 0xFF);
            decoded.text[2] = (char)((decoded.block_d >> 8) & 0xFF);
            decoded.text[3] = (char)(decoded.block_d & 0xFF);
            decoded.text[4] = '\0';
        } else if (decoded.group_type == 4) {
            uint32_t mjd = ((uint32_t)(decoded.block_b & 0x3) << 15) | (decoded.block_c >> 1);
            uint8_t hour = (uint8_t)(((decoded.block_c & 0x1) << 4) | (decoded.block_d >> 12));
            uint8_t minute = (uint8_t)((decoded.block_d >> 6) & 0x3F);
            bool offset_negative = (decoded.block_d >> 5) & 0x1;
            uint8_t offset_half_hours = decoded.block_d & 0x1F;
            int year, month, day;
            rds_clock_mjd_to_date(mjd, &year, &month, &day);
            snprintf(decoded.clock_text, sizeof(decoded.clock_text),
                    "%04d-%02d-%02d %02u:%02u UTC, offset %c%u:%02u", year, month, day, hour, minute,
                    offset_negative ? '-' : '+', offset_half_hours / 2, (offset_half_hours % 2) * 30);
        }

        groups_out[count++] = decoded;
    }

    *count_out = count;
}