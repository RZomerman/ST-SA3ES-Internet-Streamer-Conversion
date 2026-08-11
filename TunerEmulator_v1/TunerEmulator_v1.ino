#include <Arduino.h>
#include <soc/gpio_reg.h>

#include "din_status.h"
#include "lcd_cam_capture.h"
#include "receiver.h"
#include "tuner_state.h"

constexpr uint8_t CE_PIN = 11;
constexpr uint8_t CLK_PIN = 10;
constexpr uint8_t DATA_PIN = 18;
constexpr uint8_t DIN_PIN = 17;
constexpr bool ALLOW_DIN_DIAGNOSTIC = false;
constexpr uint8_t QUEUE_SIZE = 8;

portMUX_TYPE receiver_mux = portMUX_INITIALIZER_UNLOCKED;
volatile bool capture_active = false;
volatile uint8_t capture_bytes[3] = {0, 0, 0};
volatile uint8_t capture_bit_count = 0;
volatile bool capture_overflow = false;
Frame queue_frames[QUEUE_SIZE];
volatile uint8_t queue_head = 0;
volatile uint8_t queue_tail = 0;
volatile uint32_t incomplete_count = 0;
volatile uint32_t overflow_count = 0;
volatile uint32_t queue_overrun_count = 0;
volatile uint32_t ce_rising_count = 0;
volatile uint32_t ce_falling_count = 0;
volatile uint32_t active_clock_falling_count = 0;
volatile uint32_t complete_frame_count = 0;

TunerState tuner_state;
DinStatus din_status(DIN_PIN);
LcdCamCapture lcd_cam_capture;
bool verbose = false;
bool lcd_cam_active = false;
bool lcd_cam_monitor = false;
volatile bool lcd_cam_prefix_started = false;
volatile uint8_t lcd_cam_prefix_count = 0;
volatile uint8_t lcd_cam_prefix_bits[2] = {0, 0};

void IRAM_ATTR onClockFalling() {
  if (!capture_active) return;
  ++active_clock_falling_count;
  if (capture_bit_count >= 24) {
    capture_overflow = true;
    return;
  }
  const uint32_t pins = REG_READ(GPIO_IN_REG);
  if (pins & (1UL << DATA_PIN)) {
    capture_bytes[capture_bit_count >> 3] |= static_cast<uint8_t>(1U << (7 - (capture_bit_count & 7)));
  }
  ++capture_bit_count;
}

void IRAM_ATTR onCeChange() {
  const bool ce_high = REG_READ(GPIO_IN_REG) & (1UL << CE_PIN);
  if (ce_high) {
    ++ce_rising_count;
    capture_bytes[0] = capture_bytes[1] = capture_bytes[2] = 0;
    capture_bit_count = 0;
    capture_overflow = false;
    capture_active = true;
    return;
  }
  ++ce_falling_count;
  if (!capture_active) return;
  capture_active = false;
  if (capture_bit_count != 24 || capture_overflow) {
    if (capture_overflow) ++overflow_count;
    else ++incomplete_count;
    return;
  }
  const uint8_t next_head = static_cast<uint8_t>((queue_head + 1) % QUEUE_SIZE);
  if (next_head == queue_tail) {
    ++queue_overrun_count;
    return;
  }
  queue_frames[queue_head].bytes[0] = capture_bytes[0];
  queue_frames[queue_head].bytes[1] = capture_bytes[1];
  queue_frames[queue_head].bytes[2] = capture_bytes[2];
  queue_head = next_head;
  ++complete_frame_count;
}

void IRAM_ATTR onLcdCamCeRising() {
  if (lcd_cam_prefix_started) return;
  lcd_cam_prefix_started = true;
  lcd_cam_prefix_count = 0;
}

void IRAM_ATTR onLcdCamClockFalling() {
  if (!lcd_cam_prefix_started || lcd_cam_prefix_count >= 2) return;
  lcd_cam_prefix_bits[lcd_cam_prefix_count++] = (REG_READ(GPIO_IN_REG) & (1UL << DATA_PIN)) ? 1 : 0;
}

bool dequeueFrame(Frame& frame) {
  portENTER_CRITICAL(&receiver_mux);
  if (queue_tail == queue_head) {
    portEXIT_CRITICAL(&receiver_mux);
    return false;
  }
  frame = queue_frames[queue_tail];
  queue_tail = static_cast<uint8_t>((queue_tail + 1) % QUEUE_SIZE);
  tuner_state.incomplete_frames = incomplete_count;
  tuner_state.overflow_frames = overflow_count;
  tuner_state.queue_overruns = queue_overrun_count;
  portEXIT_CRITICAL(&receiver_mux);
  return true;
}

void printFrame(const Frame& frame, const DecodedFrame& decoded) {
  Serial.printf("RX %02X %02X %02X | band=%s", frame.bytes[0], frame.bytes[1], frame.bytes[2],
                bandName(tuner_state.band));
  if (decoded.type == FrameType::FREQUENCY) {
    if (tuner_state.band == Band::FM) Serial.printf(" frequency=%.2fMHz", tuner_state.frequency_hz / 1000000.0);
    else Serial.printf(" frequency=%ldkHz", static_cast<long>(tuner_state.frequency_hz / 1000));
    if (tuner_state.antenna != Antenna::UNKNOWN) Serial.printf(" antenna=%s", antennaName(tuner_state.antenna));
  } else {
    if (decoded.antenna != Antenna::UNKNOWN) Serial.printf(" antenna=%s", antennaName(decoded.antenna));
    Serial.printf(" control=%s", frameTypeName(decoded.type));
  }
  Serial.println();
}

void processLcdCamSamples(const uint8_t* samples, size_t length, bool use_prefix, bool diagnostics) {
  uint32_t ce_high_samples = 0;
  uint32_t decoded_frames = 0;
  char raw_bits[25]{};
  const size_t raw_bit_count = length < 24 ? length : 24;
  for (size_t index = 0; index < raw_bit_count; ++index) raw_bits[index] = (samples[index] & 0x01) ? '1' : '0';
  const size_t frame_count = use_prefix ? (length >= 22 ? 1 : 0) : length / 24;
  for (size_t frame_index = 0; frame_index < frame_count; ++frame_index) {
    const size_t start = use_prefix ? 0 : frame_index * 24;
    Frame frame{{0, 0, 0}};
    for (uint8_t bit = 0; bit < 24; ++bit) {
      const uint8_t sample_index = use_prefix ? bit - 2 : bit;
      const uint8_t sample = bit < 2 ? 0 : samples[start + sample_index];
      if (sample & 0x02) ++ce_high_samples;
      const bool data_high = bit < 2 ? lcd_cam_prefix_bits[bit] : (sample & 0x01);
      if (data_high) frame.bytes[bit >> 3] |= static_cast<uint8_t>(1U << (7 - (bit & 7)));
    }
    const DecodedFrame decoded = decodeFrame(frame);
    const bool changed = applyFrame(tuner_state, frame, millis());
    if (diagnostics) {
      Serial.printf("LCDCAM RX %02X %02X %02X | type=%s band=%s%s\n", frame.bytes[0], frame.bytes[1], frame.bytes[2],
                    frameTypeName(decoded.type), bandName(decoded.band), changed ? " state-change" : "");
    } else if (decoded.type == FrameType::FREQUENCY && changed) {
      if (decoded.band == Band::FM) {
        Serial.printf("MONITOR FM %.2fMHz | RX %02X %02X %02X\n", decoded.frequency_hz / 1000000.0,
                      frame.bytes[0], frame.bytes[1], frame.bytes[2]);
      } else {
        Serial.printf("MONITOR %s %ldkHz | RX %02X %02X %02X\n", bandName(decoded.band),
                      static_cast<long>(decoded.frequency_hz / 1000), frame.bytes[0], frame.bytes[1], frame.bytes[2]);
      }
    }
    ++decoded_frames;
  }
  if (!diagnostics) return;
  Serial.printf("LCDCAM samples=%u frames=%lu ce_high=%lu prefix=%u remainder=%u\n", static_cast<unsigned>(length),
                static_cast<unsigned long>(decoded_frames), static_cast<unsigned long>(ce_high_samples),
                static_cast<unsigned>(lcd_cam_prefix_count),
                static_cast<unsigned>(length % 24));
  Serial.printf("LCDCAM raw=%s prefix_bits=%u%u\n", raw_bits, lcd_cam_prefix_bits[0], lcd_cam_prefix_bits[1]);
}

bool armLcdCamCapture(bool diagnostics) {
  detachInterrupt(digitalPinToInterrupt(CLK_PIN));
  detachInterrupt(digitalPinToInterrupt(CE_PIN));
  if (!lcd_cam_capture.begin(false, true, false) || !lcd_cam_capture.arm()) {
    Serial.printf("LCDCAM error: %s\n", lcd_cam_capture.error());
    return false;
  }
  lcd_cam_prefix_started = false;
  lcd_cam_prefix_count = 0;
  attachInterrupt(digitalPinToInterrupt(CE_PIN), onLcdCamCeRising, RISING);
  attachInterrupt(digitalPinToInterrupt(CLK_PIN), onLcdCamClockFalling, FALLING);
  lcd_cam_active = true;
  if (diagnostics) Serial.println("LCDCAM armed for 24 external-clock samples (vsync=CE pclk=rising)");
  return true;
}

void printReceiverStats() {
  uint32_t ce_rising, ce_falling, active_clocks, complete_frames, incomplete_frames, overflow_frames, queue_overruns;
  portENTER_CRITICAL(&receiver_mux);
  ce_rising = ce_rising_count;
  ce_falling = ce_falling_count;
  active_clocks = active_clock_falling_count;
  complete_frames = complete_frame_count;
  incomplete_frames = incomplete_count;
  overflow_frames = overflow_count;
  queue_overruns = queue_overrun_count;
  portEXIT_CRITICAL(&receiver_mux);
  Serial.printf("STATS ce_rise=%lu ce_fall=%lu clk_fall_active=%lu complete=%lu incomplete=%lu overflow=%lu queue_overrun=%lu\n",
                static_cast<unsigned long>(ce_rising), static_cast<unsigned long>(ce_falling),
                static_cast<unsigned long>(active_clocks), static_cast<unsigned long>(complete_frames),
                static_cast<unsigned long>(incomplete_frames), static_cast<unsigned long>(overflow_frames),
                static_cast<unsigned long>(queue_overruns));
}

void resetReceiverStats() {
  portENTER_CRITICAL(&receiver_mux);
  ce_rising_count = ce_falling_count = 0;
  active_clock_falling_count = complete_frame_count = 0;
  incomplete_count = overflow_count = queue_overrun_count = 0;
  portEXIT_CRITICAL(&receiver_mux);
  Serial.println("stats reset");
}

void pollSerialCommands() {
  if (!Serial.available()) return;
  const String command = Serial.readStringUntil('\n');
  if (command == "verbose on") { verbose = true; Serial.println("verbose=on"); }
  else if (command == "verbose off") { verbose = false; Serial.println("verbose=off"); }
  else if (command == "lcdcam monitor on") {
    lcd_cam_monitor = armLcdCamCapture(false);
    if (lcd_cam_monitor) Serial.println("LCDCAM monitor=on; reports recognized frequency state changes");
  } else if (command == "lcdcam monitor off") {
    lcd_cam_monitor = false;
    Serial.println("LCDCAM monitor=off; stops after the active capture completes");
  }
  else if (command == "lcdcam on" || command == "lcdcam vsync-high" || command == "lcdcam ce-vsync" || command == "lcdcam ce-vsync-rising") {
    lcd_cam_monitor = false;
    detachInterrupt(digitalPinToInterrupt(CLK_PIN));
    detachInterrupt(digitalPinToInterrupt(CE_PIN));
    const bool vsync_high = command == "lcdcam vsync-high";
    const bool ce_as_vsync = command == "lcdcam ce-vsync";
    const bool invert_pclk = command != "lcdcam ce-vsync-rising";
    const bool ce_vsync_mode = ce_as_vsync || command == "lcdcam ce-vsync-rising";
    if (!lcd_cam_capture.begin(vsync_high, ce_vsync_mode, invert_pclk) || !lcd_cam_capture.arm()) {
      Serial.printf("LCDCAM error: %s\n", lcd_cam_capture.error());
    } else {
      lcd_cam_prefix_started = false;
      lcd_cam_prefix_count = 0;
      if (ce_vsync_mode) {
        attachInterrupt(digitalPinToInterrupt(CE_PIN), onLcdCamCeRising, RISING);
        attachInterrupt(digitalPinToInterrupt(CLK_PIN), onLcdCamClockFalling, FALLING);
      }
      lcd_cam_active = true;
      Serial.printf("LCDCAM armed for %u external-clock samples (%s pclk=%s)\n", ce_vsync_mode ? 24 : 696,
                    ce_vsync_mode ? "vsync=CE" : (vsync_high ? "vsync=high" : "vsync=low"),
                    invert_pclk ? "falling" : "rising");
    }
  }
  else if (command == "stats") printReceiverStats();
  else if (command == "stats reset") resetReceiverStats();
  else if (command.startsWith("din-low ")) {
    if (!ALLOW_DIN_DIAGNOSTIC) Serial.println("DIN diagnostic is disabled at build time");
    else din_status.pullLowFor(command.substring(8).toInt());
  }
}

void setup() {
  Serial.begin(115200);
  pinMode(CE_PIN, INPUT);
  pinMode(CLK_PIN, INPUT);
  pinMode(DATA_PIN, INPUT);
  din_status.begin();
  attachInterrupt(digitalPinToInterrupt(CLK_PIN), onClockFalling, FALLING);
  attachInterrupt(digitalPinToInterrupt(CE_PIN), onCeChange, CHANGE);
  Serial.println("IC701 tuner emulator: DIN released, verbose=off");
}

void loop() {
  din_status.service();
  pollSerialCommands();
  if (lcd_cam_active) {
    const uint8_t* samples;
    size_t length;
    if (lcd_cam_capture.takeCompleted(samples, length)) {
      lcd_cam_active = false;
      detachInterrupt(digitalPinToInterrupt(CLK_PIN));
      detachInterrupt(digitalPinToInterrupt(CE_PIN));
      processLcdCamSamples(samples, length, lcd_cam_prefix_count == 2, !lcd_cam_monitor);
      if (lcd_cam_monitor && !armLcdCamCapture(false)) lcd_cam_monitor = false;
      else if (!lcd_cam_monitor) Serial.println("LCDCAM stopped; send 'lcdcam on' to arm the next recall");
    }
  }
  Frame frame;
  while (dequeueFrame(frame)) {
    const DecodedFrame decoded = decodeFrame(frame);
    const bool changed = applyFrame(tuner_state, frame, millis());
    if (changed || verbose || decoded.type == FrameType::TRANSITION) printFrame(frame, decoded);
  }
}