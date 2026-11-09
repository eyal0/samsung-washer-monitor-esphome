"""Run with: python -m unittest discover -s tests -p 'test_*.py' (ESPHome installed)."""

from pathlib import Path
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BASE = f"""esphome:
  name: washer-config-test
esp32:
  board: esp32-c6-devkitc-1
  framework:
    type: esp-idf
logger:
  hardware_uart: USB_SERIAL_JTAG
external_components:
  - source:
      type: local
      path: {ROOT / 'components'}
    components: [samsung_washer]
uart:
  - id: primary
    rx_pin: GPIO1
    baud_rate: 9600
  - id: secondary
    rx_pin: GPIO2
    baud_rate: 9600
samsung_washer:
  uart_id: primary
  secondary_uart_id: secondary
"""


class ConfigTest(unittest.TestCase):
    def validate(self, config, valid, message=""):
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / "washer.yaml"
            path.write_text(config)
            (Path(tmp) / "secrets.yaml").write_text(
                'wifi_ssid: "test"\nwifi_password: "test-password"\n'
                'ota_password: "test-password"\n'
                'api_encryption_key: "AAECAwQFBgcICQoLDA0ODxAREhMUFRYXGBkaGxwdHh8="\n'
            )
            (Path(tmp) / "secrets-thread.yaml").write_text(
                'tlv: "0e080000000000010000000300000f"\n'
            )
            result = subprocess.run(
                [sys.executable, "-m", "esphome", "config", str(path)],
                capture_output=True, text=True, timeout=60,
            )
            output = result.stdout + result.stderr
            self.assertEqual(result.returncode == 0, valid, output)
            if message:
                self.assertIn(message, output)

    def test_valid_configs(self):
        def example(name):
            return (ROOT / "firmware" / name).read_text().replace(
                "source: github://kralonur/samsung-washer-monitor-esphome@main",
                f"source:\n      type: local\n      path: {ROOT / 'components'}",
            )

        configs = {
            "example": example("washer.yaml"),
            "thread_example": example("washer-thread.yaml"),
            "dc92_02395a_example": example("washer-dc92-02395a.yaml"),
            "minimal": BASE.replace("  secondary_uart_id: secondary\n", ""),
            "dual_uart": BASE,
            "cycle_only": BASE + "  cycle_status:\n    name: Cycle status\n",
            "filtered": BASE + "  panel_on:\n    name: Panel\n    filters:\n      - invert:\n",
            "two_washers": BASE.replace(
                "  uart_id: primary\n  secondary_uart_id: secondary\n",
                "  - uart_id: primary\n  - uart_id: secondary\n",
            ),
            "uart_debug_without_receiver": BASE.replace(
                "    rx_pin: GPIO1\n",
                "    rx_pin: GPIO1\n    debug:\n      dummy_receiver: false\n",
            ),
        }
        for name, config in configs.items():
            with self.subTest(name=name):
                self.validate(config, True)

    def test_examples_differ_only_in_network(self):
        def without_network(name):
            text = (ROOT / "firmware" / name).read_text()
            # Drop the "# Network:" comment and the network blocks that follow it.
            return re.sub(r"# Network:.*?\n(?=uart:|external_components:)", "", text, flags=re.S)

        self.assertEqual(without_network("washer.yaml"), without_network("washer-thread.yaml"))

    def test_invalid_uart_settings(self):
        for index in (0, 1):
            for setting, replacement, message in (
                ("baud_rate: 9600", "baud_rate: 115200", "requires baud rate 9600"),
                ("baud_rate: 9600", "baud_rate: 9600\n    parity: EVEN", "requires parity NONE"),
                ("baud_rate: 9600", "baud_rate: 9600\n    data_bits: 7", "requires 8 data bits"),
                ("baud_rate: 9600", "baud_rate: 9600\n    stop_bits: 2", "requires 1 stop bits"),
                (f"rx_pin: GPIO{index + 1}", f"tx_pin: GPIO{index + 1}", "declare a rx_pin"),
            ):
                with self.subTest(uart=index, replacement=replacement):
                    marker = f"  - id: {'primary' if index == 0 else 'secondary'}"
                    before, bus = BASE.split(marker, 1)
                    config = before + marker + bus.replace(setting, replacement, 1)
                    self.validate(config, False, message)

    def test_uart_ownership(self):
        self.validate(BASE.replace("secondary_uart_id: secondary", "secondary_uart_id: primary"), False, "used both")
        self.validate(BASE.replace(
            "  uart_id: primary\n  secondary_uart_id: secondary\n",
            "  - uart_id: primary\n  - uart_id: primary\n",
        ), False, "used both")

    def test_model_selects_pcb(self):
        new_board = BASE.replace("baud_rate: 9600", "baud_rate: 38400").replace(
            "  uart_id: primary\n", "  model: dc92-02395a\n  uart_id: primary\n"
        )
        self.validate(new_board, True)
        self.validate(new_board.replace("model: dc92-02395a", "model: dc92_02395a"), True)
        self.validate(BASE.replace(
            "  uart_id: primary\n", "  model: not-a-board\n  uart_id: primary\n"
        ), False, "Unknown value")
        self.validate(BASE.replace(
            "  uart_id: primary\n", "  model: dc92-02395a\n  uart_id: primary\n"
        ), False, "requires baud rate 38400")
        self.validate(BASE.replace("baud_rate: 9600", "baud_rate: 38400"), False, "requires baud rate 9600")

    def test_dummy_receiver_rejected(self):
        for pin in (1, 2):
            with self.subTest(pin=pin):
                self.validate(BASE.replace(
                    f"    rx_pin: GPIO{pin}\n",
                    f"    rx_pin: GPIO{pin}\n    debug:\n      dummy_receiver: true\n",
                ), False, "dummy_receiver")


if __name__ == "__main__":
    unittest.main()
