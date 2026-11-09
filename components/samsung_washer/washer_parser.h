#pragma once
#include <cstddef>
#include <cstdint>
#include <vector>
#include <functional>

// Input-only framing. The callback is invoked only for complete CRC-valid frames.
class WasherParser {
 public:
  explicit WasherParser(size_t length) : length_(length) {}
  void set_length(size_t length) {
    length_ = length;
    data_.clear();
  }
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

// DC92-02395A has no cycle event byte. Bit 6 of byte 7 blinks while running and
// holds steady while the machine is on. Off and Completed are the captured blank
// frames: byte 29 is 0x03 for Off and 0x23 for Completed. A steady light is
// Unknown until a blink is seen, since a cycle that has not started looks the
// same as a pause. A blink that then holds still for two seconds is Paused.
class WasherDc92CycleStatus {
 public:
  static constexpr uint32_t kSteadyMs = 2000;
  void reset() { *this = WasherDc92CycleStatus(); }
  const char *status() const { return state_; }
  void feed(bool status_lit, bool panel_blank, uint8_t power, uint32_t now) {
    if (panel_blank && (power == 0x03 || power == 0x23)) {
      state_ = power == 0x03 ? "Off" : "Completed";
      seen_ = false;
      running_ = false;
      return;
    }
    if (!seen_) {
      seen_ = true;
      lit_ = status_lit;
      changed_ms_ = now;
      running_ = false;
      state_ = "Unknown";
      return;
    }
    if (status_lit != lit_) {
      lit_ = status_lit;
      changed_ms_ = now;
      running_ = true;
      state_ = "Running";
      return;
    }
    if (running_ && now - changed_ms_ >= kSteadyMs) {
      running_ = false;
      state_ = "Paused";
    }
  }
 private:
  bool seen_ = false;
  bool lit_ = false;
  bool running_ = false;
  uint32_t changed_ms_ = 0;
  const char *state_ = "Unknown";
};

// Bytes 3 through 22 hold the panel lights and digits. Both captured
// Off and Completed frames leave that range clear.
inline bool washer_dc92_02395a_panel_blank(const uint8_t *frame) {
  for (int i = 3; i <= 22; ++i)
    if (frame[i] != 0) return false;
  return true;
}

inline const char *washer_option(uint8_t indicators) {
  switch (indicators & 0x0c) {
    case 0x04: return "Prewash";
    case 0x08: return "Intensive";
    case 0x0c: return "Rinse+";
    default: return "None";
  }
}

// DC92-02395A rinse (byte 3), spin (byte 5) and soil (byte 7). The low 5 bits
// fill from bit 4 downward: 0x00, 0x10, 0x18, 0x1C, 0x1E, 0x1F are levels 0 to 5.
// The bar ignores the higher bits: Pre Soak and delay lights in byte 3, steam in
// byte 5, and the sound-off LED and status light in byte 7.
inline int washer_dc92_02395a_bar_level(uint8_t value) {
  switch (value & 0x1f) {
    case 0x00: return 0;
    case 0x10: return 1;
    case 0x18: return 2;
    case 0x1c: return 3;
    case 0x1e: return 4;
    case 0x1f: return 5;
    default: return -1;
  }
}

inline const char *washer_dc92_02395a_bar_text(int level) {
  switch (level) {
    case 0: return "0";
    case 1: return "1";
    case 2: return "2";
    case 3: return "3";
    case 4: return "4";
    case 5: return "5";
    default: return "Unknown";
  }
}

inline const char *washer_dc92_02395a_spin_text(int level) {
  switch (level) {
    case 1: return "No Spin";
    case 5: return "High";
    default: return washer_dc92_02395a_bar_text(level);
  }
}

inline const char *washer_dc92_02395a_soil_text(int level) {
  switch (level) {
    case 1: return "Light";
    case 5: return "Heavy";
    default: return washer_dc92_02395a_bar_text(level);
  }
}

// DC92-02395A temperature LEDs, hottest first: frame bytes 4, 6, 8, 10 and 12.
// A lit LED is the low bit, 0x01. The bar fills from byte 12 (cold) toward byte 4 (hot).
// Bits 6 and 7 of bytes 6, 8, 10 and 12 are program lights.
// All five off is no indicator, not the Cold setting.
inline int washer_dc92_02395a_temperature(uint8_t t5, uint8_t t4, uint8_t t3, uint8_t t2, uint8_t t1) {
  t4 &= 0x3f;
  t3 &= 0x3f;
  t2 &= 0x3f;
  t1 &= 0x3f;
  const uint8_t lights[] = {t1, t2, t3, t4, t5};
  int level = 0;
  bool ended = false;
  for (uint8_t light : lights) {
    if (light == 0x01) {
      if (ended) return -1;
      ++level;
    } else if (light == 0x00) {
      ended = true;
    } else {
      return -1;
    }
  }
  return level;
}

// Program lights around the dial. Bit 7 runs Normal, Heavy Duty, Whites, Bedding, Sanitize.
// Bit 6 runs Rinse & Spin, Perm Press, Delicates, Quick Wash, Self Clean.
// No lit program is the blink while running, so the caller keeps the previous program.
// Two lit programs is not a selection.
inline const char *washer_dc92_02395a_program(uint8_t b6, uint8_t b8, uint8_t b10, uint8_t b12, uint8_t b14) {
  const struct {
    uint8_t value;
    uint8_t mask;
    const char *name;
  } programs[] = {
      {b14, 0x80, "Normal"},      {b12, 0x80, "Heavy Duty"}, {b10, 0x80, "Whites"},
      {b8, 0x80, "Bedding"},      {b6, 0x80, "Sanitize"},    {b12, 0x40, "Rinse & Spin"},
      {b10, 0x40, "Perm Press"},  {b8, 0x40, "Delicates"},   {b6, 0x40, "Quick Wash"},
      {b14, 0x40, "Self Clean"},
  };
  const char *found = nullptr;
  for (const auto &program : programs) {
    if ((program.value & program.mask) == 0) continue;
    if (found != nullptr) return nullptr;
    found = program.name;
  }
  return found;
}

inline const char *washer_dc92_02395a_temperature_text(int level) {
  switch (level) {
    case 0: return "No indicator";
    case 1: return "Cold";
    case 2: return "2";
    case 3: return "3";
    case 4: return "4";
    case 5: return "Hot";
    default: return "Unknown";
  }
}

// DC92-02395A seven-segment digit. Bits 0 through 6 are segments a through g.
// Bit 7 is not a segment. The 7 glyph includes segment f.
inline int washer_dc92_02395a_digit(uint8_t value) {
  switch (value & 0x7f) {
    case 0x3f: return 0;
    case 0x06: return 1;
    case 0x5b: return 2;
    case 0x4f: return 3;
    case 0x66: return 4;
    case 0x6d: return 5;
    case 0x7d: return 6;
    case 0x27: return 7;
    case 0x7f: return 8;
    case 0x6f: return 9;
    default: return -1;
  }
}

// Delay End uses the clock digits: byte 11 is the hour tens, byte 13 the hour ones,
// and byte 15 is the letter h (segments c, e, f and g). A blank tens digit is zero.
inline int washer_dc92_02395a_delay_hours(uint8_t tens_glyph, uint8_t ones_glyph, uint8_t suffix_glyph) {
  if ((suffix_glyph & 0x7f) != 0x74) return -1;
  int tens = (tens_glyph & 0x7f) == 0 ? 0 : washer_dc92_02395a_digit(tens_glyph);
  int ones = washer_dc92_02395a_digit(ones_glyph);
  if (tens < 0 || ones < 0) return -1;
  int hours = tens * 10 + ones;
  return hours >= 1 && hours <= 24 ? hours : -1;
}

// Byte 11 is the hour, byte 13 the minute tens, and byte 15 the minute ones.
// A blank hour or minute-tens digit is zero. A blank ones digit is not a time.
inline int washer_dc92_02395a_time_minutes(uint8_t hour_glyph, uint8_t tens_glyph, uint8_t ones_glyph) {
  int hour = (hour_glyph & 0x7f) == 0 ? 0 : washer_dc92_02395a_digit(hour_glyph);
  int tens = (tens_glyph & 0x7f) == 0 ? 0 : washer_dc92_02395a_digit(tens_glyph);
  int ones = washer_dc92_02395a_digit(ones_glyph);
  if (hour < 0 || tens < 0 || tens > 5 || ones < 0) return -1;
  return hour * 60 + tens * 10 + ones;
}

inline int washer_delay_hours(uint8_t tens_glyph, uint8_t ones_glyph, uint8_t suffix_glyph) {
  if ((suffix_glyph & 0x7f) != 0x1e) return -1;
  int tens = (tens_glyph & 0x7f) == 0 ? 0 : washer_digit(tens_glyph);
  int ones = washer_digit(ones_glyph);
  if (tens < 0 || ones < 0) return -1;
  int hours = tens * 10 + ones;
  return hours >= 1 && hours <= 24 ? hours : -1;
}
