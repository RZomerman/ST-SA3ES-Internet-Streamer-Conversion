/**
 * @file config.h
 * @brief Configuration for LC72130 emulator: GPIO pins, operational modes, logging levels
 */

#ifndef CONFIG_H
#define CONFIG_H

#include <stdint.h>
#include "esp_err.h"

/* ============================================================================
 * GPIO Pin Assignments (ESP32-S3)
 * ============================================================================ */

#define GPIO_CE_INPUT      15   /**< Chip Enable input from Sony controller */
#define GPIO_CLK_INPUT     16   /**< Serial Clock input from Sony */
#define GPIO_DATA_INPUT    17   /**< Data input (MOSI) from Sony */
#define GPIO_DIN_OUTPUT    18   /**< D-IN output (status/DO) to Sony */

#define GPIO_AST            3
#define GPIO_ST             2
#define GPIO_SI             9
#define GPIO_MUTE_OUTPUT    7
#define GPIO_RDS_DATA      11
#define GPIO_RDS_CLOCK     12
#define GPIO_AUDIO_READY   13
#define GPIO_AUDIO_RESET   14
#define GPIO_MUTE_INPUT     8
#define GPIO_BLN_INPUT     10

/* Optional tri-state control (future use) */
#define GPIO_DIN_OE        19   /**< D-IN output-enable (currently unused, OE tied LOW) */

/* ============================================================================
 * Operational Modes
 * ============================================================================ */

typedef enum {
    MODE_PASSIVE_MONITOR = 0,  /**< Listen & log only, D-IN inactive */
    MODE_EMULATOR        = 1,  /**< Full emulation with D-IN responses */
    MODE_CAPTURE_RAW     = 2,  /**< Log timestamped bus events */
    MODE_TEST_GENERATOR  = 3   /**< Unit test mode, do not drive Sony bus */
} lc72130_operating_mode_t;

#define DEFAULT_OPERATING_MODE MODE_EMULATOR

/* ============================================================================
 * Protocol Timing
 * ============================================================================ */

#define LC72130_BITS_PER_FRAME  24      /**< 3 bytes × 8 bits */
#define LC72130_BYTES_PER_FRAME 3
#define LC72130_CLK_FREQ_HZ     200000  /**< Nominal CLK frequency (~200 kHz) */
#define LC72130_BIT_PERIOD_US   5       /**< Nominal bit period (200 kHz) */

/**
 * CLK timing tolerances (percent of nominal period)
 * Allows for modest variations without decoding failure
 */
#define CLK_PERIOD_TOLERANCE_PERCENT 30

/* ============================================================================
 * RMT Configuration (for D-IN output)
 * ============================================================================ */

#define RMT_CHANNEL_DIN 0       /**< RMT channel for D-IN output */
#define RMT_CLK_DIV     1       /**< RMT internal clock divider */
#define RMT_MEM_BLOCKS  1       /**< RMT memory blocks allocated */

/* ============================================================================
 * Buffers & Queues
 * ============================================================================ */

#define FRAME_BUFFER_SIZE       24      /**< Max bits per frame */
#define EVENT_LOG_QUEUE_SIZE    256     /**< Events pending serial output */
#define TRANSACTION_HISTORY     32      /**< Recent transactions kept */
#define RAW_EVENT_BUFFER_SIZE   1024    /**< Timestamped CE/CLK/DATA/DIN events */

/* ============================================================================
 * Log Levels
 * ============================================================================ */

typedef enum {
    LOG_LEVEL_NONE    = 0,
    LOG_LEVEL_ERROR   = 1,
    LOG_LEVEL_WARN    = 2,
    LOG_LEVEL_INFO    = 3,
    LOG_LEVEL_DEBUG   = 4,
    LOG_LEVEL_TRACE   = 5
} log_level_t;

#define DEFAULT_LOG_LEVEL LOG_LEVEL_INFO

/* ============================================================================
 * Feature Flags
 * ============================================================================ */

/**
 * If true, GPIO18 (D-IN) is never configured as output, preventing
 * any risk of driving the Sony bus during development/testing
 */
#define FEATURE_DIN_DISABLED_AT_COMPILE_TIME 0

/**
 * If true, capture timestamped bus events into a ring buffer
 * for post-transaction analysis
 */
#define FEATURE_RAW_EVENT_CAPTURE 1

/**
 * If true, maintain detailed transaction history
 */
#define FEATURE_TRANSACTION_HISTORY 1

/**
 * If true, attempt to detect and log frequency tuning sequences
 * (e.g., small offsets, AFC behavior)
 */
#define FEATURE_TUNING_SEQUENCE_DETECTION 1

/* ============================================================================
 * D-IN Output Configuration (when in EMULATOR mode)
 * ============================================================================ */

/**
 * Default D-IN response byte (status).
 * Bit meanings per LC72130 datasheet:
 *   Bit 7: Test/Reserved (0)
 *   Bit 6: PLL Lock Status (1 = locked)
 *   Bit 5: Tuner Ready (1 = ready)
 *   Bit 4: Input Port 2 (configurable)
 *   Bit 3: Input Port 1 (configurable)
 *   Bit 2: IF-counter/Status (configurable)
 *   Bit 1-0: Reserved (0)
 *
 * Default response: PLL locked + tuner ready + others off
 */
#define DEFAULT_DIN_RESPONSE 0x60

/**
 * If true, drive D-IN in response to READ transactions.
 * If false, D-IN remains inactive even in EMULATOR mode.
 */
#define FEATURE_DRIVE_DIN_ON_READ 1

/* ============================================================================
 * Safety & Watchdog
 * ============================================================================ */

/**
 * Enable Task Watchdog Timer (TWDT) to detect task hangs
 */
#define FEATURE_TASK_WATCHDOG 1

/**
 * Timeout for a single transaction capture (milliseconds)
 */
#define TRANSACTION_TIMEOUT_MS 1000

/* ============================================================================
 * Serial Output
 * ============================================================================ */

#define SERIAL_BAUD_RATE 115200

/* ============================================================================
 * Public API
 * ============================================================================ */

/**
 * Initialize configuration system and validate GPIO assignments
 * @return ESP_OK on success
 */
esp_err_t config_init(void);

/**
 * Get the current operating mode
 */
lc72130_operating_mode_t config_get_operating_mode(void);

/**
 * Set the operating mode (for runtime reconfiguration)
 */
void config_set_operating_mode(lc72130_operating_mode_t mode);

/**
 * Get the default D-IN response byte
 */
uint8_t config_get_din_response(void);

/**
 * Set the D-IN response byte (configurable status fields)
 */
void config_set_din_response(uint8_t response);

/**
 * Get current log level
 */
log_level_t config_get_log_level(void);

/**
 * Set log level at runtime
 */
void config_set_log_level(log_level_t level);

#endif // CONFIG_H
