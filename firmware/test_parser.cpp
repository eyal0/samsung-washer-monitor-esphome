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
  assert(washer_dc92_02395a_bar_level(0x00) == 0);
  assert(washer_dc92_02395a_bar_level(0x10) == 1);
  assert(washer_dc92_02395a_bar_level(0x18) == 2);
  assert(washer_dc92_02395a_bar_level(0x1c) == 3);
  assert(washer_dc92_02395a_bar_level(0x1e) == 4);
  assert(washer_dc92_02395a_bar_level(0x1f) == 5);
  assert(washer_dc92_02395a_bar_level(0x50) == 1); // Soil keeps 0x40 beside the bar.
  assert(washer_dc92_02395a_bar_level(0x14) == -1); // A gap is not a level.
  assert(std::string(washer_dc92_02395a_bar_text(0)) == "0");
  assert(std::string(washer_dc92_02395a_bar_text(5)) == "5");
  assert(std::string(washer_dc92_02395a_bar_text(-1)) == "Unknown");
  assert(std::string(washer_dc92_02395a_spin_text(0)) == "0");
  assert(std::string(washer_dc92_02395a_spin_text(1)) == "No Spin");
  assert(std::string(washer_dc92_02395a_spin_text(4)) == "4");
  assert(std::string(washer_dc92_02395a_spin_text(5)) == "High");
  assert(std::string(washer_dc92_02395a_spin_text(-1)) == "Unknown");
  assert(std::string(washer_dc92_02395a_soil_text(0)) == "0");
  assert(std::string(washer_dc92_02395a_soil_text(1)) == "Light");
  assert(std::string(washer_dc92_02395a_soil_text(3)) == "3");
  assert(std::string(washer_dc92_02395a_soil_text(5)) == "Heavy");
  assert(std::string(washer_dc92_02395a_soil_text(-1)) == "Unknown");
  assert(washer_dc92_02395a_temperature(0, 0, 0, 0, 0) == 0);
  assert(washer_dc92_02395a_temperature(0, 0, 0, 0, 1) == 1);
  assert(washer_dc92_02395a_temperature(0, 0, 0, 1, 1) == 2);
  assert(washer_dc92_02395a_temperature(0, 0, 1, 1, 1) == 3);
  assert(washer_dc92_02395a_temperature(0, 1, 1, 1, 1) == 4);
  assert(washer_dc92_02395a_temperature(1, 1, 1, 1, 1) == 5);
  assert(washer_dc92_02395a_temperature(1, 0, 0, 0, 0) == -1); // Hot lit while cold is off.
  assert(washer_dc92_02395a_temperature(0, 0, 0, 0, 0x81) == 1); // High bit of byte 12 is not the LED.
  assert(washer_dc92_02395a_temperature(0, 0, 0, 1, 0x81) == 2);
  assert(washer_dc92_02395a_temperature(1, 1, 1, 1, 0x81) == 5);
  assert(washer_dc92_02395a_temperature(0, 0, 0, 0, 0x80) == 0);
  assert(std::string(washer_dc92_02395a_temperature_text(0)) == "No indicator");
  assert(std::string(washer_dc92_02395a_temperature_text(1)) == "Cold");
  assert(std::string(washer_dc92_02395a_temperature_text(3)) == "3");
  assert(std::string(washer_dc92_02395a_temperature_text(5)) == "Hot");
  const uint8_t dc92_digits[] = {0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x27, 0x7f, 0x6f};
  for (int digit = 0; digit < 10; ++digit)
    assert(washer_dc92_02395a_digit(dc92_digits[digit]) == digit);
  assert(washer_dc92_02395a_digit(0x86) == 1); // Bit 7 is not a segment.
  assert(washer_dc92_02395a_digit(0xa7) == 7);
  assert(washer_dc92_02395a_digit(0) == -1);
  assert(washer_dc92_02395a_time_minutes(0, 0xe6, 0x86) == 41);
  assert(washer_dc92_02395a_time_minutes(0, 0x66, 0x86) == 41); // Same digit with bit 7 clear.
  assert(washer_dc92_02395a_time_minutes(0, 0xcf, 0xa7) == 37);
  assert(washer_dc92_02395a_time_minutes(0, 0, 0x06) == 1); // Blank minute tens.
  assert(washer_dc92_02395a_time_minutes(0x06, 0xe6, 0xbf) == 100); // 1:40
  assert(washer_dc92_02395a_time_minutes(0x5b, 0x86, 0xcf) == 133); // 2:13
  assert(washer_dc92_02395a_time_minutes(0, 0x66, 0) == -1); // Blank ones digit.
  assert(washer_dc92_02395a_time_minutes(0, 0x7d, 0x3f) == -1); // Minute tens above 5.
  struct Dc92Sample {
    const char *hex;
    int temperature;
    int rinse;
    int spin;
    int soil;
  };
  const Dc92Sample samples[] = {
      {"5AA5C018001E005C0000000001E680FF00800000000000FF00000000002300A907", 1, 2, 4, 3},
      {"5AA5C018001E005C0000010001E680FD00800000000000FF000000000023003C98", 2, 2, 4, 3},
      {"5AA5C018001E005C0100010001E680ED00800000000000FF000000000023007A99", 3, 2, 4, 3},
      {"5AA5C018001E015C0100010001ED80DB00800000000000FF000000000023006B95", 4, 2, 4, 3},
      {"5AA5C018011E015C0118010601E680BF00800000000000FF000000000023009B4F", 5, 2, 4, 3},
      {"5AA5C018001F005F0018000681ED00EF00800000000000FF00000000002300B2FA", 1, 2, 5, 5},
      {"5AA5C018001F005F0018010681ED00EF00800000000000FF00000000002300670C", 2, 2, 5, 5},
      {"5AA5C018001F005F0118010681ED00EF00800000000000FF000000000023000207", 3, 2, 5, 5},
      {"5AA5C018001F015F0118010681ED00EF00800000000000FF0000000000230047B3", 4, 2, 5, 5},
      {"5AA5C018011F015F0118015B818600CF00800000000000FF000000000023006364", 5, 2, 5, 5},
      {"5AA5C000001E005C0100010001CF806D00800000000000FF00000000002300DBEA", 3, 0, 4, 3},
      {"5AA5C010001E005C0100010001E680DB00800000000000FF00000000002300EBBD", 3, 1, 4, 3},
      {"5AA5C018001E005C0100010001E680ED00800000000000FF000000000023007A99", 3, 2, 4, 3},
      {"5AA5C01C001E005C0100010001ED80DB00800000000000FF00000000002300BC71", 3, 3, 4, 3},
      {"5AA5C01E001E005C0100010001ED80EF00800000000000FF000000000023001095", 3, 4, 4, 3},
      {"5AA5C01F001E005C0118010601BF80FD00800000000000FF00000000002300F00E", 3, 5, 4, 3},
      {"5AA5C010001F00500100010001CF80FF00800000000000FF0000000000230001AE", 3, 1, 5, 1},
      {"5AA5C010001F00580100010001E680BF00800000000000FF00000000002300D13E", 3, 1, 5, 2},
      {"5AA5C010001F005C0100010001E680CF00800000000000FF00000000002300E404", 3, 1, 5, 3},
      {"5AA5C010001F005E0100010001E680FD00800000000000FF00000000002300AF75", 3, 1, 5, 4},
      {"5AA5C010001F005F0118010601CF80EF00800000000000FF0000000000230051DB", 3, 1, 5, 5},
  };
  auto decode_hex = [](const char *hex) {
    std::vector<uint8_t> bytes;
    while (hex[0] != 0 && hex[1] != 0) {
      auto nibble = [](char c) { return c <= '9' ? c - '0' : c - 'A' + 10; };
      bytes.push_back((nibble(hex[0]) << 4) | nibble(hex[1]));
      hex += 2;
    }
    return bytes;
  };
  WasherParser dc92_parser(33);
  WasherParser old_parser(32);
  int dc92_frames = 0;
  int old_frames = 0;
  for (const auto &sample : samples) {
    auto bytes = decode_hex(sample.hex);
    assert(bytes.size() == 33);
    for (auto b : bytes) {
      old_parser.feed(b, [&](const std::vector<uint8_t> &) { ++old_frames; });
      dc92_parser.feed(b, [&](const std::vector<uint8_t> &p) {
        ++dc92_frames;
        assert(washer_dc92_02395a_temperature(p[4], p[6], p[8], p[10], p[12]) == sample.temperature);
        assert(washer_dc92_02395a_bar_level(p[3]) == sample.rinse);
        assert(washer_dc92_02395a_bar_level(p[5]) == sample.spin);
        assert(washer_dc92_02395a_bar_level(p[7]) == sample.soil);
      });
    }
  }
  assert(dc92_frames == 21);
  assert(old_frames == 0); // A 32-byte parser must not accept these frames.
  std::cout << found << " frames validated\n";
}
