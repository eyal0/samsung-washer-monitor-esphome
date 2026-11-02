# Samsung Washer Monitor for ESPHome

Local ESPHome monitoring for my **Samsung WW11BGA046**, which uses the **DC92-01950B control/display panel**. An ESP listens to the panel/main-board UART. No Samsung cloud account, no replacement of the original washer firmware.

The [DC92-01950B parts catalog](https://www.sos-parts.com/display-card-original-control-unit-samsung-dc92-01950b-133080.html) lists this panel in **80 regional washer variants**, so this may also work with other machines using the same panel. Only my machine has been tested so far.

**Receive-only:** this cannot start, stop or change a wash remotely.

**Experimental DIY project:** tested on one washer, not a qualified or plug-and-play retrofit. Read [Safety](#safety) before attempting any hardware work.

## What you get

![Home Assistant device-page Sensors card showing Cotton, 40 °C, 800 rpm, two rinses and 1:05 displayed time](docs/images/home-assistant-demo.webp)

The actual Home Assistant device-page **Sensors** card, with the entity icons configured in this project. **Demo values:** selections and indicators replayed from my washer's recorded history; cycle status is simulated, not a live reading.

- Program, temperature, spin, rinse and option selections, plus Bubble Soak and Easy Iron indicators.
- Displayed time, including Delay End.
- **Cycle status:** `Running`, `Paused`, `Completed`, `Off`, `Unknown`.
- Door-lock indicator, panel-on and UART-link diagnostics, plus an optional raw-capture switch.
- An optional Android progress/countdown and wash-finished notification through Home Assistant.

These are panel readings, not measured water temperature, drum speed or wash stages. The door-lock entity reports the panel's door-lock indicator.

Running needs a captured Start event and blink confirmation. After a reboot or UART loss, status stays Unknown until fresh evidence arrives. Other selections can retain stale values; check UART link and device availability. Completion has natural-finish capture evidence, but cancellation, errors and other programs need more testing.

## Photos

![Tested Samsung control-panel layout](docs/images/control-panel.webp)

**This is the correct control-panel layout for this project**, tested on my WW11BGA046. Use it as a visual reference, and check the installed board is **DC92-01950B**; appearance alone does not confirm compatibility.

![CN803 and CN201 marked on the control-panel board](docs/images/display-board-pinout.webp)

## Connector pinout

The photo above marks **CN803** (main harness) and **CN201** (programming header). **In this vertical photo, the top pin of each connector is pin 1**, with numbering increasing downward: CN803 pins 1–8 and CN201 pins 1–4. The small arrows on the PCB mark pin 1; check those marks on your own board rather than relying on cable orientation. In my harness, the white conductor identifies CN803 pin 1.

### CN803

| Pin | Reference signal | My checks / monitor connection |
| --- | --- | --- |
| 1 | MAIN_UI_RX | UART captured; 5 V→3.3 V level translation to ESP GPIO1 |
| 2 | MAIN_UI_TX | UART captured; 5 V→3.3 V level translation to ESP GPIO2 |
| 3 | RESET | Not verified; leave untouched |
| 4 | 5 V | Continuity to CN201 pin 1 |
| 5 | SGND | Ground; continuity to CN201 pin 2 |
| 6 | 12 V | Not used by the monitor; exact pin assignment not independently verified |
| 7 | N/C | Not verified; leave untouched |
| 8 | POWER_BUTTON | Pressing Power connects it to pin 5 |

Reference: [Samsung WW6000T service manual, §5-3 “DISPLAY PCB (5000_UI)”](https://manuals.plus/m/f48da46b4831fc53fcb9898bab72945a4b6ee0bb1638cdc777d67fa09850af2a.pdf), with [UART and 5 V/12 V supply description on printed p. 87 / viewer p. 90](https://www.manualslib.fr/manual/406510/Samsung-Ww1-T-Serie.html?page=90). The reference pin labels above were supplied during the original research; the full numbered diagram could not be independently reopened.

**My experiments match the UART connections, 5 V/ground continuity and power-button contact in this reference pinout.** This does not confirm the remaining pin functions or establish mains isolation. The RX/TX names are Samsung's reference labels; both ESP connections are receive-only.

### CN201

| Pin | My board |
| --- | --- |
| 1 | 5 V; same rail as CN803 pin 4; used to power my ESP |
| 2 | Ground; same as CN803 pin 5 |
| 3–4 | Unidentified programming/debug signals; not connected to the monitor |

These power/ground assignments come from my continuity and operating-voltage checks. No programming/debug protocol was confirmed.

### My very professional installation

![My ESP32-C6 prototype taped into the empty space on the control panel](docs/images/washer-monitor-installed.webp)

![ESP32-C6 prototype with resistor voltage dividers for the 5 V UART signals](docs/images/esp32-c6-wiring.webp)

I just used Scotch tape to stick the ESP into an empty space. Budget engineering at its finest. Poor installation? Absolutely :D I power it from the **5 V supply on the panel's programming port (CN201)**.

> [!WARNING]
> **Not recommended:** the resistor voltage divider shown below is just my prototype hack. Use a proper **5 V→3.3 V UART level shifter** instead.

The resistors in the wiring photo are **voltage dividers for the washer's 5 V UART signals**, using **10 kΩ and 20 kΩ**. My multimeter's resistance range was broken, so I stuck with 10 kΩ resistors: each 20 kΩ leg is simply two 10 kΩ resistors in series. That's why there are so many resistors in the photo!

Each UART signal has its **own** divider, wired like this:

```text
Samsung UART ── 10 kΩ ──┬── ESP UART RX
                       │
                      20 kΩ
                       │
Samsung ground ────────┴── ESP GND
```

Repeat for **CN803 pin 1 → GPIO1** and **CN803 pin 2 → GPIO2**; do not join the two signal lines. Ground is **CN803 pin 5 / CN201 pin 2**. At nominal 5 V, this divider produces about **3.33 V**; verify actual signal levels and resistor tolerances against your ESP's input limits before use.

> [!IMPORTANT]
> **If you build this yourself, prefer a proper 5 V→3.3 V UART level shifter rather than copying the resistor dividers.** I used dividers because I already had 10 kΩ resistors on hand and only needed 9600 baud receive. A passive divider has real limitations:
> - **Tight voltage margin:** 10 kΩ / 20 kΩ turns 5.00 V into ~3.33 V, leaving little headroom if the washer's 5 V rail runs high or resistor tolerances shift the ratio.
> - **Power-off / back-powering behavior:** if the washer drives UART high while the ESP is unpowered, current can flow into the ESP input protection network.
> - **Higher source impedance:** resistor dividers and wiring capacitance slow signal edges and reduce noise margin compared with a level shifter.
> - **Receive-only (`5 V → 3.3 V`):** a passive divider only steps voltage down; it cannot step a 3.3 V ESP TX output up to 5 V.

This is my prototype, not a recommended permanent mounting method or a qualified wiring guide.

## Hardware tested

- **Control/display panel:** Samsung **DC92-01950B**.
- **Washer:** Samsung WW11BGA046.
- **ESP board:** ESP32-C6 SuperMini.

I used an ESP32-C6, but a C6 is not required. Other ESPHome-supported ESP boards should work too, provided they have two usable UART RX channels. Adjust the board, framework, logger and RX pins in the configuration for your hardware; only the C6 has been tested here.

For other washers, check your installed panel and verify the electrical interface, packet format and decoded readings. Reports from other washers and ESP boards are welcome!

## Safety

> [!WARNING]
> **Before attempting this project, unplug the washing machine from the mains socket. Switching it off at the control panel is not sufficient.** Do not open the appliance or touch internal wiring while it is connected to mains power. Unplugging does not guarantee that internal capacitors have discharged; if you are not qualified to work safely on mains-powered appliances, do not proceed.

**Mains appliance: the receive interface, power arrangement and off-state behavior have not been qualified.** ESP32 GPIO is not 5 V tolerant.

Disconnect mains before changing wiring. **Disconnect every washer lead, including ground and power, before connecting USB for flashing or recovery.** Never attach USB, a PC or grounded test equipment while connected to the washer. Use Wi-Fi for logs and OTA. If you cannot establish a safe interface and enclosure, do not install this modification.

Samsung's [official English repair guide](https://downloadcenter.samsung.com/content/EU/202507/20250709163659233/Repair_Guide_Washer_English.pdf) does not qualify this modification.

## Setup

Follow the official [ESPHome installation guide](https://esphome.io/install/) and [Device Builder getting-started guide](https://esphome.io/install/getting-started/). Use ESPHome **2026.10** or newer.

[`samsung-washer.yaml`](samsung-washer.yaml) is a device configuration for the `samsung_washer` external component in this repository. Leave the C++ sources here; ESPHome loads them through `external_components`.

1. Copy [`samsung-washer.yaml`](samsung-washer.yaml) into your Device Builder configuration directory.
2. Add the entries from [`secrets.example.yaml`](secrets.example.yaml) to your private `secrets.yaml`. Generate an encryption key using the official [API documentation](https://esphome.io/components/api/#configuration-variables). Do not overwrite existing secrets or publish them.
3. Review the board configuration and RX substitutions. My tested wiring matches the ESP32-C6 defaults:

   | Samsung CN803 harness | Through 5 V→3.3 V level shifter (recommended) or tested divider to ESP |
   | --- | --- |
   | Pin 1 | GPIO1 (`uart_id`, decoded) |
   | Pin 2 | GPIO2 (`raw_uart_id`, raw capture only) |

   Both inputs use **9600 baud, 8N1**. Verify connector numbering on your board; do not connect the 5 V UART signals directly to ESP GPIO. Prefer a proper 5 V→3.3 V UART level shifter over the prototype resistor dividers. `raw_uart_id` is optional. Decoded readings come from pin 1.

   To compile from a checkout of this repository, point `external_components` at the local `components` directory. The comment in `samsung-washer.yaml` shows that source.
4. In Device Builder, **Validate**, then **Install**. First USB installation requires every washer lead disconnected. Later updates can use Wi-Fi OTA.
5. In Home Assistant, add the **ESPHome** integration with the monitor's address and encryption key. Compare readings with the panel before building automations.

## Android notifications

The [notification blueprint](home-assistant/blueprints/washer_notification.yaml) keeps one card: estimated progress and countdown while Running, static time while Paused, and **Wash finished** on Completed. Off or unknown/unavailable status clears it. Delay End shows a scheduled-finish label rather than a progress bar.

1. Copy it to `/config/blueprints/automation/samsung_washer/washer_notification.yaml`, then reload automations.
2. Create a dedicated **Number helper**: minimum `0`, maximum `172800`, step `1`, unit `s`; leave it at zero initially.
3. Create an automation from the blueprint. Select Cycle status, Displayed time and the helper; enter your Android `notify.mobile_app_…` action and a washer dashboard/device path.

Allow Companion app notifications on the phone. Progress is estimated from the first observed running time, not actual wash stages; reaching zero does not trigger completion. See the official [Android notification documentation](https://companion.home-assistant.io/docs/notifications/notifications-basic/).

## Contributing

Report the exact model/board, expected reading and actual panel behavior. For decoding issues, enable **UART raw capture**, save a short **Wi-Fi** log around one action, then turn capture off. Raw lines are tagged `samsung_washer.raw`. Review logs and photos before sharing; never include credentials, backups or compiled firmware. Keep the component receive-only.

The decoder has a host test. A capture file is optional:

```bash
g++ -std=c++17 -Wall -Wextra -Werror -I components/samsung_washer -o /tmp/test_parser tests/test_parser.cpp && /tmp/test_parser
```

## License

Original project code and configuration are available under the [MIT License](LICENSE). ESPHome and other dependencies retain their own licenses; this does not relicense them. Distributing compiled firmware also requires complying with the licenses of its included components; this project publishes source/configuration, not firmware binaries. Linked Samsung manuals and other third-party material are not covered by this license.

Not affiliated with Samsung.
