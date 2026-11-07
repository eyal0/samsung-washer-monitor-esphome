#include "../components/samsung_washer/washer_parser.h"
#include <cassert>
#include <fstream>
#include <iterator>
#include <iostream>

int main(int argc, char **argv) {
  assert(argc == 4);
  for (size_t length : {30, 32, 33}) {
    std::vector<uint8_t> frame(length, 0);
    frame[0] = 0x5a;
    frame[1] = 0xa5;
    uint16_t crc = 0xffff;
    for (size_t i = 0; i < length - 2; ++i) {
      crc ^= uint16_t(frame[i]) << 8;
      for (int bit = 0; bit < 8; ++bit)
        crc = (crc & 0x8000) ? (crc << 1) ^ 0x1021 : crc << 1;
    }
    frame[length - 2] = crc >> 8;
    frame[length - 1] = crc & 0xff;
    WasherParser framing(length);
    int frames = 0;
    auto feed = [&](const std::vector<uint8_t> &bytes) {
      for (auto b : bytes) framing.feed(b, [&](const std::vector<uint8_t> &p) {
        assert(p == frame);
        ++frames;
      });
    };
    feed({0xff, 0, 0x5a, 0x5a}); // Noise and repeated header prefix.
    feed(frame);
    feed(frame); // Back-to-back frames.
    assert(frames == 2);
    auto corrupt = frame;
    corrupt[7] ^= 1;
    feed(corrupt);
    assert(frames == 2); // Bad CRC must not reach the callback.
    auto dropped = frame;
    dropped.erase(dropped.begin() + 7);
    feed(dropped);
    feed(frame);
    assert(frames == 3); // Recover a header inside a rejected frame.
    auto inserted = frame;
    inserted.insert(inserted.begin() + 7, 0xff);
    feed(inserted);
    feed(frame);
    assert(frames == 4);
    // Framing state belongs to the parser instance, not the other UART.
    WasherParser other(length);
    other.feed(0x5a, [](const std::vector<uint8_t> &) { assert(false); });
    feed(frame);
    assert(frames == 5);
  }
  std::ifstream file(argv[1], std::ios::binary);
  assert(file);
  std::vector<uint8_t> bytes{std::istreambuf_iterator<char>(file), {}};
  WasherParser parser(std::stoi(argv[2]));
  int found = 0;
  WasherCycleStatus captured_cycle;
  for (auto b : bytes) parser.feed(b, [&](const std::vector<uint8_t> &p) {
    ++found;
    if (p.size() == 32) {
      captured_cycle.feed(p[23], p[18] != 0 || p[20] != 0);
      if (p[23] == 0x0e) assert(std::string(captured_cycle.status(true)) == "Completed");
    }
  });
  assert(found == std::stoi(argv[3]));
  assert(washer_digit(0xf4) == 7);
  assert(washer_digit(0x1e) == -1);
  // The live panel suppresses leading digits for single-minute countdowns.
  const uint8_t digits[] = {0x77, 0x30, 0x6b, 0x79, 0x3c, 0x5d, 0x5f, 0x74, 0x7f, 0x7d};
  for (int minute = 0; minute < 10; ++minute) {
    assert(washer_time_minutes(0, 0, digits[minute]) == minute);
    assert(washer_time_minutes(0x80, 0x80, digits[minute] | 0x80) == minute);
    assert(washer_time_minutes(0, 0x77, digits[minute]) == minute);
  }
  assert(washer_time_minutes(0x6b, 0x5d, 0x77) == 170); // 2:50
  assert(washer_time_minutes(0x30, 0x77, 0x5d) == 65); // 1:05
  assert(washer_time_minutes(0, 0, 0) == -1); // Blank is not zero.
  assert(washer_time_minutes(0, 0x5f, 0x77) == -1); // Invalid minute tens.
  assert(washer_time_minutes(0, 0x30, 0x1e) == -1); // Delay End, not H:MM.
  assert(washer_time_minutes(0x1e, 0x77, 0x77) == -1);
  assert(washer_time_minutes(0x30, 0, 0x5d) == -1); // Not a leading blank.
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
  expect(4, true, "Unknown"); // Repeated Start cannot add a blink.
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
  expect(4, false, "Unknown"); // Resume requires fresh Start confirmation.
  for (int i = 0; i < 3; ++i) {
    expect(0, true, i == 2 ? "Running" : "Unknown");
    expect(0, false, i == 2 ? "Running" : "Unknown");
  }
  expect(0x0e, false, "Completed");
  expect(0x0e, false, "Completed"); // Repeated event is harmless.
  expect(0, false, "Completed"); // Latches after the one-frame event.
  expect(4, false, "Unknown"); // New Start clears completion.
  expect(0x0e, false, "Completed"); // Finish during pending Start.
  for (int i = 0; i < 4; ++i) {
    expect(0, false, "Completed");
    expect(0, true, "Completed"); // Old pending blinks cannot restart it.
  }
  expect(5, false, "Paused");
  cycle.reset(); // Detected panel off resets event/blink history.
  assert(std::string(cycle.status(false)) == "Off");
  expect(0, true, "Unknown"); // Panel on is not evidence of a running cycle.
  expect(0x0e, false, "Completed"); // Can observe a finish after a reboot.
  cycle.reset(); // UART loss must also forget completion.
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
  const uint8_t bars[] = {0x00, 0x10, 0x18, 0x1c, 0x1e, 0x1f};
  for (int level = 0; level <= 5; ++level) assert(washer_bar_level(bars[level]) == level);
  assert(washer_bar_level(0x5c) == 3); // Soil shares its byte with another indicator.
  assert(washer_bar_level(0x08) == -1);
  assert(washer_bar_level(0x1d) == -1);
  std::vector<uint8_t> temperature(33, 0);
  assert(washer_temperature_level(temperature) == 1);
  temperature[10] = 1;
  assert(washer_temperature_level(temperature) == 2);
  temperature[6] = 1;
  assert(washer_temperature_level(temperature) == -1); // Bar segments cannot skip one.
  temperature[8] = 1;
  assert(washer_temperature_level(temperature) == 4);
  temperature[4] = 2;
  assert(washer_temperature_level(temperature) == -1);
  std::cout << found << " frames validated\n";
}
