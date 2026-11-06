#include "samsung_washer.h"

#include <cinttypes>
#include <cmath>
#include <cstdio>
#include <cstring>

#include "esphome/core/controller_registry.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

namespace esphome::samsung_washer {

static const char *const TAG = "samsung_washer";
static const char *const RAW_TAG = "washer.raw";

static const char *const MACHINE_ACTIVE = "Active";
static const char *const MACHINE_PAUSED = "Paused";
static const char *const MACHINE_IDLE = "Idle";
static const char *const PROGRESS_FINISHED = "Finished";
static const char *const PROGRESS_IDLE = "Idle";
static const char *const PROGRESS_DELAY = "Delayed start";

void PanelSelect::control(size_t index) {
  // The panel is the only writer. Publishing the current option puts the Home Assistant control back.
  if (this->has_state())
    this->publish_state(this->active_index().value_or(index));
}

void PanelSwitch::set_panel(bool on) {
  this->panel_ = on;
  this->has_panel_ = true;
  this->publish_state(on);
}

void PanelSwitch::write_state(bool state) {
  if (!this->has_panel_)
    return;
  // publish_state deduplicates, so bounce through the opposite value when a write has to be undone.
  if (state != this->panel_)
    this->publish_state(!this->panel_);
  this->publish_state(this->panel_);
}

void PanelNumber::control(float value) {
  if (this->has_state())
    this->publish_state(this->state);
  else
    (void) value;
}

static void publish_text(text_sensor::TextSensor *sensor, const char *value) {
  if (sensor == nullptr || value == nullptr)
    return;
  // publish_state notifies even when the text is unchanged, including through a filter.
  if (sensor->has_state() && sensor->get_raw_state() == value)
    return;
  sensor->publish_state(value);
}

static void invalidate_text(text_sensor::TextSensor *sensor) {
  if (sensor == nullptr || !sensor->has_state())
    return;
  sensor->set_has_state(false);
#if defined(USE_CONTROLLER_REGISTRY) && defined(USE_TEXT_SENSOR)
  ControllerRegistry::notify_text_sensor_update(sensor);
#endif
}

static void publish_sensor(sensor::Sensor *sensor, float value) {
  if (sensor == nullptr || (sensor->has_state() && sensor->state == value))
    return;
  sensor->publish_state(value);
}

static void invalidate_sensor(sensor::Sensor *sensor) {
  if (sensor == nullptr || !sensor->has_state())
    return;
  sensor->state = NAN;
  sensor->set_has_state(false);
#if defined(USE_CONTROLLER_REGISTRY) && defined(USE_SENSOR)
  ControllerRegistry::notify_sensor_update(sensor);
#endif
}

static void publish_number(number::Number *entity, float value) {
  if (entity == nullptr || (entity->has_state() && entity->state == value))
    return;
  entity->publish_state(value);
}

static void invalidate_number(number::Number *entity) {
  if (entity == nullptr || !entity->has_state())
    return;
  entity->state = NAN;
  entity->set_has_state(false);
#if defined(USE_CONTROLLER_REGISTRY) && defined(USE_NUMBER)
  ControllerRegistry::notify_number_update(entity);
#endif
}

static void publish_select(select::Select *entity, const char *value) {
  if (entity == nullptr || value == nullptr)
    return;
  if (entity->has_state() && entity->current_option() == value)
    return;
  entity->publish_state(value);
}

static void publish_switch(PanelSwitch *entity, bool on) {
  if (entity != nullptr)
    entity->set_panel(on);
}

static void publish_binary(binary_sensor::BinarySensor *entity, bool on) {
  if (entity != nullptr)
    entity->publish_state(on);
}

static void invalidate_binary(binary_sensor::BinarySensor *entity) {
  if (entity != nullptr && entity->has_state())
    entity->invalidate_state();
}

// English labels from LocalThings' translation catalog. Course hex codes differ by model, so the
// panel legend is published instead of a code. "Colors" is LocalThings' name for the COLOURS key.
static const char *cycle_label(const uint8_t *p) {
  if (p[20] == 0) {
    switch (p[18]) {
      case 0x20: return "Eco 40-60";
      case 0x08: return "Cotton";
      case 0x10: return "Synthetics";
      case 0x04: return "Hygiene Steam";
      case 0x02: return "Rinse+Spin";
      case 0x01: return "Drain/Spin";
      case 0x40: return "Drum Clean";
      default: return nullptr;
    }
  }
  if (p[18] == 0) {
    switch (p[20]) {
      case 0x40: return "Bedding";
      case 0x01: return "Colors";
      case 0x02: return "Intense Cold";
      case 0x04: return "Wool";
      case 0x10: return "Delicates";
      case 0x08: return "Mixed Load";
      case 0x20: return "15' Quick Wash";
      default: return nullptr;
    }
  }
  return nullptr;
}

// LocalThings stores some of these as translation keys (none, rinse_hold, no_spin) and shows the
// words below. Numeric options are the device values, without units.
static const char *temperature_label(uint8_t value) {
  switch (value) {
    case 0: return "None";
    case 0x10: return "20";
    case 0x08: return "30";
    case 0x04: return "40";
    case 0x02: return "60";
    case 0x01: return "90";
    case 0x06: return "40 and 60 indicators";
    default: return "Unknown";
  }
}

static const char *spin_label(uint8_t value) {
  switch (value) {
    case 0: return "Rinse hold";
    case 0x10: return "No spin";
    case 0x08: return "400";
    case 0x04: return "800";
    case 0x02: return "1200";
    case 0x01: return "1400";
    default: return "Unknown";
  }
}

static const char *rinse_label(uint8_t value) {
  switch (value) {
    case 0: return "0";
    case 0x10: return "1";
    case 0x08: return "2";
    case 0x04: return "3";
    case 0x02: return "4";
    case 0x01: return "5";
    default: return "Unknown";
  }
}

static const char *machine_state_label(const char *status) {
  if (std::strcmp(status, "Running") == 0)
    return MACHINE_ACTIVE;
  if (std::strcmp(status, "Paused") == 0)
    return MACHINE_PAUSED;
  if (std::strcmp(status, "Completed") == 0)
    return MACHINE_IDLE;
  return nullptr;
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
  if (!force && buf.size() < 32 && (millis() - last_byte_ms) < 50)
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
      if (this->primary_raw_buf_.size() >= 32) {
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
        if (this->secondary_raw_buf_.size() >= 32) {
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

void SamsungWasherComponent::clear_operational_() {
  invalidate_text(this->machine_state_text_sensor_);
  invalidate_text(this->progress_text_sensor_);
  invalidate_binary(this->running_binary_sensor_);
  invalidate_sensor(this->completion_time_sensor_);
  invalidate_sensor(this->estimated_finish_sensor_);
  invalidate_number(this->delay_start_number_);
}

void SamsungWasherComponent::reset_tracking_() {
  this->blank_pending_ = false;
  this->panel_on_known_ = false;
  this->cycle_tracker_.reset();
  invalidate_binary(this->power_binary_sensor_);
  this->clear_operational_();
}

void SamsungWasherComponent::publish_estimated_finish_(bool running, int minutes) {
#ifdef USE_SAMSUNG_WASHER_TIME
  if (this->estimated_finish_sensor_ == nullptr)
    return;
  if (!running || minutes <= 0 || this->time_ == nullptr) {
    invalidate_sensor(this->estimated_finish_sensor_);
    return;
  }
  auto now = this->time_->now();
  if (!now.is_valid()) {
    invalidate_sensor(this->estimated_finish_sensor_);
    return;
  }
  // Same whole-minute finish instant LocalThings publishes while a cycle is active.
  const auto finish = static_cast<float>(now.timestamp - now.second + static_cast<time_t>(minutes) * 60);
  publish_sensor(this->estimated_finish_sensor_, finish);
#else
  (void) running;
  (void) minutes;
#endif
}

void SamsungWasherComponent::update() {
  const bool online = this->has_primary_frame_ && (millis() - this->last_primary_ms_) < 5000;
  if (this->link_active_binary_sensor_ != nullptr)
    this->link_active_binary_sensor_->publish_state(online);
  if (!online) {
    this->has_primary_frame_ = false;
    this->reset_tracking_();
  }
}

void SamsungWasherComponent::handle_primary_frame_(const std::vector<uint8_t> &p) {
  const uint32_t now = millis();
  // A >5s gap can fall between polling ticks; do not inherit the old cycle.
  if (this->has_primary_frame_ && now - this->last_primary_ms_ >= 5000)
    this->reset_tracking_();
  this->last_primary_ms_ = now;
  this->has_primary_frame_ = true;
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
  if (this->panel_on_known_)
    publish_binary(this->power_binary_sensor_, this->panel_on_state_);

  const int minutes = washer_time_minutes(p[4], p[6], p[8]);
  const int delay_hours = washer_delay_hours(p[4], p[6], p[8]);
  const char *status = this->panel_on_known_ ? this->cycle_tracker_.status(this->panel_on_state_) : "Unknown";
  const bool running = std::strcmp(status, "Running") == 0;

  if (const char *machine = machine_state_label(status))
    publish_text(this->machine_state_text_sensor_, machine);
  else
    invalidate_text(this->machine_state_text_sensor_);

  if (std::strcmp(status, "Unknown") == 0)
    invalidate_binary(this->running_binary_sensor_);
  else
    publish_binary(this->running_binary_sensor_, running);

  const char *progress = nullptr;
  if (std::strcmp(status, "Completed") == 0)
    progress = PROGRESS_FINISHED;
  else if (std::strcmp(status, "Off") == 0)
    progress = PROGRESS_IDLE;
  else if (delay_hours >= 0)
    progress = PROGRESS_DELAY;
  if (progress != nullptr)
    publish_text(this->progress_text_sensor_, progress);
  else
    invalidate_text(this->progress_text_sensor_);

  // Program indicators blink in some states. Blank is not a new program.
  publish_select(this->cycle_select_, cycle_label(p.data()));
  publish_select(this->wash_temperature_select_, temperature_label(p[16]));
  publish_select(this->spin_speed_select_, spin_label(p[12]));
  publish_select(this->rinse_cycles_select_, rinse_label(p[14]));

  const uint8_t option = p[10] & 0x0c;
  publish_switch(this->pre_wash_switch_, option == 0x04);
  publish_switch(this->intensive_switch_, option == 0x08);
  publish_switch(this->extra_rinse_switch_, option == 0x0c);
  publish_switch(this->bubble_soak_switch_, p[10] & 0x02);
  publish_binary(this->iron_indicator_binary_sensor_, p[10] & 0x01);
  publish_binary(this->lock_indicator_binary_sensor_, p[5] & 0x01);

  if (delay_hours >= 0) {
    publish_sensor(this->completion_time_sensor_, delay_hours * 60);
    publish_number(this->delay_start_number_, delay_hours);
  } else if (minutes >= 0) {
    publish_sensor(this->completion_time_sensor_, minutes);
    publish_number(this->delay_start_number_, 0);
  } else {
    invalidate_sensor(this->completion_time_sensor_);
    invalidate_number(this->delay_start_number_);
  }
  // LocalThings only reports a finish instant while the cycle is running, and not for a zero countdown.
  this->publish_estimated_finish_(running, minutes);
}

void SamsungWasherComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Samsung Washer Monitor:");
  LOG_SELECT("  ", "Cycle", this->cycle_select_);
  LOG_SELECT("  ", "Wash Temperature", this->wash_temperature_select_);
  LOG_SELECT("  ", "Spin Speed", this->spin_speed_select_);
  LOG_SELECT("  ", "Rinse Cycles", this->rinse_cycles_select_);
  LOG_TEXT_SENSOR("  ", "Machine State", this->machine_state_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Progress", this->progress_text_sensor_);
  LOG_BINARY_SENSOR("  ", "Running", this->running_binary_sensor_);
  LOG_SENSOR("  ", "Completion Time", this->completion_time_sensor_);
  LOG_NUMBER("  ", "Delay Start", this->delay_start_number_);
  LOG_SENSOR("  ", "Estimated Finish", this->estimated_finish_sensor_);
  LOG_SWITCH("  ", "Bubble Soak", this->bubble_soak_switch_);
  LOG_SWITCH("  ", "Pre-wash", this->pre_wash_switch_);
  LOG_SWITCH("  ", "Intensive", this->intensive_switch_);
  LOG_SWITCH("  ", "Extra Rinse", this->extra_rinse_switch_);
  LOG_BINARY_SENSOR("  ", "Easy Iron Indicator", this->iron_indicator_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Power", this->power_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "UART Link Active", this->link_active_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Door Lock Indicator", this->lock_indicator_binary_sensor_);
  LOG_SWITCH("  ", "UART Raw Capture", this->raw_capture_switch_);
}

}  // namespace esphome::samsung_washer
