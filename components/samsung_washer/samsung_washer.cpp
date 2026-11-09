#include "samsung_washer.h"

#include <cinttypes>
#include <cstdio>

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::samsung_washer {

static const char *const TAG = "samsung_washer";
static const char *const RAW_TAG = "washer.raw";

static void publish_text(text_sensor::TextSensor *sensor, const char *value) {
  if (sensor != nullptr && (!sensor->has_state() || sensor->get_raw_state() != value))
    sensor->publish_state(value);
}

void SamsungWasherRawCaptureSwitch::setup() {
  optional<bool> initial_state = this->get_initial_state_with_restore_mode();
  if (initial_state.has_value()) {
    this->control(initial_state.value());
  }
}

void SamsungWasherRawCaptureSwitch::dump_config() {
  LOG_SWITCH("", "UART Raw Capture Switch", this);
}

void SamsungWasherRawCaptureSwitch::write_state(bool state) {
  this->publish_state(state);
}

void SamsungWasherComponent::flush_raw_capture_(const char *label, std::vector<uint8_t> &buf, uint32_t last_byte_ms,
                                                bool force) {
  if (buf.empty())
    return;
  if (!force && buf.size() < 64 && (millis() - last_byte_ms) < 50)
    return;
  ESP_LOGI(RAW_TAG, "%s %" PRIu32 " %s", label, millis(), format_hex_pretty(buf.data(), buf.size()).c_str());
  buf.clear();
}

void SamsungWasherComponent::loop() {
  const bool raw_capture = this->raw_capture_switch_ != nullptr && this->raw_capture_switch_->state;
  if (!raw_capture) {
    this->primary_raw_buf_.clear();
    this->secondary_raw_buf_.clear();
  }

  uint8_t byte = 0;
  int budget = 64;
  while (budget-- > 0 && this->available() && this->read_byte(&byte)) {
    if (raw_capture) {
      this->primary_raw_buf_.push_back(byte);
      this->primary_raw_last_ms_ = millis();
      if (this->primary_raw_buf_.size() >= 64) {
        this->flush_raw_capture_("CN803 pin 1", this->primary_raw_buf_, this->primary_raw_last_ms_, true);
      }
    }
    this->primary_parser_.feed(byte, [this](const std::vector<uint8_t> &p) { this->handle_primary_frame_(p); });
  }

  if (this->secondary_uart_ != nullptr) {
    int sec_budget = 64;
    while (sec_budget-- > 0 && this->secondary_uart_->available() && this->secondary_uart_->read_byte(&byte)) {
      if (raw_capture) {
        this->secondary_raw_buf_.push_back(byte);
        this->secondary_raw_last_ms_ = millis();
        if (this->secondary_raw_buf_.size() >= 64) {
          this->flush_raw_capture_("CN803 pin 2", this->secondary_raw_buf_, this->secondary_raw_last_ms_, true);
        }
      }
      this->secondary_parser_.feed(byte, [](const std::vector<uint8_t> &) {});
    }
  }

  if (raw_capture) {
    this->flush_raw_capture_("CN803 pin 1", this->primary_raw_buf_, this->primary_raw_last_ms_, false);
    this->flush_raw_capture_("CN803 pin 2", this->secondary_raw_buf_, this->secondary_raw_last_ms_, false);
  }
}

void SamsungWasherComponent::reset_tracking_() {
  this->blank_pending_ = false;
  this->panel_on_known_ = false;
  this->cycle_tracker_.reset();
  this->dc92_02395a_cycle_tracker_.reset();
  if (this->panel_on_binary_sensor_ != nullptr && this->panel_on_binary_sensor_->has_state())
    this->panel_on_binary_sensor_->invalidate_state();
}

void SamsungWasherComponent::update() {
  const bool online = this->has_primary_frame_ && (millis() - this->last_primary_ms_) < 5000;
  if (this->link_active_binary_sensor_ != nullptr)
    this->link_active_binary_sensor_->publish_state(online);
  if (!online) {
    this->has_primary_frame_ = false;
    this->reset_tracking_();
    publish_text(this->cycle_status_text_sensor_, "Unknown");
  }
}

void SamsungWasherComponent::handle_primary_frame_(const std::vector<uint8_t> &p) {
  const uint32_t now = millis();
  // A >5s gap can fall between polling ticks; do not inherit the old cycle.
  if (this->has_primary_frame_ && now - this->last_primary_ms_ >= 5000)
    this->reset_tracking_();
  this->last_primary_ms_ = now;
  this->has_primary_frame_ = true;
  if (this->model_ == WasherModel::DC92_02395A) {
    this->handle_dc92_02395a_frame_(p);
    return;
  }
  this->cycle_tracker_.feed(p[23], p[18] != 0 || p[20] != 0);

  bool panel_lit = false;
  for (size_t i = 2; i < 22; i++) {
    panel_lit |= p[i] != 0;
  }

  if (panel_lit) {
    this->blank_pending_ = false;
    this->panel_on_known_ = true;
    this->panel_on_state_ = true;
  } else {
    if (!this->blank_pending_) {
      this->blank_since_ms_ = millis();
      this->blank_pending_ = true;
    }
    if (millis() - this->blank_since_ms_ >= 2000) {
      if (!this->panel_on_known_ || this->panel_on_state_)
        this->cycle_tracker_.reset();
      this->panel_on_known_ = true;
      this->panel_on_state_ = false;
    }
  }
  if (this->panel_on_known_ && this->panel_on_binary_sensor_ != nullptr)
    this->panel_on_binary_sensor_->publish_state(this->panel_on_state_);

  const char *status = this->panel_on_known_ ? this->cycle_tracker_.status(this->panel_on_state_) : "Unknown";
  publish_text(this->cycle_status_text_sensor_, status);

  // Program indicators blink in some states. Blank is not a new program.
  const char *program = nullptr;
  if (p[20] == 0) {
    switch (p[18]) {
      case 0x20: program = "Eco 40-60"; break;
      case 0x08: program = "Cotton"; break;
      case 0x10: program = "Synthetics"; break;
      case 0x04: program = "Hygiene Steam"; break;
      case 0x02: program = "Rinse+Spin"; break;
      case 0x01: program = "Drain/Spin"; break;
      case 0x40: program = "Drum Clean"; break;
    }
  } else if (p[18] == 0) {
    switch (p[20]) {
      case 0x40: program = "Bedding"; break;
      case 0x01: program = "Colours"; break;
      case 0x02: program = "Intense Cold"; break;
      case 0x04: program = "Wool"; break;
      case 0x10: program = "Delicates"; break;
      case 0x08: program = "Mixed Load"; break;
      case 0x20: program = "15' Quick Wash"; break;
    }
  }
  if (program != nullptr)
    publish_text(this->program_indicator_text_sensor_, program);

  const char *temp = nullptr;
  switch (p[16]) {
    case 0: temp = "No indicator"; break;
    case 0x10: temp = "20 °C"; break;
    case 0x08: temp = "30 °C"; break;
    case 0x04: temp = "40 °C"; break;
    case 0x02: temp = "60 °C"; break;
    case 0x01: temp = "90 °C"; break;
    case 0x06: temp = "40 and 60 indicators"; break;
    default: temp = "Unknown"; break;
  }
  publish_text(this->temperature_selection_text_sensor_, temp);

  const char *spin = nullptr;
  switch (p[12]) {
    case 0: spin = "No indicator (Rinse Hold)"; break;
    case 0x10: spin = "No Spin"; break;
    case 0x08: spin = "400 rpm"; break;
    case 0x04: spin = "800 rpm"; break;
    case 0x02: spin = "1200 rpm"; break;
    case 0x01: spin = "1400 rpm"; break;
    default: spin = "Unknown"; break;
  }
  publish_text(this->spin_selection_text_sensor_, spin);

  const char *rinse = nullptr;
  switch (p[14]) {
    case 0: rinse = "No indicator"; break;
    case 0x10: rinse = "1"; break;
    case 0x08: rinse = "2"; break;
    case 0x04: rinse = "3"; break;
    case 0x02: rinse = "4"; break;
    case 0x01: rinse = "5"; break;
    default: rinse = "Unknown"; break;
  }
  publish_text(this->rinse_selection_text_sensor_, rinse);

  auto update_binary = [](binary_sensor::BinarySensor *s, bool v) {
    if (s != nullptr)
      s->publish_state(v);
  };
  update_binary(this->bubble_indicator_binary_sensor_, p[10] & 0x02);
  update_binary(this->iron_indicator_binary_sensor_, p[10] & 0x01);

  const char *option = washer_option(p[10]);
  publish_text(this->option_selection_text_sensor_, option);

  update_binary(this->lock_indicator_binary_sensor_, p[5] & 0x01);

  const int minutes = washer_time_minutes(p[4], p[6], p[8]);
  const int delay_hours = washer_delay_hours(p[4], p[6], p[8]);
  char display[32];
  if (delay_hours >= 0) {
    snprintf(display, sizeof(display), "%dh", delay_hours);
  } else if (minutes >= 0) {
    snprintf(display, sizeof(display), "%d:%02d", minutes / 60, minutes % 60);
  } else {
    snprintf(display, sizeof(display), "Unknown / non-time display");
  }
  publish_text(this->displayed_time_text_sensor_, display);
}

void SamsungWasherComponent::handle_dc92_02395a_frame_(const std::vector<uint8_t> &p) {
  // Bit 6 of byte 7 is the status light. Blank frames with byte 29 of 0x03 or
  // 0x23 are the captured Off and Completed messages.
  this->dc92_02395a_cycle_tracker_.feed(p[7] & 0x40, washer_dc92_02395a_panel_blank(p.data()), p[29], millis());
  publish_text(this->cycle_status_text_sensor_, this->dc92_02395a_cycle_tracker_.status());
  // A dark program light is the blink while running, so it leaves the current program in place.
  const char *program = washer_dc92_02395a_program(p[6], p[8], p[10], p[12], p[14]);
  if (program != nullptr)
    publish_text(this->program_indicator_text_sensor_, program);
  const int temperature = washer_dc92_02395a_temperature(p[4], p[6], p[8], p[10], p[12]);
  publish_text(this->temperature_selection_text_sensor_, washer_dc92_02395a_temperature_text(temperature));
  publish_text(this->rinse_selection_text_sensor_,
               washer_dc92_02395a_bar_text(washer_dc92_02395a_bar_level(p[3])));
  publish_text(this->spin_selection_text_sensor_,
               washer_dc92_02395a_spin_text(washer_dc92_02395a_bar_level(p[5])));
  publish_text(this->soil_selection_text_sensor_, washer_dc92_02395a_soil_text(washer_dc92_02395a_bar_level(p[7])));

  const int delay_hours = washer_dc92_02395a_delay_hours(p[11], p[13], p[15]);
  const int minutes = washer_dc92_02395a_time_minutes(p[11], p[13], p[15]);
  char display[32];
  if (delay_hours >= 0)
    snprintf(display, sizeof(display), "%dh", delay_hours);
  else if (minutes >= 0)
    snprintf(display, sizeof(display), "%d:%02d", minutes / 60, minutes % 60);
  else
    snprintf(display, sizeof(display), "Unknown / non-time display");
  publish_text(this->displayed_time_text_sensor_, display);

  // Bit 1 of byte 17 is the door lock. Bit 3 is the child lock. Bit 7 flips between frame copies.
  if (this->lock_indicator_binary_sensor_ != nullptr)
    this->lock_indicator_binary_sensor_->publish_state(p[17] & 0x02);
  if (this->child_lock_indicator_binary_sensor_ != nullptr)
    this->child_lock_indicator_binary_sensor_->publish_state(p[17] & 0x08);
  // Bit 6 of byte 5 is the steam light. The low 5 bits of that byte remain the spin bar.
  if (this->steam_indicator_binary_sensor_ != nullptr)
    this->steam_indicator_binary_sensor_->publish_state(p[5] & 0x40);
  // Bit 5 of byte 3 is the Pre Soak light. The low 5 bits of that byte remain the rinse bar.
  if (this->pre_soak_indicator_binary_sensor_ != nullptr)
    this->pre_soak_indicator_binary_sensor_->publish_state(p[3] & 0x20);
  // Bit 6 of byte 3 is the delay light.
  if (this->delay_indicator_binary_sensor_ != nullptr)
    this->delay_indicator_binary_sensor_->publish_state(p[3] & 0x40);
  // Bit 5 of byte 7 is the sound-off LED. The alarm sounds when that LED is dark.
  if (this->alarm_binary_sensor_ != nullptr)
    this->alarm_binary_sensor_->publish_state((p[7] & 0x20) == 0);
}

void SamsungWasherComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Samsung Washer Monitor:");
  ESP_LOGCONFIG(TAG, "  Model: %s", this->model_ == WasherModel::DC92_02395A ? "DC92-02395A" : "DC92-01950B");
  LOG_TEXT_SENSOR("  ", "Cycle Status", this->cycle_status_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Program Indicator", this->program_indicator_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Temperature Selection", this->temperature_selection_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Spin Selection", this->spin_selection_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Rinse Selection", this->rinse_selection_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Soil Selection", this->soil_selection_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Displayed Time", this->displayed_time_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Option", this->option_selection_text_sensor_);
  LOG_BINARY_SENSOR("  ", "Panel On", this->panel_on_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "UART Link Active", this->link_active_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Bubble Soak Indicator", this->bubble_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Easy Iron Indicator", this->iron_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Door Lock Indicator", this->lock_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Child Lock Indicator", this->child_lock_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Steam Indicator", this->steam_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Pre Soak Indicator", this->pre_soak_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Delay Indicator", this->delay_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Alarm", this->alarm_binary_sensor_);
  LOG_SWITCH("  ", "UART Raw Capture", this->raw_capture_switch_);
}

}  // namespace esphome::samsung_washer
