#include "samsung_washer.h"

#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"

#include <cinttypes>

namespace esphome::samsung_washer {

static const char *const TAG = "samsung_washer";
static const char *const RAW_TAG = "samsung_washer.raw";

#ifdef USE_SWITCH
void RawCaptureSwitch::setup() {
  const optional<bool> initial = this->get_initial_state_with_restore_mode();
  if (initial.has_value())
    this->control(initial.value());
}

void RawCaptureSwitch::write_state(bool state) {
  this->publish_state(state);
  if (this->parent_ != nullptr)
    this->parent_->set_raw_capture(this->state);
}
#endif

void SamsungWasher::RawChunk::push(uint8_t byte, bool enabled) {
  if (!enabled) {
    this->clear();
    return;
  }
  const uint32_t now = millis();
  if (this->size > 0 && now - this->last_ms >= GAP_MS)
    this->flush_();
  this->data[this->size++] = byte;
  this->last_ms = now;
  if (this->size == BYTES)
    this->flush_();
}

void SamsungWasher::RawChunk::flush_if_stale(uint32_t now, bool enabled) {
  if (!enabled) {
    this->clear();
    return;
  }
  if (this->size > 0 && now - this->last_ms >= GAP_MS)
    this->flush_();
}

void SamsungWasher::RawChunk::clear() { this->size = 0; }

void SamsungWasher::RawChunk::flush_() {
  if (this->size == 0)
    return;
  char hex[format_hex_pretty_size(BYTES)];
  format_hex_pretty_to(hex, this->data, this->size, ':');
  ESP_LOGI(RAW_TAG, "%s %" PRIu32 " %s", this->label, millis(), hex);
  this->size = 0;
}

void SamsungWasher::set_raw_capture(bool enabled) {
  this->raw_capture_ = enabled;
  if (!enabled) {
    this->pin1_chunk_.clear();
    this->pin2_chunk_.clear();
  }
}

void SamsungWasher::setup() {
#ifdef USE_TEXT_SENSOR
  this->publish_text_(this->cycle_status_text_sensor_, "Unknown");
#endif
#ifdef USE_BINARY_SENSOR
  this->publish_binary_(this->uart_link_binary_sensor_, false);
#endif
}

void SamsungWasher::loop() {
  uint8_t byte;
  while (this->available() > 0) {
    if (!this->read_byte(&byte))
      break;
    this->pin1_chunk_.push(byte, this->raw_capture_);
    this->parser_.feed(byte, [this](const std::vector<uint8_t> &frame) { this->handle_frame_(frame); });
  }
  if (this->raw_uart_ != nullptr) {
    while (this->raw_uart_->available() > 0) {
      if (!this->raw_uart_->read_byte(&byte))
        break;
      this->pin2_chunk_.push(byte, this->raw_capture_);
    }
  }
  const uint32_t now = millis();
  this->pin1_chunk_.flush_if_stale(now, this->raw_capture_);
  this->pin2_chunk_.flush_if_stale(now, this->raw_capture_);
  this->update_link_(now);
}

void SamsungWasher::dump_config() {
  ESP_LOGCONFIG(TAG, "Samsung washer:");
  ESP_LOGCONFIG(TAG, "  Raw UART: %s", this->raw_uart_ != nullptr ? "configured" : "none");
#ifdef USE_TEXT_SENSOR
  LOG_TEXT_SENSOR("  ", "Cycle status", this->cycle_status_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Program", this->program_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Temperature", this->temperature_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Spin", this->spin_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Rinse", this->rinse_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Displayed time", this->displayed_time_text_sensor_);
  LOG_TEXT_SENSOR("  ", "Option", this->option_text_sensor_);
#endif
#ifdef USE_BINARY_SENSOR
  LOG_BINARY_SENSOR("  ", "Panel on", this->panel_on_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "UART link", this->uart_link_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Bubble Soak", this->bubble_soak_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Easy Iron", this->easy_iron_binary_sensor_);
  LOG_BINARY_SENSOR("  ", "Door lock", this->door_lock_binary_sensor_);
#endif
#ifdef USE_SWITCH
  LOG_SWITCH("  ", "UART raw capture", this->raw_capture_switch_);
#endif
}

void SamsungWasher::handle_frame_(const std::vector<uint8_t> &frame) {
  if (frame.size() != PanelFrame::LENGTH)
    return;
  const uint8_t *data = frame.data();
  const uint32_t now = millis();
  this->last_frame_ms_ = now;
  this->cycle_.feed(data[PanelFrame::EVENT],
                    data[PanelFrame::PROGRAM_A] != 0 || data[PanelFrame::PROGRAM_B] != 0);

  const bool lit = panel_lit(data, frame.size());
  if (lit) {
    this->blank_since_ms_ = 0;
    if (this->panel_ != PanelState::ON) {
      this->panel_ = PanelState::ON;
#ifdef USE_BINARY_SENSOR
      this->publish_binary_(this->panel_on_binary_sensor_, true);
#endif
    }
  } else if (this->blank_since_ms_ == 0) {
    this->blank_since_ms_ = now;
  } else if (now - this->blank_since_ms_ >= PANEL_BLANK_MS && this->panel_ != PanelState::OFF) {
    this->panel_ = PanelState::OFF;
    this->cycle_.reset();
#ifdef USE_BINARY_SENSOR
    this->publish_binary_(this->panel_on_binary_sensor_, false);
#endif
  }

  const char *status = "Unknown";
  if (this->panel_ != PanelState::UNKNOWN)
    status = this->cycle_.status(this->panel_ == PanelState::ON);

#ifdef USE_TEXT_SENSOR
  this->publish_text_(this->cycle_status_text_sensor_, status);
  if (const char *program = program_name(data[PanelFrame::PROGRAM_A], data[PanelFrame::PROGRAM_B]))
    this->publish_text_(this->program_text_sensor_, program);
  this->publish_text_(this->temperature_text_sensor_, temperature_selection(data[PanelFrame::TEMPERATURE]));
  this->publish_text_(this->spin_text_sensor_, spin_selection(data[PanelFrame::SPIN]));
  this->publish_text_(this->rinse_text_sensor_, rinse_selection(data[PanelFrame::RINSE]));
  this->publish_text_(this->option_text_sensor_, washer_option(data[PanelFrame::OPTIONS]));
  char displayed[32];
  format_displayed_time(data[PanelFrame::HOUR], data[PanelFrame::MINUTE_TENS], data[PanelFrame::MINUTE_ONES], displayed,
                        sizeof(displayed));
  this->publish_text_(this->displayed_time_text_sensor_, displayed);
#else
  (void) status;
#endif
#ifdef USE_BINARY_SENSOR
  this->publish_binary_(this->bubble_soak_binary_sensor_, bubble_soak(data[PanelFrame::OPTIONS]));
  this->publish_binary_(this->easy_iron_binary_sensor_, easy_iron(data[PanelFrame::OPTIONS]));
  this->publish_binary_(this->door_lock_binary_sensor_, door_locked(data[PanelFrame::LOCK]));
#endif
}

void SamsungWasher::update_link_(uint32_t now) {
  const bool online = this->last_frame_ms_ != 0 && now - this->last_frame_ms_ < LINK_TIMEOUT_MS;
  if (online == this->link_online_)
    return;
  this->link_online_ = online;
#ifdef USE_BINARY_SENSOR
  this->publish_binary_(this->uart_link_binary_sensor_, online);
#endif
  if (online) {
    ESP_LOGD(TAG, "UART link active");
    return;
  }
  ESP_LOGD(TAG, "UART link lost");
  this->blank_since_ms_ = 0;
  this->cycle_.reset();
  this->panel_ = PanelState::UNKNOWN;
#ifdef USE_BINARY_SENSOR
  if (this->panel_on_binary_sensor_ != nullptr && this->panel_on_binary_sensor_->has_state())
    this->panel_on_binary_sensor_->invalidate_state();
#endif
#ifdef USE_TEXT_SENSOR
  this->publish_text_(this->cycle_status_text_sensor_, "Unknown");
#endif
}

#ifdef USE_TEXT_SENSOR
void SamsungWasher::publish_text_(text_sensor::TextSensor *sensor, const char *value) {
  if (sensor == nullptr || value == nullptr)
    return;
  if (!sensor->has_state() || sensor->state != value)
    sensor->publish_state(value);
}
#endif

#ifdef USE_BINARY_SENSOR
void SamsungWasher::publish_binary_(binary_sensor::BinarySensor *sensor, bool value) {
  if (sensor == nullptr)
    return;
  if (!sensor->has_state() || sensor->state != value)
    sensor->publish_state(value);
}
#endif

}  // namespace esphome::samsung_washer
