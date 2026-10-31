#pragma once
#include <cstdint>
#include <vector>
#include <functional>

// Input-only framing. The callback is invoked only for complete CRC-valid frames.
class WasherParser {
 public:
  explicit WasherParser(size_t length) : length_(length) {}
  void feed(uint8_t b, const std::function<void(const std::vector<uint8_t> &)> &callback) {
    if (data_.empty()) {
      if (b == 0x5a) data_.push_back(b);
      return;
    }
    if (data_.size() == 1 && b != 0xa5) {
      data_.clear();
      if (b == 0x5a) data_.push_back(b);
      return;
    }
    data_.push_back(b);
    if (data_.size() != length_) return;
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length_ - 2; ++i) {
      crc ^= uint16_t(data_[i]) << 8;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    const bool valid = data_[length_ - 2] == (crc >> 8) && data_[length_ - 1] == (crc & 0xff);
    if (valid) callback(data_);
    // Search within a rejected frame: a dropped byte must not prevent resync.
    auto old = data_;
    data_.clear();
    if (valid) return;
    for (size_t i = 1; i < old.size(); ++i) {
      if (old[i] == 0x5a && (i + 1 == old.size() || old[i + 1] == 0xa5)) {
        data_.assign(old.begin() + i, old.end());
        break;
      }
    }
  }
 private:
  size_t length_;
  std::vector<uint8_t> data_;
};

inline int washer_digit(uint8_t value) {
  switch (value & 0x7f) {
    case 0x77: return 0; case 0x30: return 1; case 0x6b: return 2;
    case 0x79: return 3; case 0x3c: return 4; case 0x5d: return 5;
    case 0x5f: return 6; case 0x74: return 7; case 0x7f: return 8;
    case 0x7d: return 9; default: return -1;
  }
}

inline int washer_time_minutes(uint8_t hour_glyph, uint8_t tens_glyph, uint8_t ones_glyph) {
  int hour = (hour_glyph & 0x7f) == 0 ? 0 : washer_digit(hour_glyph);
  // A single-digit countdown has blank hour and minute-tens positions.
  int tens = hour == 0 && (tens_glyph & 0x7f) == 0 ? 0 : washer_digit(tens_glyph);
  int ones = washer_digit(ones_glyph);
  if (hour < 0 || tens < 0 || tens > 5 || ones < 0) return -1;
  return hour * 60 + tens * 10 + ones;
}

class WasherCycleStatus {
 public:
  void reset() { *this = WasherCycleStatus(); }
  const char *status(bool panel_on) const { return panel_on ? state_ : "Off"; }
  // Start needs three indicator blinks; Pause and completion are distinct events.
  void feed(uint8_t event, bool program_lit) {
    if (event != previous_event_) {
      if (event == 0x04) { pending_ = true; blinks_ = 0; state_ = "Unknown"; }
      if (event == 0x05) { pending_ = false; state_ = "Paused"; }
      if (event == 0x0e) { pending_ = false; state_ = "Completed"; }
    }
    previous_event_ = event;
    if (pending_ && !previous_lit_ && program_lit && ++blinks_ >= 3) {
      pending_ = false;
      state_ = "Running";
    }
    previous_lit_ = program_lit;
  }
 private:
  uint8_t previous_event_ = 0;
  uint8_t blinks_ = 0;
  bool previous_lit_ = false;
  bool pending_ = false;
  const char *state_ = "Unknown";
};

inline WasherCycleStatus &washer_cycle_tracker() {
  static WasherCycleStatus tracker;
  return tracker;
}

inline const char *washer_option(uint8_t indicators) {
  switch (indicators & 0x0c) {
    case 0x04: return "Prewash";
    case 0x08: return "Intensive";
    case 0x0c: return "Rinse+";
    default: return "None";
  }
}

inline int washer_delay_hours(uint8_t tens_glyph, uint8_t ones_glyph, uint8_t suffix_glyph) {
  if ((suffix_glyph & 0x7f) != 0x1e) return -1;
  int tens = (tens_glyph & 0x7f) == 0 ? 0 : washer_digit(tens_glyph);
  int ones = washer_digit(ones_glyph);
  if (tens < 0 || ones < 0) return -1;
  int hours = tens * 10 + ones;
  return hours >= 1 && hours <= 24 ? hours : -1;
}
