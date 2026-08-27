#include "sony_tuner_control.h"

#include <Arduino.h>
#include <driver/gpio.h>
#include <driver/spi_slave.h>
#include <esp32s3/rom/gpio.h>
#include <freertos/queue.h>
#include <freertos/task.h>
#include <soc/gpio_sig_map.h>

#include "config.h"

namespace sony_tuner {
namespace {

constexpr uint8_t kFrameBits = 24;
constexpr size_t kQueuedTransactions = 32;
constexpr int32_t kMinimumFmKHz = 87500;
constexpr int32_t kMaximumFmKHz = 108000;
QueueHandle_t selectionQueue = nullptr;
QueueHandle_t playbackQueue = nullptr;
TaskHandle_t tunerTaskHandle = nullptr;

uint8_t reverse8(uint8_t value) {
  value = (value >> 4) | (value << 4);
  value = ((value & 0xCC) >> 2) | ((value & 0x33) << 2);
  return ((value & 0xAA) >> 1) | ((value & 0x55) << 1);
}

void setStatusOutputs(bool stationSelected, bool streamPlaying) {
  gpio_set_level(static_cast<gpio_num_t>(SONY_TUNER_DIN_PIN), 1);
  gpio_set_level(static_cast<gpio_num_t>(SONY_TUNER_SIG_PIN), stationSelected);
  gpio_set_level(static_cast<gpio_num_t>(SONY_TUNER_AST_PIN), stationSelected);
  gpio_set_level(static_cast<gpio_num_t>(SONY_TUNER_ST_PIN),
                 stationSelected && streamPlaying);
  gpio_set_level(static_cast<gpio_num_t>(SONY_TUNER_MUTE_PIN),
                 !(stationSelected && streamPlaying));
}

bool initializeSpiCapture() {
  gpio_config_t ceConfig{};
  ceConfig.pin_bit_mask = 1ULL << SONY_TUNER_CE_PIN;
  ceConfig.mode = GPIO_MODE_INPUT;
  ceConfig.pull_up_en = GPIO_PULLUP_DISABLE;
  ceConfig.pull_down_en = GPIO_PULLDOWN_DISABLE;
  ceConfig.intr_type = GPIO_INTR_DISABLE;
  ESP_ERROR_CHECK(gpio_config(&ceConfig));

  spi_bus_config_t busConfig{};
  busConfig.mosi_io_num = SONY_TUNER_DATA_PIN;
  busConfig.miso_io_num = -1;
  busConfig.sclk_io_num = SONY_TUNER_CLK_PIN;
  busConfig.quadwp_io_num = -1;
  busConfig.quadhd_io_num = -1;
  busConfig.data4_io_num = -1;
  busConfig.data5_io_num = -1;
  busConfig.data6_io_num = -1;
  busConfig.data7_io_num = -1;
  busConfig.max_transfer_sz = sizeof(uint32_t);

  spi_slave_interface_config_t slaveConfig{};
  slaveConfig.spics_io_num = -1;
  slaveConfig.flags = SPI_SLAVE_RXBIT_LSBFIRST;
  slaveConfig.queue_size = kQueuedTransactions;
  slaveConfig.mode = 2;

  const esp_err_t result = spi_slave_initialize(
      SPI2_HOST, &busConfig, &slaveConfig, SPI_DMA_DISABLED);
  if (result != ESP_OK) {
    ESP_LOGE("sony_tuner", "SPI2 slave initialization failed: %s",
             esp_err_to_name(result));
    return false;
  }

  gpio_matrix_in(SONY_TUNER_CE_PIN, FSPICS0_IN_IDX, true);
  ESP_LOGI("sony_tuner", "SPI2 ready: CE=%d CLK=%d DATA=%d, mode 2",
           SONY_TUNER_CE_PIN, SONY_TUNER_CLK_PIN, SONY_TUNER_DATA_PIN);
  return true;
}

void tunerTask(void*) {
  int32_t lastRfKHz = -1;
  bool stationSelected = false;
  bool streamPlaying = false;
  bool waitingForStoppedState = false;
  static uint32_t receiveBuffers[kQueuedTransactions]{};
  static spi_slave_transaction_t transactions[kQueuedTransactions]{};

  setStatusOutputs(false, false);
  vTaskDelay(pdMS_TO_TICKS(3500));
  if (!initializeSpiCapture()) vTaskDelete(nullptr);
  for (size_t index = 0; index < kQueuedTransactions; ++index) {
    transactions[index].length = kFrameBits;
    transactions[index].rx_buffer = &receiveBuffers[index];
    const esp_err_t result =
        spi_slave_queue_trans(SPI2_HOST, &transactions[index], portMAX_DELAY);
    if (result != ESP_OK) {
      ESP_LOGE("sony_tuner", "SPI receive queue failed: %s",
               esp_err_to_name(result));
    }
  }
  ESP_LOGI("sony_tuner", "%u SPI receive transactions armed",
           kQueuedTransactions);

  while (true) {
    spi_slave_transaction_t* completedTransaction = nullptr;
    if (spi_slave_get_trans_result(SPI2_HOST, &completedTransaction,
                                   pdMS_TO_TICKS(10)) == ESP_OK) {
      const size_t index = completedTransaction - transactions;
      const uint8_t* bytes =
          reinterpret_cast<const uint8_t*>(&receiveBuffers[index]);
      const uint32_t wireBits =
          (static_cast<uint32_t>(reverse8(bytes[0])) << 16) |
          (static_cast<uint32_t>(reverse8(bytes[1])) << 8) |
          reverse8(bytes[2]);
      SonyTunerFrame frame;
      if (completedTransaction->trans_len == 0) {
        // A disconnected or floating CE input can complete empty transactions.
      } else if (completedTransaction->trans_len != kFrameBits) {
        ESP_LOGW("sony_tuner", "Rejected SPI frame with %u clocks",
                 completedTransaction->trans_len);
      } else if (!decodeSonyFmFrame(wireBits, frame)) {
        ESP_LOGW("sony_tuner",
                 "Rejected FM frame raw=%02X %02X %02X, calculated=%ld kHz",
                 frame.raw[0], frame.raw[1], frame.raw[2], frame.rfKHz);
      } else if (frame.rfKHz != lastRfKHz) {
        lastRfKHz = frame.rfKHz;
        StationSelection selection;
        selection.rfKHz = frame.rfKHz;
        selection.mapped = stationForFrequency(frame.rfKHz, selection.station);
        xQueueOverwrite(selectionQueue, &selection);

        stationSelected = selection.mapped;
        streamPlaying = false;
        waitingForStoppedState = stationSelected;
        setStatusOutputs(stationSelected, false);
        ESP_LOGI("sony_tuner",
                 "FM %.2f MHz raw=%02X %02X %02X control=%02X station=%s",
                 frame.rfKHz / 1000.0, frame.raw[0], frame.raw[1],
                 frame.raw[2], frame.control,
                 selection.mapped ? selection.station.name : "unmapped");
      }
      receiveBuffers[index] = 0;
      completedTransaction->trans_len = 0;
      const esp_err_t result = spi_slave_queue_trans(
          SPI2_HOST, completedTransaction, portMAX_DELAY);
      if (result != ESP_OK) {
        ESP_LOGE("sony_tuner", "SPI receive requeue failed: %s",
                 esp_err_to_name(result));
      }
    }

    bool requestedPlayback = false;
    if (xQueueReceive(playbackQueue, &requestedPlayback, 0) == pdTRUE) {
      if (!stationSelected) {
        streamPlaying = false;
      } else if (waitingForStoppedState) {
        if (!requestedPlayback) {
          waitingForStoppedState = false;
        }
        streamPlaying = false;
      } else {
        streamPlaying = requestedPlayback;
      }
      setStatusOutputs(stationSelected, streamPlaying);
    }
    vTaskDelay(pdMS_TO_TICKS(1));
  }
}

} // namespace

bool decodeSonyFmFrame(uint32_t wireBits, SonyTunerFrame& frame) {
  frame.raw[0] = reverse8(static_cast<uint8_t>(wireBits >> 16));
  frame.raw[1] = reverse8(static_cast<uint8_t>(wireBits >> 8));
  frame.raw[2] = reverse8(static_cast<uint8_t>(wireBits));
  frame.divider = static_cast<uint16_t>(frame.raw[0]) |
                  (static_cast<uint16_t>(frame.raw[1]) << 8);
  frame.control = frame.raw[2];
  frame.rfKHz = static_cast<int32_t>(frame.divider) * 50 - 10800;
  frame.valid = frame.rfKHz >= kMinimumFmKHz &&
                frame.rfKHz <= kMaximumFmKHz &&
                frame.rfKHz % 50 == 0;
  return frame.valid;
}

bool begin() {
  if (tunerTaskHandle != nullptr) return false;

  SonyTunerFrame knownFrame;
  SonyTunerFrame radio538Frame;
  SonyTunerFrame bnrFrame;
  StationMapEntry station;
  if (!decodeSonyFmFrame(0x87E054, knownFrame) ||
      knownFrame.rfKHz != 90050 || knownFrame.divider != 0x07E1 ||
      !decodeSonyFmFrame(0x07E054, radio538Frame) ||
      radio538Frame.rfKHz != 90000 || radio538Frame.divider != 0x07E0 ||
      !decodeSonyFmFrame(0x2FE054, bnrFrame) ||
      bnrFrame.rfKHz != 91000 || bnrFrame.divider != 0x07F4 ||
      !stationForFrequency(90000, station) ||
      !stationForFrequency(91000, station)) {
    return false;
  }

  selectionQueue = xQueueCreate(1, sizeof(StationSelection));
  playbackQueue = xQueueCreate(1, sizeof(bool));
  if (selectionQueue == nullptr || playbackQueue == nullptr) {
    return false;
  }

  gpio_set_direction(static_cast<gpio_num_t>(SONY_TUNER_DIN_PIN),
                     GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(SONY_TUNER_MUTE_PIN), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(SONY_TUNER_AST_PIN), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(SONY_TUNER_ST_PIN), GPIO_MODE_OUTPUT);
  gpio_set_direction(static_cast<gpio_num_t>(SONY_TUNER_SIG_PIN), GPIO_MODE_OUTPUT);
  setStatusOutputs(false, false);

  return xTaskCreatePinnedToCore(tunerTask, "SonyTuner", 3072, nullptr, 3,
                                 &tunerTaskHandle, 0) == pdPASS;
}

bool takeStationSelection(StationSelection& selection) {
  return selectionQueue != nullptr &&
         xQueueReceive(selectionQueue, &selection, 0) == pdTRUE;
}

void setPlaybackActive(bool active) {
  if (playbackQueue != nullptr) xQueueOverwrite(playbackQueue, &active);
}

} // namespace sony_tuner