#pragma once

#include <cstdint>
#include <vector>

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/number/number.h"
#include "esphome/components/select/select.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "washer_parser.h"

#ifdef USE_SAMSUNG_WASHER_TIME
#include "esphome/components/time/real_time_clock.h"
#endif

namespace esphome::samsung_washer {

// Home Assistant can write these entities. The washer UART is receive-only, so a write is rejected
// and the panel value is published again.
class PanelSelect : public select::Select {
 protected:
  void control(size_t index) override;
};

class PanelSwitch : public switch_::Switch {
 public:
  void set_panel(bool on);

 protected:
  void write_state(bool state) override;

  bool panel_{false};
  bool has_panel_{false};
};

class PanelNumber : public number::Number {
 protected:
  void control(float value) override;
};

class SamsungWasherRawCaptureSwitch final : public switch_::Switch, public Component {
 public:
  void setup() override;
  void dump_config() override;

 protected:
  void write_state(bool state) override;
};

class SamsungWasherComponent final : public PollingComponent, public uart::UARTDevice {
 public:
  SamsungWasherComponent() : PollingComponent(2000) {}

  void loop() override;
  void update() override;
  void dump_config() override;

  void set_secondary_uart(uart::UARTComponent *uart) { this->secondary_uart_ = uart; }
  void set_raw_capture_switch(switch_::Switch *sw) { this->raw_capture_switch_ = sw; }
#ifdef USE_SAMSUNG_WASHER_TIME
  void set_time(time::RealTimeClock *clock) { this->time_ = clock; }
#endif

  SUB_SELECT(cycle)
  SUB_SELECT(wash_temperature)
  SUB_SELECT(spin_speed)
  SUB_SELECT(rinse_cycles)

  SUB_TEXT_SENSOR(machine_state)
  SUB_TEXT_SENSOR(progress)

  SUB_BINARY_SENSOR(running)
  SUB_BINARY_SENSOR(power)
  SUB_BINARY_SENSOR(link_active)
  SUB_BINARY_SENSOR(iron_indicator)
  SUB_BINARY_SENSOR(lock_indicator)

  SUB_SENSOR(completion_time)
  SUB_SENSOR(estimated_finish)
  SUB_NUMBER(delay_start)

  void set_bubble_soak_switch(PanelSwitch *sw) { this->bubble_soak_switch_ = sw; }
  void set_pre_wash_switch(PanelSwitch *sw) { this->pre_wash_switch_ = sw; }
  void set_intensive_switch(PanelSwitch *sw) { this->intensive_switch_ = sw; }
  void set_extra_rinse_switch(PanelSwitch *sw) { this->extra_rinse_switch_ = sw; }

 protected:
  void reset_tracking_();
  void clear_operational_();
  void handle_primary_frame_(const std::vector<uint8_t> &p);
  void publish_estimated_finish_(bool running, int minutes);
  void flush_raw_capture_(const char *label, std::vector<uint8_t> &buf, uint32_t last_byte_ms, bool force);

  uart::UARTComponent *secondary_uart_{nullptr};
  switch_::Switch *raw_capture_switch_{nullptr};
  PanelSwitch *bubble_soak_switch_{nullptr};
  PanelSwitch *pre_wash_switch_{nullptr};
  PanelSwitch *intensive_switch_{nullptr};
  PanelSwitch *extra_rinse_switch_{nullptr};
#ifdef USE_SAMSUNG_WASHER_TIME
  time::RealTimeClock *time_{nullptr};
#endif

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
  uint32_t primary_raw_last_ms_{0};
  uint32_t secondary_raw_last_ms_{0};
};

}  // namespace esphome::samsung_washer
