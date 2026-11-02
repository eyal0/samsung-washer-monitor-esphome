#pragma once

#include "esphome/components/uart/uart.h"
#include "esphome/core/component.h"
#include "washer_protocol.h"

#ifdef USE_BINARY_SENSOR
#include "esphome/components/binary_sensor/binary_sensor.h"
#endif
#ifdef USE_SWITCH
#include "esphome/components/switch/switch.h"
#endif
#ifdef USE_TEXT_SENSOR
#include "esphome/components/text_sensor/text_sensor.h"
#endif

namespace esphome::samsung_washer {

class SamsungWasher;

#ifdef USE_SWITCH
// Diagnostic switch. Off after every boot, matching the previous template switch.
class RawCaptureSwitch final : public switch_::Switch, public Component, public Parented<SamsungWasher> {
 public:
  void setup() override;

 protected:
  void write_state(bool state) override;
};
#endif

// Receive-only listener for the DC92-01950B panel bus. It never writes UART data.
class SamsungWasher final : public Component, public uart::UARTDevice {
#ifdef USE_BINARY_SENSOR
  SUB_BINARY_SENSOR(panel_on)
  SUB_BINARY_SENSOR(uart_link)
  SUB_BINARY_SENSOR(bubble_soak)
  SUB_BINARY_SENSOR(easy_iron)
  SUB_BINARY_SENSOR(door_lock)
#endif
#ifdef USE_TEXT_SENSOR
  SUB_TEXT_SENSOR(cycle_status)
  SUB_TEXT_SENSOR(program)
  SUB_TEXT_SENSOR(temperature)
  SUB_TEXT_SENSOR(spin)
  SUB_TEXT_SENSOR(rinse)
  SUB_TEXT_SENSOR(displayed_time)
  SUB_TEXT_SENSOR(option)
#endif
#ifdef USE_SWITCH
  SUB_SWITCH(raw_capture)
#endif

 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Optional second RX, CN803 pin 2. Bytes are logged only while raw capture is on.
  void set_raw_uart(uart::UARTComponent *uart) { this->raw_uart_ = uart; }
  void set_raw_capture(bool enabled);

 protected:
  static constexpr uint32_t LINK_TIMEOUT_MS = 5000;
  static constexpr uint32_t PANEL_BLANK_MS = 2000;

  enum class PanelState : uint8_t { UNKNOWN, ON, OFF };

  struct RawChunk {
    static constexpr size_t BYTES = 32;
    static constexpr uint32_t GAP_MS = 50;

    explicit RawChunk(const char *label) : label(label) {}

    const char *label;
    uint8_t data[BYTES]{};
    uint8_t size{0};
    uint32_t last_ms{0};

    void push(uint8_t byte, bool enabled);
    void flush_if_stale(uint32_t now, bool enabled);
    void clear();

   protected:
    void flush_();
  };

  void handle_frame_(const std::vector<uint8_t> &frame);
  void update_link_(uint32_t now);
#ifdef USE_TEXT_SENSOR
  void publish_text_(text_sensor::TextSensor *sensor, const char *value);
#endif
#ifdef USE_BINARY_SENSOR
  void publish_binary_(binary_sensor::BinarySensor *sensor, bool value);
#endif

  uart::UARTComponent *raw_uart_{nullptr};
  WasherParser parser_{PanelFrame::LENGTH};
  WasherCycleStatus cycle_{};
  RawChunk pin1_chunk_{"CN803 pin 1"};
  RawChunk pin2_chunk_{"CN803 pin 2"};
  uint32_t last_frame_ms_{0};
  uint32_t blank_since_ms_{0};
  PanelState panel_{PanelState::UNKNOWN};
  bool link_online_{false};
  bool raw_capture_{false};
};

}  // namespace esphome::samsung_washer
