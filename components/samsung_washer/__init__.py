import esphome.codegen as cg
from esphome.components import binary_sensor, number, select, sensor, switch, text_sensor, time, uart
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import (
    CONF_DEBUG,
    CONF_DUMMY_RECEIVER,
    CONF_ID,
    CONF_TIME_ID,
    CONF_UART_ID,
    DEVICE_CLASS_DURATION,
    DEVICE_CLASS_POWER,
    DEVICE_CLASS_RUNNING,
    DEVICE_CLASS_TIMESTAMP,
    ENTITY_CATEGORY_DIAGNOSTIC,
    STATE_CLASS_MEASUREMENT,
    UNIT_HOUR,
    UNIT_MINUTE,
)
from esphome.types import ConfigType

CODEOWNERS = ["@kralonur"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "number", "select", "sensor", "switch", "text_sensor"]
MULTI_CONF = True

samsung_washer_ns = cg.esphome_ns.namespace("samsung_washer")
SamsungWasherComponent = samsung_washer_ns.class_(
    "SamsungWasherComponent", cg.PollingComponent, uart.UARTDevice
)
SamsungWasherRawCaptureSwitch = samsung_washer_ns.class_(
    "SamsungWasherRawCaptureSwitch", switch.Switch, cg.Component
)
PanelSelect = samsung_washer_ns.class_("PanelSelect", select.Select)
PanelSwitch = samsung_washer_ns.class_("PanelSwitch", switch.Switch)
PanelNumber = samsung_washer_ns.class_("PanelNumber", number.Number)

CONF_SECONDARY_UART_ID = "secondary_uart_id"

# Names and Home Assistant types follow LocalThings where the panel reports the same thing.
# Option lists are the English labels LocalThings shows. Keep them in sync with samsung_washer.cpp.
CONF_CYCLE = "cycle"
CONF_WASH_TEMPERATURE = "wash_temperature"
CONF_SPIN_SPEED = "spin_speed"
CONF_RINSE_CYCLES = "rinse_cycles"
CYCLE_OPTIONS = [
    "15' Quick Wash",
    "Mixed Load",
    "Delicates",
    "Wool",
    "Intense Cold",
    "Colors",
    "Bedding",
    "Eco 40-60",
    "Cotton",
    "Synthetics",
    "Hygiene Steam",
    "Rinse+Spin",
    "Drain/Spin",
    "Drum Clean",
]
TEMPERATURE_OPTIONS = ["None", "20", "30", "40", "60", "90", "40 and 60 indicators", "Unknown"]
SPIN_OPTIONS = ["Rinse hold", "No spin", "400", "800", "1200", "1400", "Unknown"]
RINSE_OPTIONS = ["0", "1", "2", "3", "4", "5", "Unknown"]
SELECTS = (
    (CONF_CYCLE, "set_cycle_select", "mdi:washing-machine", CYCLE_OPTIONS),
    (CONF_WASH_TEMPERATURE, "set_wash_temperature_select", "mdi:thermometer-water", TEMPERATURE_OPTIONS),
    (CONF_SPIN_SPEED, "set_spin_speed_select", "mdi:sync", SPIN_OPTIONS),
    (CONF_RINSE_CYCLES, "set_rinse_cycles_select", "mdi:water-sync", RINSE_OPTIONS),
)

CONF_MACHINE_STATE = "machine_state"
CONF_PROGRESS = "progress"
TEXT_SENSORS = (
    (CONF_MACHINE_STATE, "set_machine_state_text_sensor", "mdi:washing-machine"),
    (CONF_PROGRESS, "set_progress_text_sensor", "mdi:progress-wrench"),
)

CONF_RUNNING = "running"
CONF_POWER = "power"
CONF_UART_LINK_ACTIVE = "uart_link_active"
CONF_EASY_IRON_INDICATOR = "easy_iron_indicator"
CONF_DOOR_LOCK_INDICATOR = "door_lock_indicator"
BINARY_SENSORS = (
    (CONF_RUNNING, "set_running_binary_sensor", DEVICE_CLASS_RUNNING, None, None),
    (CONF_POWER, "set_power_binary_sensor", DEVICE_CLASS_POWER, None, None),
    (
        CONF_UART_LINK_ACTIVE,
        "set_link_active_binary_sensor",
        None,
        "mdi:lan-connect",
        ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    (CONF_EASY_IRON_INDICATOR, "set_iron_indicator_binary_sensor", None, "mdi:iron-outline", None),
    (
        CONF_DOOR_LOCK_INDICATOR,
        "set_lock_indicator_binary_sensor",
        None,
        "mdi:lock",
        ENTITY_CATEGORY_DIAGNOSTIC,
    ),
)

CONF_COMPLETION_TIME = "completion_time"
CONF_ESTIMATED_FINISH = "estimated_finish"
CONF_DELAY_START = "delay_start"

CONF_BUBBLE_SOAK = "bubble_soak"
CONF_PRE_WASH = "pre_wash"
CONF_INTENSIVE = "intensive"
CONF_EXTRA_RINSE = "extra_rinse"
PANEL_SWITCHES = (
    (CONF_BUBBLE_SOAK, "set_bubble_soak_switch", "mdi:chart-bubble"),
    (CONF_PRE_WASH, "set_pre_wash_switch", "mdi:washing-machine"),
    (CONF_INTENSIVE, "set_intensive_switch", "mdi:washing-machine"),
    (CONF_EXTRA_RINSE, "set_extra_rinse_switch", "mdi:water-plus"),
)

CONF_UART_RAW_CAPTURE = "uart_raw_capture"


def _binary_schema(device_class, icon, entity_category):
    kwargs = {}
    if device_class is not None:
        kwargs["device_class"] = device_class
    if icon is not None:
        kwargs["icon"] = icon
    if entity_category is not None:
        kwargs["entity_category"] = entity_category
    return binary_sensor.binary_sensor_schema(**kwargs)


CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SamsungWasherComponent),
            cv.Optional(CONF_SECONDARY_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_TIME_ID): cv.use_id(time.RealTimeClock),
            **{
                cv.Optional(key): select.select_schema(PanelSelect, icon=icon)
                for key, _setter, icon, _options in SELECTS
            },
            **{
                cv.Optional(key): text_sensor.text_sensor_schema(icon=icon)
                for key, _setter, icon in TEXT_SENSORS
            },
            **{
                cv.Optional(key): _binary_schema(device_class, icon, entity_category)
                for key, _setter, device_class, icon, entity_category in BINARY_SENSORS
            },
            cv.Optional(CONF_COMPLETION_TIME): sensor.sensor_schema(
                unit_of_measurement=UNIT_MINUTE,
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_DURATION,
                state_class=STATE_CLASS_MEASUREMENT,
                icon="mdi:clock-outline",
            ),
            cv.Optional(CONF_ESTIMATED_FINISH): sensor.sensor_schema(
                accuracy_decimals=0,
                device_class=DEVICE_CLASS_TIMESTAMP,
                icon="mdi:clock-end",
            ),
            cv.Optional(CONF_DELAY_START): number.number_schema(
                PanelNumber,
                unit_of_measurement=UNIT_HOUR,
                device_class=DEVICE_CLASS_DURATION,
                icon="mdi:timer-plus-outline",
            ),
            **{
                cv.Optional(key): switch.switch_schema(
                    PanelSwitch, icon=icon, default_restore_mode="DISABLED"
                )
                for key, _setter, icon in PANEL_SWITCHES
            },
            cv.Optional(CONF_UART_RAW_CAPTURE): switch.switch_schema(
                SamsungWasherRawCaptureSwitch,
                icon="mdi:code-braces",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
                default_restore_mode="ALWAYS_OFF",
            ).extend(cv.COMPONENT_SCHEMA),
        }
    )
    .extend(cv.polling_component_schema("2s"))
    .extend(uart.UART_DEVICE_SCHEMA)
)

_PRIMARY_UART_VALIDATE = uart.final_validate_device_schema(
    "samsung_washer",
    uart_bus=CONF_UART_ID,
    baud_rate=9600,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)

_SECONDARY_UART_VALIDATE = uart.final_validate_device_schema(
    "samsung_washer",
    uart_bus=CONF_SECONDARY_UART_ID,
    baud_rate=9600,
    require_rx=True,
    data_bits=8,
    parity="NONE",
    stop_bits=1,
)


def _validate_uart_debug(config: ConfigType) -> ConfigType:
    if config.get(CONF_DEBUG, {}).get(CONF_DUMMY_RECEIVER, False):
        raise cv.Invalid(
            "samsung_washer reads the UART itself; set debug dummy_receiver to false "
            "so it cannot consume the washer's data."
        )
    return config


def _final_validate(config: ConfigType) -> ConfigType:
    _PRIMARY_UART_VALIDATE(config)
    if CONF_SECONDARY_UART_ID in config:
        _SECONDARY_UART_VALIDATE(config)
    cv.Schema(
        {
            cv.Required(CONF_UART_ID): fv.id_declaration_match_schema(_validate_uart_debug),
            cv.Optional(CONF_SECONDARY_UART_ID): fv.id_declaration_match_schema(_validate_uart_debug),
        },
        extra=cv.ALLOW_EXTRA,
    )(config)
    return config


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    if secondary_uart_id := config.get(CONF_SECONDARY_UART_ID):
        secondary_uart = await cg.get_variable(secondary_uart_id)
        cg.add(var.set_secondary_uart(secondary_uart))

    if time_id := config.get(CONF_TIME_ID):
        cg.add_define("USE_SAMSUNG_WASHER_TIME")
        cg.add(var.set_time(await cg.get_variable(time_id)))

    for key, setter, _icon, options in SELECTS:
        if conf := config.get(key):
            cg.add(getattr(var, setter)(await select.new_select(conf, options=options)))

    for key, setter, _icon in TEXT_SENSORS:
        if conf := config.get(key):
            cg.add(getattr(var, setter)(await text_sensor.new_text_sensor(conf)))

    for key, setter, _device_class, _icon, _category in BINARY_SENSORS:
        if conf := config.get(key):
            cg.add(getattr(var, setter)(await binary_sensor.new_binary_sensor(conf)))

    if conf := config.get(CONF_COMPLETION_TIME):
        cg.add(var.set_completion_time_sensor(await sensor.new_sensor(conf)))
    if conf := config.get(CONF_ESTIMATED_FINISH):
        cg.add(var.set_estimated_finish_sensor(await sensor.new_sensor(conf)))
    if conf := config.get(CONF_DELAY_START):
        cg.add(
            var.set_delay_start_number(
                await number.new_number(conf, min_value=0, max_value=24, step=1)
            )
        )

    for key, setter, _icon in PANEL_SWITCHES:
        if conf := config.get(key):
            cg.add(getattr(var, setter)(await switch.new_switch(conf)))

    if conf := config.get(CONF_UART_RAW_CAPTURE):
        sw = await switch.new_switch(conf)
        await cg.register_component(sw, conf)
        cg.add(var.set_raw_capture_switch(sw))
