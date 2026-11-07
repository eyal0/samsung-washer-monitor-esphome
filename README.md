# Samsung Washer Monitor for ESPHome

Local ESPHome monitoring for my **Samsung WW11BGA046**, which uses the **DC92-01950B control/display panel**. An ESP listens to the panel/main-board UART. No Samsung cloud account, no replacement of the original washer firmware.

The [DC92-01950B parts catalog](https://www.sos-parts.com/display-card-original-control-unit-samsung-dc92-01950b-133080.html) lists this panel in **80 regional washer variants**, so this may also work with other machines using the same panel. Only my machine has been tested so far.

A second frame format, from a WF45R6100AP/US with the **DC92-02395A** board, is selected with `model`. Its captured layout is in [`docs/debugging.txt`](docs/debugging.txt). Temperature, rinse, spin, soil, displayed time, the door lock and the program are decoded. Cycle status and the other lamp indicators are not.

**Receive-only:** this cannot start, stop or change a wash remotely.

**Experimental DIY project:** tested on one washer, not a qualified or plug-and-play retrofit. Read [Safety](#safety) before attempting any hardware work.

## What you get

![Home Assistant device-page Sensors card showing Cotton, 40 °C, 800 rpm, two rinses and 1:05 displayed time](docs/images/home-assistant-demo.webp)

The actual Home Assistant device-page **Sensors** card, with the entity icons configured in this project. **Demo values:** selections and indicators replayed from my washer's recorded history; cycle status is simulated, not a live reading.

- Program, temperature, spin, rinse and option selections, plus Bubble Soak and Easy Iron indicators.
- Displayed time, including Delay End.
- **Cycle status:** `Running`, `Paused`, `Completed`, `Off`, `Unknown`.
- Panel-on and door-lock indicators, a UART-link diagnostic, plus an optional raw-capture switch.
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
> **Not recommended:** the resistor voltage dividers in the photo above and the diagram below are just my prototype hack. Use a proper **5 V→3.3 V UART level shifter** instead.

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
- **Also decoded from captures:** Samsung **DC92-02395A** on a WF45R6100AP/US. Pin 1 frames are 33 bytes at 38400 baud. Use [`washer-dc92-02395a.yaml`](firmware/washer-dc92-02395a.yaml).
- **ESP board:** ESP32-C6 SuperMini.

I used an ESP32-C6, but a C6 is not required. Other ESPHome-supported ESP boards should work too, provided they have one usable UART RX channel for the readings (two if you also want CN803 pin 2 raw diagnostics, as in the example configuration). Adjust the board, framework, logger and RX pins in the configuration for your hardware; only the C6 has been tested here.

For other washers, check your installed panel and verify the electrical interface, packet format and decoded readings. Reports from other washers and ESP boards are welcome!

## Safety

> [!WARNING]
> **Before attempting this project, unplug the washing machine from the mains socket. Switching it off at the control panel is not sufficient.** Do not open the appliance or touch internal wiring while it is connected to mains power. Unplugging does not guarantee that internal capacitors have discharged; if you are not qualified to work safely on mains-powered appliances, do not proceed.

**Mains appliance: the receive interface, power arrangement and off-state behavior have not been qualified.** ESP32 GPIO is not 5 V tolerant.

Disconnect mains before changing wiring. **Disconnect every washer lead, including ground and power, before connecting USB for flashing or recovery.** Never attach USB, a PC or grounded test equipment while connected to the washer. Use the network (Wi-Fi or Thread) for logs and OTA. If you cannot establish a safe interface and enclosure, do not install this modification.

Samsung's [official English repair guide](https://downloadcenter.samsung.com/content/EU/202507/20250709163659233/Repair_Guide_Washer_English.pdf) does not qualify this modification.

## Setup

Follow the official [ESPHome installation guide](https://esphome.io/install/) and [Device Builder getting-started guide](https://esphome.io/install/getting-started/).

1. Copy [`washer.yaml`](firmware/washer.yaml) into your Device Builder configuration directory (or add the `external_components`, `uart` and `samsung_washer` blocks to an existing ESPHome YAML). For the WF45R6100AP/US board, copy [`washer-dc92-02395a.yaml`](firmware/washer-dc92-02395a.yaml) instead. ESPHome fetches the [`samsung_washer`](components/samsung_washer/) external component from this repository automatically.
2. Add the entries from [`secrets.example.yaml`](firmware/secrets.example.yaml) to your private `secrets.yaml`. Generate an encryption key using the official [API documentation](https://esphome.io/components/api/#configuration-variables). Do not overwrite existing secrets or publish them.
3. Review the board configuration and RX substitutions. My tested wiring matches the ESP32-C6 defaults:

   | Samsung CN803 harness | Through 5 V→3.3 V level shifter (recommended) or tested divider to ESP |
   | --- | --- |
   | Pin 1 | GPIO1 |
   | Pin 2 | GPIO2 |

   Both inputs use **8N1**. **DC92-01950B** uses **9600 baud**. **DC92-02395A** uses **38400 baud**. Verify connector numbering on your board; do not connect the 5 V UART signals directly to ESP GPIO. Prefer a proper 5 V→3.3 V UART level shifter over the prototype resistor dividers.
4. In Device Builder, **Validate**, then **Install**. First USB installation requires every washer lead disconnected. Later updates can use network OTA (Wi-Fi or Thread).
5. In Home Assistant, add the **ESPHome** integration with the monitor's address and encryption key. Compare readings with the panel before building automations.

### Component configuration

The [`washer.yaml`](firmware/washer.yaml) example contains all supported entities for the DC92-01950B board. [`washer-dc92-02395a.yaml`](firmware/washer-dc92-02395a.yaml) is the same wiring with the entities that the DC92-02395A board decodes. Under `samsung_washer`:

- `model` selects the control board. `DC92-01950B` (the default) is the 32-byte format. `DC92-02395A` is the 33-byte format from the WF45R6100AP/US. `model` does not change the baud rate; set that on the UART blocks (9600 or 38400). `dc92_02395a` is accepted as well.
- `uart_id` selects the **CN803 pin 1** UART: 8 data bits, no parity, 1 stop bit. This stream supplies all readings.
- `secondary_uart_id` optionally selects the **CN803 pin 2** UART for raw diagnostics. Omit it and its UART block if you only want the decoded readings. The two UART IDs must be different and cannot be shared with another reader. Do not enable UART debug `dummy_receiver`; the component reads the bytes itself.
- Every entity block is optional. Standard ESPHome entity options such as `id`, `icon`, filters and automations remain available. Tracking does not depend on which entities you expose.
- `uart_raw_capture` adds the diagnostic switch; it defaults to on after reboot. While on, it logs raw bytes as hex under the `washer.raw` log tag, including CRC-invalid frames, without transmitting anything. Each `5A A5` header starts a new row, bytes outside a row starting with `5A A5` are not shown, and a row identical to the previous logged row is not logged. On `DC92-01950B` this is pin 1 and, if `secondary_uart_id` is set, pin 2, trimmed to the first 33 bytes. On `DC92-02395A` only pin 1 is logged, and only a row of exactly 33 bytes whose byte 24 is `00`.
- `soil_selection` is filled only for `DC92-02395A`. On that board, temperature is `Cold`, `2`, `3`, `4` or `Hot` (one light through all five, cold to hot). Rinse is the light count `0` to `5`. Spin is `0`, `No Spin`, `2`, `3`, `4`, or `High`. Soil is `0`, `Light`, `2`, `3`, `4`, or `Heavy`. A pattern that is not a solid bar is `Unknown`. Displayed time uses the same `H:MM` text as the other board. Its hour digit is byte 11, minute tens are byte 13, and minute ones are byte 15; bits 0 through 6 are segments a through g, and a blank hour or tens digit is zero. Cycle status stays `Unknown`. The door lock is bit 1 of byte 17. Program is one lit dial light: `Normal`, `Heavy Duty`, `Whites`, `Bedding`, `Sanitize`, `Rinse & Spin`, `Perm Press`, `Delicates`, `Quick Wash`, or `Self Clean`. While a program is running that light blinks, and a dark frame keeps the previous program. Option, panel, bubble soak and easy iron are not decoded, so the example leaves them out.
- `update_interval` defaults to `2s` and sets how often the UART link is checked. Five seconds without a valid pin 1 frame marks the link inactive and resets cycle status to Unknown; a blank panel must persist for two seconds before reporting Off. The blank-panel timing applies to `DC92-01950B`.

No parser header or YAML lambdas need to be copied. When updating an existing installation, remove the old `includes: washer_parser.h`, washer globals/interval, template entities and UART debug lambdas before adding the new component blocks. The example keeps the old entity names, so Home Assistant entity IDs stay the same; don't rename them if you want to keep your history and automations. The old template entities' ESPHome `id` values (such as `cycle_status` or `packet_diagnostics`) are not set by default; if your own YAML refers to them, add the `id` to the matching entity block.

### Thread instead of Wi-Fi

The ESP32-C6 also has a Thread (802.15.4) radio. [`washer-thread.yaml`](firmware/washer-thread.yaml) is the same configuration with Wi-Fi replaced by ESPHome's [OpenThread component](https://esphome.io/components/openthread/). ESPHome cannot run Wi-Fi and Thread together on one ESP, so the API, logs and OTA updates all go over Thread. It needs an ESP with an 802.15.4 radio (ESP32-C6, C5 or H2) and a Thread border router that Home Assistant can reach.

1. Get your Thread network's active dataset as a hex TLV string. In Home Assistant, go to **Settings → Devices & services → Thread**, open your preferred network, and use its dataset/TLV option (this needs a border router that Home Assistant manages, such as the OpenThread Border Router add-on). Alternatively, run `ot-ctl dataset active -x` on an OpenThread border router.
2. Copy [`secrets-thread.example.yaml`](firmware/secrets-thread.example.yaml) to `secrets-thread.yaml` next to `washer-thread.yaml`, and put the TLV in it. Your existing `secrets.yaml` still provides the API key and OTA password; its Wi-Fi entries are unused. The dataset contains your Thread network key, so keep this file private.

   ESPHome's `!secret` only reads `secrets.yaml`, so this file is loaded with `!include` instead. As a result, ESPHome does not redact it like other secrets: `esphome config` prints the TLV, so don't share that output. Device Builder also lists `secrets-thread.yaml` as a device card; ignore that card and don't install it.
3. Validate and install `washer-thread.yaml` instead of `washer.yaml`. Then add the device in Home Assistant through the ESPHome integration, if it is not discovered automatically.

The device joins as a full Thread device (`FTD`), so it can also act as a router for your other Thread devices, since it is mains-powered.

> [!WARNING]
> Switching an installed monitor from Wi-Fi to Thread happens over the last Wi-Fi OTA update. If the Thread dataset is wrong or Home Assistant cannot reach the Thread network, the monitor drops off the network, and recovery needs USB flashing, which means disconnecting every washer lead first. Check the dataset before installing. Home Assistant also needs an IPv6 route to the Thread network, which your border router normally advertises on your LAN.

## Android notifications

The [notification blueprint](home-assistant/blueprints/washer_notification.yaml) keeps one card: estimated progress and countdown while Running, static time while Paused, and **Wash finished** on Completed. Off or unknown/unavailable status clears it. Delay End shows a scheduled-finish label rather than a progress bar.

1. Copy it to `/config/blueprints/automation/samsung_washer/washer_notification.yaml`, then reload automations.
2. Create a dedicated **Number helper**: minimum `0`, maximum `172800`, step `1`, unit `s`; leave it at zero initially.
3. Create an automation from the blueprint. Select Cycle status, Displayed time and the helper; enter your Android `notify.mobile_app_…` action and a washer dashboard/device path.

Allow Companion app notifications on the phone. Progress is estimated from the first observed running time, not actual wash stages; reaching zero does not trigger completion. See the official [Android notification documentation](https://companion.home-assistant.io/docs/notifications/notifications-basic/).

## Contributing

Report the exact model/board, expected reading and actual panel behavior. For decoding issues, turn on the **UART raw capture** switch (configured with `uart_raw_capture`), save a short **network** (Wi-Fi or Thread, not USB) log around one action, then turn capture off. Review logs and photos before sharing; never include credentials, backups or compiled firmware. Keep firmware receive-only.

### Development checks

Run from this repository's root, with a C++ compiler and ESPHome installed in the Python environment that runs the tests:

```sh
python -m unittest discover -s tests -p 'test_*.py'
g++ -std=c++17 -Wall -Wextra -pedantic firmware/test_parser.cpp -o /tmp/washer-parser-test
/tmp/washer-parser-test /dev/null 32 0
esphome compile tests/component.yaml
tests/.esphome/build/washer-component-test/.pioenvs/washer-component-test/program
```

The runtime tests build against ESPHome's actual entities and filters on a Linux/macOS host, using in-memory UARTs, not the washer. They cover decoding, cycle tracking, optional/filtered entities, link loss, switch restoration and receive-only behavior. The parser test also accepts a binary capture, frame length and expected valid-frame count as arguments.

To compile `firmware/washer.yaml` against a local checkout before publishing, replace its external component source with:

```yaml
external_components:
  - source:
      type: local
      path: ../components
    components: [samsung_washer]
```

## License

Original project code and configuration are available under the [MIT License](LICENSE). ESPHome and other dependencies retain their own licenses; this does not relicense them. Distributing compiled firmware also requires complying with the licenses of its included components; this project publishes source/configuration, not firmware binaries. Linked Samsung manuals and other third-party material are not covered by this license.

Not affiliated with Samsung.
