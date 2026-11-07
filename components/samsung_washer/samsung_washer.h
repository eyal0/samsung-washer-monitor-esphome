#pragma once

#include <cstdint>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "washer_parser.h"

namespace esphome::samsung_washer {

class SamsungWasherRawCaptureSwitch final : public switch_::Switch, public Component {
 public:
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
};

enum WasherModel : uint8_t {
  WASHER_MODEL_DC92_01950B,
  WASHER_MODEL_DC92_02395A,
};

class SamsungWasherComponent final : public PollingComponent, public uart::UARTDevice {
 public:
  SamsungWasherComponent() : PollingComponent(2000) {}

  void loop() override;
  void update() override;
  void dump_config() override;

  void set_model(WasherModel model) {
    this->model_ = model;
    this->primary_parser_ = WasherParser(model == WASHER_MODEL_DC92_02395A ? 33 : 32);
  }
  void set_secondary_uart(uart::UARTComponent *uart) { this->secondary_uart_ = uart; }
  void set_raw_capture_switch(switch_::Switch *sw) { this->raw_capture_switch_ = sw; }

  SUB_TEXT_SENSOR(cycle_status)
  SUB_TEXT_SENSOR(program_indicator)
  SUB_TEXT_SENSOR(temperature_selection)
  SUB_TEXT_SENSOR(spin_selection)
  SUB_TEXT_SENSOR(rinse_selection)
  SUB_TEXT_SENSOR(displayed_time)
  SUB_TEXT_SENSOR(option_selection)
  SUB_TEXT_SENSOR(soil_selection)

  SUB_BINARY_SENSOR(panel_on)
  SUB_BINARY_SENSOR(link_active)
  SUB_BINARY_SENSOR(bubble_indicator)
  SUB_BINARY_SENSOR(iron_indicator)
  SUB_BINARY_SENSOR(lock_indicator)

 protected:
  void reset_tracking_();
  void handle_primary_frame_(const std::vector<uint8_t> &p);
  void handle_dc92_02395a_frame_(const std::vector<uint8_t> &p);
  void flush_raw_capture_(const char *label, std::vector<uint8_t> &buf, std::vector<uint8_t> &last_row,
                          uint32_t last_byte_ms, bool force);
  void capture_raw_byte_(const char *label, std::vector<uint8_t> &buf, std::vector<uint8_t> &last_row,
                         uint32_t &last_byte_ms, uint8_t byte);

  uart::UARTComponent *secondary_uart_{nullptr};
  switch_::Switch *raw_capture_switch_{nullptr};

  WasherModel model_{WASHER_MODEL_DC92_01950B};
  WasherParser primary_parser_{32};
  WasherParser secondary_parser_{30};
  WasherCycleStatus cycle_tracker_{};

  uint32_t last_primary_ms_{0};
  uint32_t blank_since_ms_{0};
  bool has_primary_frame_{false};
  bool blank_pending_{false};
  bool panel_on_known_{false};
  bool panel_on_state_{false};

  std::vector<uint8_t> primary_raw_buf_{};
  std::vector<uint8_t> secondary_raw_buf_{};
  std::vector<uint8_t> primary_raw_last_row_{};
  std::vector<uint8_t> secondary_raw_last_row_{};
  uint32_t primary_raw_last_ms_{0};
  uint32_t secondary_raw_last_ms_{0};
};

}  // namespace esphome::samsung_washer
