//***************************************************************************************************
// config.h                                                                                         *
//***************************************************************************************************
// Configuration definition for your particular set-up.                                             *
//***************************************************************************************************
//
#ifndef CONFIG_H
  //#define NAME "ESP32-Radio"                              // Define name of the radio, also AP SSID,
                                                            // also namespace in NVS.
                                                            // Default is "ESP32-Radio"

  //#define SDCARD                                          // Experimental: For SD card support (reading MP3-files)

  //#define ETHERNET                                        // For wired Ethernet (WT32-ETH-01 or similar)

  #define FIXEDWIFI "Studiographic/HappyWifeHappyLife"      // Fixed WiFi network
  #define NO_LOCAL_CONTROLS                                 // Control arrives chip-to-chip later
  #define DEFAULT_STATION_HOST "playerservices.streamtheworld.com:80/api/livestream-redirect/RADIO538AAC.aac"
  #define DEFAULT_STATION_NAME "RADIO538"
  #define DEFAULT_RDS_PTY 10
  #define DEFAULT_VOLUME 72
  #define SPDIF_OUTPUT_PIN 7

  #define SONY_TUNER_CE_PIN 16
  #define SONY_TUNER_CLK_PIN 15
  #define SONY_TUNER_DIN_PIN 17
  #define SONY_TUNER_DATA_PIN 18
  #define SONY_TUNER_MUTE_PIN 8
  #define SONY_TUNER_AST_PIN 3
  #define SONY_TUNER_ST_PIN 2                             // GPIO46 is input-only on ESP32-S3
  #define SONY_TUNER_SIG_PIN 9

  #ifndef CONFIG_RADIO_CONTROLLER_UART_RX_GPIO
    #define CONFIG_RADIO_CONTROLLER_UART_RX_GPIO 40
  #endif
  #ifndef CONFIG_RADIO_CONTROLLER_UART_TX_GPIO
    #define CONFIG_RADIO_CONTROLLER_UART_TX_GPIO 41
  #endif
  #ifndef CONFIG_RADIO_CONTROLLER_AUDIO_RDY_GPIO
    #define CONFIG_RADIO_CONTROLLER_AUDIO_RDY_GPIO 42
  #endif
  
  #define ENABLEOTA                                        // OTA feature

  //#define TOGGLEMUTE                                      // "mute" command will toggle mute/unmute
  
  // Define (just one) type of MP3/AAC decoder
  //#define DEC_VS1053                                      // Hardware decoder for MP3, AAC, OGG
  //#define DEC_VS1003                                      // Hardware decoder for MP3 only
  //#define DEC_HELIX                                       // Software decoder for MP3, AAC. I2S output
  #define DEC_HELIX_SPDIF                                  // Toslink/Spdif output for MP3, AAC
  //#define DEC_HELIX_AI                                    // Software decoder for AI Audio kit (AC101)
  //#define DEC_HELIX_INT                                   // Software decoder for MP3, AAC. DAC output
  
  // Define (just one) type of display.  See documentation.
  //#define BLUETFT                                          // Works also for RED TFT 128x160
  //#define ST7789                                          // 240x240 TFT (SPI)
  //#define OLED1306                                        // 64x128 I2C OLED SSD1306
  //#define OLED1309                                        // 64x128 I2C OLED SSD1309
  //#define OLED1106                                        // 64x128 I2C OLED SH1106
  #define DUMMYTFT                                         // No local display
  //#define LCD1602I2C                                      // LCD 1602 display with I2C backpack
  //#define LCD2004I2C                                      // LCD 2004 display with I2C backpack
  //#define ILI9341                                         // ILI9341 240*320
  //#define NEXTION                                         // Nextion display
  //

  // Define ZIPPYB5 if a ZIPPY B5 Side Switch is used instead of a rotary switch
  //#define ZIPPYB5

  // End of configuration parameters.
  #define CONFIG_H
#endif
