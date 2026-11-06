#pragma once

#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <deque>

#include "esphome/components/samsung_washer/samsung_washer.h"

namespace esphome::samsung_washer::test {

class FakeClock : public time::RealTimeClock {
 public:
  void update() override {}
  time_t timestamp_now() override { return 1700000000; }
};

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

inline void seal(std::vector<uint8_t> &p) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < p.size() - 2; ++i) {
    crc ^= uint16_t(p[i]) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  p[p.size() - 2] = crc >> 8;
  p[p.size() - 1] = crc & 0xff;
}

inline std::vector<uint8_t> frame(bool lit, uint8_t event = 0) {
  std::vector<uint8_t> p(32, 0);
  p[0] = 0x5a;
  p[1] = 0xa5;
  if (lit) {
    p[4] = 0x30;  // 1:05
    p[5] = 1;     // Door lock
    p[6] = 0x77;
    p[8] = 0x5d;
    p[10] = 0x0b;  // Intensive, Bubble Soak, Easy Iron
    p[12] = 0x04;  // 800
    p[14] = 0x08;  // 2 rinses
    p[16] = 0x04;  // 40
    p[18] = 0x08;  // Cotton
  }
  p[23] = event;
  seal(p);
  return p;
}

inline void feed(SamsungWasherComponent &washer, TestUART &uart, const std::vector<uint8_t> &p) {
  uart.bytes.insert(uart.bytes.end(), p.begin(), p.end());
  while (uart.available()) washer.loop();
}

template<size_t N> void set_options(select::Select &sel, const char *const (&options)[N]) {
  sel.traits.set_options_static(options, N);
}

inline void run() {
  static const char *const cycles[] = {
      "15' Quick Wash", "Mixed Load", "Delicates", "Wool", "Intense Cold", "Colors", "Bedding",
      "Eco 40-60", "Cotton", "Synthetics", "Hygiene Steam", "Rinse+Spin", "Drain/Spin", "Drum Clean",
  };
  static const char *const temperatures[] = {"None", "20", "30", "40", "60", "90", "40 and 60 indicators", "Unknown"};
  static const char *const spins[] = {"Rinse hold", "No spin", "400", "800", "1200", "1400", "Unknown"};
  static const char *const rinses[] = {"0", "1", "2", "3", "4", "5", "Unknown"};

  TestUART uart, second_uart;
  SamsungWasherComponent washer;
  text_sensor::TextSensor machine, progress;
  PanelSelect cycle, temperature, spin, rinse;
  set_options(cycle, cycles);
  set_options(temperature, temperatures);
  set_options(spin, spins);
  set_options(rinse, rinses);
  sensor::Sensor completion, finish;
  PanelNumber delay_start;
  PanelSwitch bubble, pre_wash, intensive, extra_rinse;
  binary_sensor::BinarySensor power, link, iron, lock, running;
  washer.set_uart_parent(&uart);
  washer.set_secondary_uart(&second_uart);
  washer.set_machine_state_text_sensor(&machine);
  washer.set_progress_text_sensor(&progress);
  washer.set_cycle_select(&cycle);
  washer.set_wash_temperature_select(&temperature);
  washer.set_spin_speed_select(&spin);
  washer.set_rinse_cycles_select(&rinse);
  washer.set_completion_time_sensor(&completion);
  washer.set_estimated_finish_sensor(&finish);
  washer.set_delay_start_number(&delay_start);
  washer.set_bubble_soak_switch(&bubble);
  washer.set_pre_wash_switch(&pre_wash);
  washer.set_intensive_switch(&intensive);
  washer.set_extra_rinse_switch(&extra_rinse);
  washer.set_power_binary_sensor(&power);
  washer.set_link_active_binary_sensor(&link);
  washer.set_iron_indicator_binary_sensor(&iron);
  washer.set_lock_indicator_binary_sensor(&lock);
  washer.set_running_binary_sensor(&running);
  washer.update();
  assert(!link.state && !power.has_state() && !machine.has_state() && !running.has_state());

  // CRC errors and the secondary stream must never update primary entities.
  auto bad = frame(true);
  bad[30] ^= 1;
  feed(washer, uart, bad);
  second_uart.bytes.insert(second_uart.bytes.end(), bad.begin(), bad.end());
  washer.loop();
  assert(second_uart.bytes.empty() && !power.has_state());
  feed(washer, uart, frame(true));
  washer.update();
  assert(link.state && power.state && !machine.has_state() && !running.has_state());
  assert(std::string(cycle.current_option()) == "Cotton");
  assert(std::string(temperature.current_option()) == "40");
  assert(std::string(spin.current_option()) == "800");
  assert(std::string(rinse.current_option()) == "2");
  assert(completion.state == 65 && delay_start.state == 0 && !finish.has_state());
  assert(intensive.state && bubble.state && iron.state && lock.state);
  assert(!pre_wash.state && !extra_rinse.state);
  // Home Assistant writes do not change the washer, and the panel value is restored.
  cycle.make_call().set_option("Wool").perform();
  intensive.turn_off();
  delay_start.make_call().set_value(9).perform();
  assert(std::string(cycle.current_option()) == "Cotton");
  assert(intensive.state && delay_start.state == 0);

  feed(washer, uart, frame(true, 4));
  for (int i = 0; i < 3; ++i) {
    feed(washer, uart, frame(false));
    feed(washer, uart, frame(true));
  }
  assert(machine.state == "Active" && running.state);
  feed(washer, uart, frame(true, 5));
  assert(machine.state == "Paused" && !running.state);
  feed(washer, uart, frame(true, 0x0e));
  assert(machine.state == "Idle" && progress.state == "Finished" && !running.state);

  // Entity subsets must not change tracking. Compare an omitted power sensor
  // and one whose filtered state is inverted with the unfiltered monitor.
  TestUART subset_uart, filtered_uart;
  SamsungWasherComponent subset, filtered;
  text_sensor::TextSensor subset_machine, filtered_machine;
  binary_sensor::BinarySensor filtered_power, filtered_iron, filtered_link;
  binary_sensor::InvertFilter invert, invert_iron, invert_link;
  filtered_power.add_filter(&invert);
  filtered_iron.add_filter(&invert_iron);
  filtered_link.add_filter(&invert_link);
  subset.set_uart_parent(&subset_uart);
  subset.set_machine_state_text_sensor(&subset_machine);
  filtered.set_uart_parent(&filtered_uart);
  filtered.set_machine_state_text_sensor(&filtered_machine);
  filtered.set_power_binary_sensor(&filtered_power);
  filtered.set_iron_indicator_binary_sensor(&filtered_iron);
  filtered.set_link_active_binary_sensor(&filtered_link);
  filtered.update();
  assert(filtered_link.state);  // Inverted offline.
  for (auto *p : {&subset, &filtered}) {
    auto &bus = p == &subset ? subset_uart : filtered_uart;
    feed(*p, bus, frame(true, 0x0e));
    feed(*p, bus, frame(false));
  }
  assert(subset_machine.state == "Idle");
  assert(filtered_machine.state == "Idle");
  assert(!filtered_power.state && filtered_iron.state);
  filtered.update();
  assert(!filtered_link.state);  // Inverted online.

  // Ordinary panel blinking keeps completion, but a sustained blank is Off.
  feed(washer, uart, frame(false));
  assert(machine.state == "Idle" && std::string(cycle.current_option()) == "Cotton");
  delay(2100);
  feed(washer, uart, frame(false));
  feed(subset, subset_uart, frame(false));
  feed(filtered, filtered_uart, frame(false));
  assert(!machine.has_state() && progress.state == "Idle" && !power.state && !running.state);
  assert(!subset_machine.has_state() && !filtered_machine.has_state());
  assert(filtered_power.state);

  // A fresh panel after Off cannot inherit Idle or Active.
  feed(washer, uart, frame(true));
  assert(power.state && !machine.has_state());
  feed(washer, uart, frame(true, 0x0e));
  feed(subset, subset_uart, frame(true, 0x0e));
  delay(5100);
  // Traffic can resume after a timeout but before the next polling tick.
  feed(subset, subset_uart, frame(true));
  assert(!subset_machine.has_state());
  washer.update();
  assert(!link.state && !power.has_state() && !machine.has_state());
  feed(washer, uart, frame(true));
  washer.update();
  assert(link.state && power.state && !machine.has_state());

  // Delay End is LocalThings' delay duration, not the wash countdown.
  {
    TestUART delay_uart;
    SamsungWasherComponent delay_washer;
    text_sensor::TextSensor delay_progress;
    sensor::Sensor delay_completion;
    PanelNumber delay_hours;
    delay_washer.set_uart_parent(&delay_uart);
    delay_washer.set_progress_text_sensor(&delay_progress);
    delay_washer.set_completion_time_sensor(&delay_completion);
    delay_washer.set_delay_start_number(&delay_hours);
    auto delayed = frame(true);
    delayed[4] = 0x80;
    delayed[6] = 0xeb;
    delayed[8] = 0x1e;
    seal(delayed);
    feed(delay_washer, delay_uart, delayed);
    assert(delay_progress.state == "Delayed start");
    assert(delay_completion.state == 120 && delay_hours.state == 2);
    delay_hours.make_call().set_value(5).perform();
    assert(delay_hours.state == 2);
  }

  // Estimated finish is the panel countdown added to the clock, truncated to the minute.
  {
    TestUART clock_uart;
    SamsungWasherComponent clock_washer;
    FakeClock clock;
    sensor::Sensor clock_finish;
    binary_sensor::BinarySensor clock_running;
    clock_washer.set_uart_parent(&clock_uart);
    clock_washer.set_time(&clock);
    clock_washer.set_estimated_finish_sensor(&clock_finish);
    clock_washer.set_running_binary_sensor(&clock_running);
    feed(clock_washer, clock_uart, frame(true));
    assert(!clock_finish.has_state());
    feed(clock_washer, clock_uart, frame(true, 4));
    for (int i = 0; i < 3; ++i) {
      feed(clock_washer, clock_uart, frame(false));
      feed(clock_washer, clock_uart, frame(true));
    }
    assert(clock_running.state && clock_finish.has_state());
    const float expected = static_cast<float>(1700000000 - 20 + 65 * 60);
    assert(clock_finish.state == expected);
    feed(clock_washer, clock_uart, frame(true, 5));
    assert(!clock_finish.has_state());
  }

  // Raw-to-filtered text collisions must not suppress real state changes.
  TestUART mapped_uart;
  SamsungWasherComponent mapped;
  text_sensor::TextSensor mapped_machine;
  text_sensor::LambdaFilter map_machine([](std::string value) -> optional<std::string> {
    if (value == "Idle") return std::string("Paused");
    if (value == "Paused") return std::string("Stopped");
    return value;
  });
  mapped_machine.set_filters({&map_machine});
  mapped.set_uart_parent(&mapped_uart);
  mapped.set_machine_state_text_sensor(&mapped_machine);
  int publications = 0;
  mapped_machine.add_on_state_callback([&](const std::string &) { ++publications; });
  feed(mapped, mapped_uart, frame(true, 0x0e));
  assert(mapped_machine.state == "Paused");
  feed(mapped, mapped_uart, frame(true, 5));
  assert(mapped_machine.state == "Stopped");
  feed(mapped, mapped_uart, frame(true, 5));
  assert(publications == 2);  // Duplicate raw text is not republished.

  SamsungWasherComponent bare;
  TestUART bare_uart;
  bare.set_uart_parent(&bare_uart);
  feed(bare, bare_uart, frame(true));  // All entities are optional.
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
  feed(washer, uart, bad);  // Raw capture must include CRC-invalid data too.
  capture.turn_off();
  assert(!capture.state);

  std::puts("PASS: component runtime tests");
  std::exit(0);
}

}  // namespace esphome::samsung_washer::test
