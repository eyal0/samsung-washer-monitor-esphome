#pragma once

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <deque>

#include "esphome/components/samsung_washer/samsung_washer.h"

namespace esphome::samsung_washer::test {

class TestUART : public uart::UARTComponent {
 public:
  std::deque<uint8_t> bytes;
  void write_array(const uint8_t *, size_t) override { assert(false && "Monitor must never transmit"); }
  bool peek_byte(uint8_t *byte) override {
    if (bytes.empty()) return false;
    *byte = bytes.front();
    return true;
  }
  bool read_array(uint8_t *data, size_t size) override {
    if (bytes.size() < size) return false;
    while (size-- != 0) {
      *data++ = bytes.front();
      bytes.pop_front();
    }
    return true;
  }
  size_t available() override { return bytes.size(); }
  uart::UARTFlushResult flush() override { return uart::UARTFlushResult::UART_FLUSH_RESULT_SUCCESS; }

 protected:
  void check_logger_conflict() override {}
};

inline std::vector<uint8_t> frame(bool lit, uint8_t event = 0) {
  std::vector<uint8_t> p(32, 0);
  p[0] = 0x5a;
  p[1] = 0xa5;
  if (lit) {
    p[4] = 0x30;  // 1:05
    p[5] = 1;     // Door lock
    p[6] = 0x77;
    p[8] = 0x5d;
    p[10] = 0x0b; // Intensive, Bubble Soak, Easy Iron
    p[12] = 0x04; // 800 rpm
    p[14] = 0x08; // 2 rinses
    p[16] = 0x04; // 40 C
    p[18] = 0x08; // Cotton
  }
  p[23] = event;
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < p.size() - 2; ++i) {
    crc ^= uint16_t(p[i]) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  p[30] = crc >> 8;
  p[31] = crc & 0xff;
  return p;
}

inline void feed(SamsungWasherComponent &washer, TestUART &uart, const std::vector<uint8_t> &p) {
  uart.bytes.insert(uart.bytes.end(), p.begin(), p.end());
  while (uart.available()) washer.loop();
}

inline void run() {
  TestUART uart, second_uart;
  SamsungWasherComponent washer;
  text_sensor::TextSensor status, program, temperature, spin, rinse, display, option;
  binary_sensor::BinarySensor panel, link, bubble, iron, lock;
  washer.set_uart_parent(&uart);
  washer.set_secondary_uart(&second_uart);
  washer.set_cycle_status_text_sensor(&status);
  washer.set_program_indicator_text_sensor(&program);
  washer.set_temperature_selection_text_sensor(&temperature);
  washer.set_spin_selection_text_sensor(&spin);
  washer.set_rinse_selection_text_sensor(&rinse);
  washer.set_displayed_time_text_sensor(&display);
  washer.set_option_selection_text_sensor(&option);
  washer.set_panel_on_binary_sensor(&panel);
  washer.set_link_active_binary_sensor(&link);
  washer.set_bubble_indicator_binary_sensor(&bubble);
  washer.set_iron_indicator_binary_sensor(&iron);
  washer.set_lock_indicator_binary_sensor(&lock);
  washer.update();
  assert(!link.state && !panel.has_state() && status.state == "Unknown");

  // CRC errors and the secondary stream must never update primary entities.
  auto bad = frame(true);
  bad[30] ^= 1;
  feed(washer, uart, bad);
  second_uart.bytes.insert(second_uart.bytes.end(), bad.begin(), bad.end());
  washer.loop();
  assert(second_uart.bytes.empty() && !panel.has_state());
  feed(washer, uart, frame(true));
  washer.update();
  assert(link.state && panel.state && status.state == "Unknown");
  assert(program.state == "Cotton" && temperature.state == "40 °C");
  assert(spin.state == "800 rpm" && rinse.state == "2" && display.state == "1:05");
  assert(option.state == "Intensive" && bubble.state && iron.state && lock.state);

  feed(washer, uart, frame(true, 4));
  for (int i = 0; i < 3; ++i) {
    feed(washer, uart, frame(false));
    feed(washer, uart, frame(true));
  }
  assert(status.state == "Running");
  feed(washer, uart, frame(true, 5));
  assert(status.state == "Paused");
  feed(washer, uart, frame(true, 0x0e));
  assert(status.state == "Completed");

  // Entity subsets must not change tracking. Compare an omitted panel sensor
  // and one whose filtered state is inverted with the unfiltered monitor.
  TestUART subset_uart, filtered_uart;
  SamsungWasherComponent subset, filtered;
  text_sensor::TextSensor subset_status, filtered_status;
  binary_sensor::BinarySensor filtered_panel, filtered_bubble, filtered_link;
  binary_sensor::InvertFilter invert, invert_bubble, invert_link;
  filtered_panel.add_filter(&invert);
  filtered_bubble.add_filter(&invert_bubble);
  filtered_link.add_filter(&invert_link);
  subset.set_uart_parent(&subset_uart);
  subset.set_cycle_status_text_sensor(&subset_status);
  filtered.set_uart_parent(&filtered_uart);
  filtered.set_cycle_status_text_sensor(&filtered_status);
  filtered.set_panel_on_binary_sensor(&filtered_panel);
  filtered.set_bubble_indicator_binary_sensor(&filtered_bubble);
  filtered.set_link_active_binary_sensor(&filtered_link);
  filtered.update();
  assert(filtered_link.state); // Inverted offline.
  for (auto *p : {&subset, &filtered}) {
    auto &bus = p == &subset ? subset_uart : filtered_uart;
    feed(*p, bus, frame(true, 0x0e));
    feed(*p, bus, frame(false));
  }
  assert(subset_status.state == "Completed");
  assert(filtered_status.state == "Completed");
  assert(!filtered_panel.state && filtered_bubble.state);
  filtered.update();
  assert(!filtered_link.state); // Inverted online.

  // Ordinary panel blinking keeps completion, but a sustained blank is Off.
  feed(washer, uart, frame(false));
  assert(status.state == "Completed" && program.state == "Cotton");
  delay(2100);
  feed(washer, uart, frame(false));
  feed(subset, subset_uart, frame(false));
  feed(filtered, filtered_uart, frame(false));
  assert(status.state == "Off" && !panel.state);
  assert(subset_status.state == "Off" && filtered_status.state == "Off");
  assert(filtered_panel.state);

  // A fresh panel after Off cannot inherit Completed or Running.
  feed(washer, uart, frame(true));
  assert(panel.state && status.state == "Unknown");
  feed(washer, uart, frame(true, 0x0e));
  feed(subset, subset_uart, frame(true, 0x0e));
  delay(5100);
  // Traffic can resume after a timeout but before the next polling tick.
  feed(subset, subset_uart, frame(true));
  assert(subset_status.state == "Unknown");
  washer.update();
  assert(!link.state && !panel.has_state() && status.state == "Unknown");
  feed(washer, uart, frame(true));
  washer.update();
  assert(link.state && panel.state && status.state == "Unknown");

  // Raw-to-filtered text collisions must not suppress real state changes.
  TestUART mapped_uart;
  SamsungWasherComponent mapped;
  text_sensor::TextSensor mapped_status;
  text_sensor::LambdaFilter map_status([](std::string value) -> optional<std::string> {
    if (value == "Completed") return std::string("Paused");
    if (value == "Paused") return std::string("Stopped");
    return value;
  });
  mapped_status.set_filters({&map_status});
  mapped.set_uart_parent(&mapped_uart);
  mapped.set_cycle_status_text_sensor(&mapped_status);
  int publications = 0;
  mapped_status.add_on_state_callback([&](const std::string &) { ++publications; });
  feed(mapped, mapped_uart, frame(true, 0x0e));
  assert(mapped_status.state == "Paused");
  feed(mapped, mapped_uart, frame(true, 5));
  assert(mapped_status.state == "Stopped");
  feed(mapped, mapped_uart, frame(true, 5));
  assert(publications == 2); // Duplicate raw text is not republished.

  SamsungWasherComponent bare;
  TestUART bare_uart;
  bare.set_uart_parent(&bare_uart);
  feed(bare, bare_uart, frame(true)); // All entities are optional.
  bare.update();

  SamsungWasherRawCaptureSwitch inverted_capture;
  inverted_capture.set_inverted(true);
  inverted_capture.set_restore_mode(switch_::SWITCH_ALWAYS_OFF);
  inverted_capture.setup();
  assert(!inverted_capture.state);
  inverted_capture.turn_on();
  assert(inverted_capture.state);
  inverted_capture.turn_off();
  assert(!inverted_capture.state);

  SamsungWasherRawCaptureSwitch capture;
  capture.set_restore_mode(switch_::SWITCH_ALWAYS_OFF);
  capture.setup();
  assert(!capture.state);
  capture.turn_on();
  assert(capture.state);
  washer.set_raw_capture_switch(&capture);
  feed(washer, uart, bad); // Raw capture must include CRC-invalid data too.
  capture.turn_off();
  assert(!capture.state);

  std::puts("PASS: component runtime tests");
  std::exit(0);
}

}  // namespace esphome::samsung_washer::test
