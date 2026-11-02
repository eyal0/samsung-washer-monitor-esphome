#include "washer_protocol.h"

#include <cassert>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
#include <vector>

using esphome::samsung_washer::PanelFrame;
using esphome::samsung_washer::WasherCycleStatus;
using esphome::samsung_washer::WasherParser;
using esphome::samsung_washer::bubble_soak;
using esphome::samsung_washer::door_locked;
using esphome::samsung_washer::easy_iron;
using esphome::samsung_washer::format_displayed_time;
using esphome::samsung_washer::panel_lit;
using esphome::samsung_washer::program_name;
using esphome::samsung_washer::rinse_selection;
using esphome::samsung_washer::spin_selection;
using esphome::samsung_washer::temperature_selection;
using esphome::samsung_washer::washer_delay_hours;
using esphome::samsung_washer::washer_digit;
using esphome::samsung_washer::washer_option;
using esphome::samsung_washer::washer_time_minutes;

namespace {

uint16_t frame_crc(const uint8_t *data, size_t length) {
  uint16_t crc = 0xffff;
  for (size_t i = 0; i < length; ++i) {
    crc ^= uint16_t(data[i]) << 8;
    for (int bit = 0; bit < 8; ++bit)
      crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
  }
  return crc;
}

std::vector<uint8_t> encoded_frame() {
  std::vector<uint8_t> frame(PanelFrame::LENGTH, 0);
  frame[0] = 0x5a;
  frame[1] = 0xa5;
  frame[PanelFrame::HOUR] = 0x6b;
  frame[PanelFrame::LOCK] = 0x01;
  frame[PanelFrame::MINUTE_TENS] = 0x5d;
  frame[PanelFrame::MINUTE_ONES] = 0x77;
  frame[PanelFrame::OPTIONS] = 0x0f;
  frame[PanelFrame::SPIN] = 0x04;
  frame[PanelFrame::RINSE] = 0x08;
  frame[PanelFrame::TEMPERATURE] = 0x04;
  frame[PanelFrame::PROGRAM_A] = 0x08;
  const uint16_t crc = frame_crc(frame.data(), frame.size() - 2);
  frame[frame.size() - 2] = crc >> 8;
  frame[frame.size() - 1] = crc & 0xff;
  return frame;
}

int count_frames(const std::vector<uint8_t> &bytes, size_t length) {
  WasherParser parser(length);
  int found = 0;
  for (auto byte : bytes)
    parser.feed(byte, [&](const std::vector<uint8_t> &) { ++found; });
  return found;
}

void test_frame() {
  const std::vector<uint8_t> frame = encoded_frame();
  WasherParser parser(PanelFrame::LENGTH);
  int found = 0;
  parser.feed(0x00, [&](const std::vector<uint8_t> &) { ++found; });
  parser.feed(0x11, [&](const std::vector<uint8_t> &) { ++found; });
  for (auto byte : frame) {
    parser.feed(byte, [&](const std::vector<uint8_t> &got) {
      ++found;
      assert(got == frame);
      assert(std::string(program_name(got[PanelFrame::PROGRAM_A], got[PanelFrame::PROGRAM_B])) == "Cotton");
      assert(std::string(temperature_selection(got[PanelFrame::TEMPERATURE])) == "40 °C");
      assert(std::string(spin_selection(got[PanelFrame::SPIN])) == "800 rpm");
      assert(std::string(rinse_selection(got[PanelFrame::RINSE])) == "2");
      assert(std::string(washer_option(got[PanelFrame::OPTIONS])) == "Rinse+");
      assert(bubble_soak(got[PanelFrame::OPTIONS]));
      assert(easy_iron(got[PanelFrame::OPTIONS]));
      assert(door_locked(got[PanelFrame::LOCK]));
      assert(panel_lit(got.data(), got.size()));
      char displayed[32];
      format_displayed_time(got[PanelFrame::HOUR], got[PanelFrame::MINUTE_TENS], got[PanelFrame::MINUTE_ONES],
                            displayed, sizeof(displayed));
      assert(std::string(displayed) == "2:50");
    });
  }
  assert(found == 1);

  std::vector<uint8_t> broken = frame;
  broken.back() ^= 0xff;
  assert(count_frames(broken, PanelFrame::LENGTH) == 0);

  // A bad second header byte must not swallow the following frame.
  std::vector<uint8_t> resync = {0x5a, 0x00};
  resync.insert(resync.end(), frame.begin(), frame.end());
  assert(count_frames(resync, PanelFrame::LENGTH) == 1);
  assert(count_frames(frame, PanelFrame::LENGTH) == 1);
}

void test_indicators() {
  assert(program_name(0x08, 0) != nullptr && std::string(program_name(0x08, 0)) == "Cotton");
  assert(program_name(0, 0x40) != nullptr && std::string(program_name(0, 0x40)) == "Bedding");
  assert(program_name(0x20, 0) != nullptr && std::string(program_name(0x20, 0)) == "Eco 40-60");
  assert(program_name(0, 0x20) != nullptr && std::string(program_name(0, 0x20)) == "15' Quick Wash");
  assert(program_name(0, 0) == nullptr);
  assert(program_name(0x08, 0x40) == nullptr);
  assert(program_name(0x03, 0) == nullptr);

  assert(std::string(temperature_selection(0)) == "No indicator");
  assert(std::string(temperature_selection(0x06)) == "40 and 60 indicators");
  assert(std::string(temperature_selection(0x03)) == "Unknown");
  assert(std::string(spin_selection(0)) == "No indicator (Rinse Hold)");
  assert(std::string(spin_selection(0x01)) == "1400 rpm");
  assert(std::string(spin_selection(0x20)) == "Unknown");
  assert(std::string(rinse_selection(0x10)) == "1");
  assert(std::string(rinse_selection(0xff)) == "Unknown");
  assert(!bubble_soak(0x01));
  assert(!easy_iron(0x02));
  assert(!door_locked(0x00));

  uint8_t dark[PanelFrame::LENGTH] = {};
  dark[0] = 0x5a;
  dark[1] = 0xa5;
  assert(!panel_lit(dark, sizeof(dark)));
  dark[21] = 0x01;
  assert(panel_lit(dark, sizeof(dark)));
  assert(!panel_lit(dark, PanelFrame::LIT_END - 1));

  char displayed[32];
  format_displayed_time(0x80, 0xeb, 0x1e, displayed, sizeof(displayed));
  assert(std::string(displayed) == "2h");
  format_displayed_time(0, 0, 0, displayed, sizeof(displayed));
  assert(std::string(displayed) == "Unknown / non-time display");
  format_displayed_time(0x30, 0x77, 0x5d, displayed, sizeof(displayed));
  assert(std::string(displayed) == "1:05");
}

void test_capture(const char *path, int length, int expected) {
  std::ifstream file(path, std::ios::binary);
  assert(file);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  WasherParser parser(length);
  int found = 0;
  WasherCycleStatus captured_cycle;
  for (auto b : bytes)
    parser.feed(b, [&](const std::vector<uint8_t> &p) {
      ++found;
      if (p.size() == 32) {
        captured_cycle.feed(p[23], p[18] != 0 || p[20] != 0);
        if (p[23] == 0x0e)
          assert(std::string(captured_cycle.status(true)) == "Completed");
      }
    });
  assert(found == expected);
  std::cout << found << " frames validated\n";
}

void test_decoder() {
  assert(washer_digit(0xf4) == 7);
  assert(washer_digit(0x1e) == -1);
  // The live panel suppresses leading digits for single-minute countdowns.
  const uint8_t digits[] = {0x77, 0x30, 0x6b, 0x79, 0x3c, 0x5d, 0x5f, 0x74, 0x7f, 0x7d};
  for (int minute = 0; minute < 10; ++minute) {
    assert(washer_time_minutes(0, 0, digits[minute]) == minute);
    assert(washer_time_minutes(0x80, 0x80, digits[minute] | 0x80) == minute);
    assert(washer_time_minutes(0, 0x77, digits[minute]) == minute);
  }
  assert(washer_time_minutes(0x6b, 0x5d, 0x77) == 170);  // 2:50
  assert(washer_time_minutes(0x30, 0x77, 0x5d) == 65);   // 1:05
  assert(washer_time_minutes(0, 0, 0) == -1);            // Blank is not zero.
  assert(washer_time_minutes(0, 0x5f, 0x77) == -1);      // Invalid minute tens.
  assert(washer_time_minutes(0, 0x30, 0x1e) == -1);      // Delay End, not H:MM.
  assert(washer_time_minutes(0x1e, 0x77, 0x77) == -1);
  assert(washer_time_minutes(0x30, 0, 0x5d) == -1);  // Not a leading blank.
  WasherCycleStatus cycle;
  auto expect = [&](uint8_t event, bool lit, const char *status) {
    cycle.feed(event, lit);
    assert(std::string(cycle.status(true)) == status);
  };
  // Blinking alone must not restore Running after reboot or UART loss.
  for (int i = 0; i < 4; ++i) {
    expect(0, false, "Unknown");
    expect(0, true, "Unknown");
  }
  expect(4, true, "Unknown");
  expect(4, true, "Unknown");  // Repeated Start cannot add a blink.
  for (int i = 0; i < 2; ++i) {
    expect(0, false, "Unknown");
    expect(0, true, "Unknown");
  }
  expect(0, false, "Unknown");
  expect(0, true, "Running");
  expect(5, true, "Paused");
  for (int i = 0; i < 4; ++i) {
    expect(0, false, "Paused");
    expect(0, true, "Paused");
  }
  expect(4, false, "Unknown");  // Resume requires fresh Start confirmation.
  for (int i = 0; i < 3; ++i) {
    expect(0, true, i == 2 ? "Running" : "Unknown");
    expect(0, false, i == 2 ? "Running" : "Unknown");
  }
  expect(0x0e, false, "Completed");
  expect(0x0e, false, "Completed");  // Repeated event is harmless.
  expect(0, false, "Completed");     // Latches after the one-frame event.
  expect(4, false, "Unknown");       // New Start clears completion.
  expect(0x0e, false, "Completed");  // Finish during pending Start.
  for (int i = 0; i < 4; ++i) {
    expect(0, false, "Completed");
    expect(0, true, "Completed");  // Old pending blinks cannot restart it.
  }
  expect(5, false, "Paused");
  cycle.reset();  // Detected panel off resets event/blink history.
  assert(std::string(cycle.status(false)) == "Off");
  expect(0, true, "Unknown");  // Panel on is not evidence of a running cycle.
  expect(0x0e, false, "Completed");  // Can observe a finish after a reboot.
  cycle.reset();                     // UART loss must also forget completion.
  expect(0, true, "Unknown");
  assert(std::string(washer_option(0x00)) == "None");
  assert(std::string(washer_option(0x08)) == "Intensive");
  assert(std::string(washer_option(0x04)) == "Prewash");
  assert(std::string(washer_option(0x0f)) == "Rinse+");
  assert(washer_delay_hours(0x80, 0xeb, 0x1e) == 2);
  assert(washer_delay_hours(0xb0, 0xbc, 0x1e) == 14);
  assert(washer_delay_hours(0xeb, 0xbc, 0x1e) == 24);
  assert(washer_delay_hours(0xeb, 0xdd, 0x1e) == -1);
  assert(washer_delay_hours(0x80, 0xeb, 0x77) == -1);
}

}  // namespace

int main(int argc, char **argv) {
  if (argc == 4) {
    test_capture(argv[1], std::stoi(argv[2]), std::stoi(argv[3]));
  } else if (argc != 1) {
    std::cerr << "usage: test_parser [capture.bin length expected_frames]\n";
    return 2;
  }
  test_decoder();
  test_indicators();
  test_frame();
  std::cout << "ok\n";
}
