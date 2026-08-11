#include "lcd_cam_capture.h"

#include <driver/gpio.h>
#include <esp_attr.h>
#include <esp_err.h>
#include <esp_rom_gpio.h>
#include <esp_private/gdma.h>
#include <esp_private/periph_ctrl.h>
#include <hal/cam_ll.h>
#include <hal/dma_types.h>
#include <soc/gpio_pins.h>
#include <soc/gpio_sig_map.h>
#include <soc/lcd_cam_struct.h>

namespace {
constexpr gpio_num_t kClockPin = GPIO_NUM_10;
constexpr gpio_num_t kCePin = GPIO_NUM_11;
constexpr gpio_num_t kDataPin = GPIO_NUM_18;

DMA_ATTR uint8_t capture_buffer[LcdCamCapture::kCaptureBytes];
DMA_ATTR dma_descriptor_align4_t capture_descriptor;
gdma_channel_handle_t dma_channel = nullptr;
volatile bool capture_complete = false;
volatile size_t captured_length = 0;
size_t capture_bytes = LcdCamCapture::kCaptureBytes;

bool IRAM_ATTR onDmaEof(gdma_channel_handle_t, gdma_event_data_t*, void*) {
  captured_length = capture_descriptor.dw0.length;
  capture_complete = true;
  return false;
}

bool IRAM_ATTR onDmaError(gdma_channel_handle_t, gdma_event_data_t*, void*) {
  captured_length = 0;
  capture_complete = true;
  return false;
}

bool check(esp_err_t result, const char*& error, const char* message) {
  if (result == ESP_OK) return true;
  error = message;
  return false;
}

void connectInput(gpio_num_t pin, uint32_t signal) {
  gpio_set_direction(pin, GPIO_MODE_INPUT);
  gpio_set_pull_mode(pin, GPIO_FLOATING);
  esp_rom_gpio_connect_in_signal(pin, signal, false);
}
}  // namespace

bool LcdCamCapture::configureHardware() {
  connectInput(kClockPin, CAM_PCLK_IDX);
  connectInput(kDataPin, CAM_DATA_IN0_IDX);
  connectInput(kCePin, CAM_DATA_IN1_IDX);
  for (uint32_t signal = CAM_DATA_IN2_IDX; signal <= CAM_DATA_IN7_IDX; ++signal) {
    esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, signal, false);
  }

  // The C701 has no camera framing lines. Hold the required camera gates active.
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, CAM_H_ENABLE_IDX, false);
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ZERO_INPUT, CAM_V_SYNC_IDX, false);
  esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, CAM_H_SYNC_IDX, false);

  periph_module_enable(PERIPH_LCD_CAM_MODULE);
  periph_module_reset(PERIPH_LCD_CAM_MODULE);
  cam_ll_set_input_data_width(&LCD_CAM, 8);
  cam_ll_set_vh_de_mode(&LCD_CAM, false);
  cam_ll_enable_invert_de(&LCD_CAM, false);
  cam_ll_enable_invert_hsync(&LCD_CAM, false);
  cam_ll_enable_invert_vsync(&LCD_CAM, false);
  cam_ll_enable_vsync_filter(&LCD_CAM, false);
  cam_ll_enable_vsync_generate_eof(&LCD_CAM, false);
  cam_ll_enable_rgb_yuv_convert(&LCD_CAM, false);
  cam_ll_reverse_dma_data_bit_order(&LCD_CAM, false);
  cam_ll_swap_dma_data_byte_order(&LCD_CAM, false);
  cam_ll_enable_stop_signal(&LCD_CAM, false);
  gdma_channel_alloc_config_t channel_config{};
  channel_config.direction = GDMA_CHANNEL_DIRECTION_RX;
  if (!check(gdma_new_ahb_channel(&channel_config, &dma_channel), error_, "GDMA channel allocation failed")) return false;
  if (!check(gdma_connect(dma_channel, GDMA_MAKE_TRIGGER(GDMA_TRIG_PERIPH_CAM, 0)), error_, "GDMA camera connection failed")) return false;
  const gdma_transfer_config_t transfer_config{.max_data_burst_size = 0, .access_ext_mem = false};
  if (!check(gdma_config_transfer(dma_channel, &transfer_config), error_, "GDMA transfer configuration failed")) return false;
  const gdma_strategy_config_t strategy{.owner_check = true, .auto_update_desc = false, .eof_till_data_popped = false};
  if (!check(gdma_apply_strategy(dma_channel, &strategy), error_, "GDMA strategy configuration failed")) return false;
  gdma_rx_event_callbacks_t callbacks{};
  callbacks.on_recv_eof = onDmaEof;
  callbacks.on_descr_err = onDmaError;
  if (!check(gdma_register_rx_event_callbacks(dma_channel, &callbacks, nullptr), error_, "GDMA callback registration failed")) return false;

  error_ = nullptr;
  return true;
}

bool LcdCamCapture::begin(bool vsync_high, bool ce_as_vsync, bool invert_pclk) {
  if (!initialized_) {
    if (!configureHardware()) return false;
    initialized_ = true;
  }
  cam_ll_enable_invert_pclk(&LCD_CAM, invert_pclk);
  if (ce_as_vsync) {
    esp_rom_gpio_connect_in_signal(kCePin, CAM_V_SYNC_IDX, false);
    capture_bytes_ = kFrameBytes;
  } else {
    if (vsync_high) esp_rom_gpio_connect_in_signal(GPIO_MATRIX_CONST_ONE_INPUT, CAM_V_SYNC_IDX, false);
    capture_bytes_ = kCaptureBytes;
  }
  capture_bytes = capture_bytes_;
  return true;
}

bool LcdCamCapture::arm() {
  if (dma_channel == nullptr) {
    setError("LCD-CAM is not initialized");
    return false;
  }
  capture_complete = false;
  captured_length = 0;
  capture_descriptor = {};
  capture_descriptor.dw0.size = capture_bytes;
  capture_descriptor.dw0.owner = DMA_DESCRIPTOR_BUFFER_OWNER_DMA;
  capture_descriptor.dw0.suc_eof = 1;
  capture_descriptor.buffer = capture_buffer;
  capture_descriptor.next = nullptr;
  cam_ll_stop(&LCD_CAM);
  cam_ll_reset(&LCD_CAM);
  cam_ll_fifo_reset(&LCD_CAM);
  cam_ll_set_recv_data_bytelen(&LCD_CAM, capture_bytes - 1);
  if (!check(gdma_reset(dma_channel), error_, "GDMA reset failed")) return false;
  if (!check(gdma_start(dma_channel, reinterpret_cast<intptr_t>(&capture_descriptor)), error_, "GDMA start failed")) return false;
  cam_ll_start(&LCD_CAM);
  return true;
}

bool LcdCamCapture::takeCompleted(const uint8_t*& samples, size_t& length) {
  if (!capture_complete) return false;
  capture_complete = false;
  samples = capture_buffer;
  length = captured_length;
  return true;
}