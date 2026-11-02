import esphome.codegen as cg
from esphome.components import binary_sensor, switch, text_sensor, uart
import esphome.config_validation as cv
import esphome.final_validate as fv
from esphome.const import (
    CONF_DEBUG,
    CONF_DUMMY_RECEIVER,
    CONF_ID,
    CONF_UART_ID,
    ENTITY_CATEGORY_DIAGNOSTIC,
)
from esphome.types import ConfigType

CODEOWNERS = ["@kralonur"]
DEPENDENCIES = ["uart"]
AUTO_LOAD = ["binary_sensor", "switch", "text_sensor"]
MULTI_CONF = True

samsung_washer_ns = cg.esphome_ns.namespace("samsung_washer")
SamsungWasherComponent = samsung_washer_ns.class_(
    "SamsungWasherComponent", cg.PollingComponent, uart.UARTDevice
)
SamsungWasherRawCaptureSwitch = samsung_washer_ns.class_(
    "SamsungWasherRawCaptureSwitch", switch.Switch, cg.Component
)

CONF_SECONDARY_UART_ID = "secondary_uart_id"

CONF_CYCLE_STATUS = "cycle_status"
CONF_PROGRAM_INDICATOR = "program_indicator"
CONF_TEMPERATURE_SELECTION = "temperature_selection"
CONF_SPIN_SELECTION = "spin_selection"
CONF_RINSE_SELECTION = "rinse_selection"
CONF_DISPLAYED_TIME = "displayed_time"
CONF_OPTION = "option"

CONF_PANEL_ON = "panel_on"
CONF_UART_LINK_ACTIVE = "uart_link_active"
CONF_BUBBLE_SOAK_INDICATOR = "bubble_soak_indicator"
CONF_EASY_IRON_INDICATOR = "easy_iron_indicator"
CONF_DOOR_LOCK_INDICATOR = "door_lock_indicator"

CONF_UART_RAW_CAPTURE = "uart_raw_capture"

TEXT_SENSORS = (
    (CONF_CYCLE_STATUS, "set_cycle_status_text_sensor"),
    (CONF_PROGRAM_INDICATOR, "set_program_indicator_text_sensor"),
    (CONF_TEMPERATURE_SELECTION, "set_temperature_selection_text_sensor"),
    (CONF_SPIN_SELECTION, "set_spin_selection_text_sensor"),
    (CONF_RINSE_SELECTION, "set_rinse_selection_text_sensor"),
    (CONF_DISPLAYED_TIME, "set_displayed_time_text_sensor"),
    (CONF_OPTION, "set_option_selection_text_sensor"),
)

BINARY_SENSORS = (
    (CONF_PANEL_ON, "set_panel_on_binary_sensor"),
    (CONF_UART_LINK_ACTIVE, "set_link_active_binary_sensor"),
    (CONF_BUBBLE_SOAK_INDICATOR, "set_bubble_indicator_binary_sensor"),
    (CONF_EASY_IRON_INDICATOR, "set_iron_indicator_binary_sensor"),
    (CONF_DOOR_LOCK_INDICATOR, "set_lock_indicator_binary_sensor"),
)

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SamsungWasherComponent),
            cv.Optional(CONF_SECONDARY_UART_ID): cv.use_id(uart.UARTComponent),
            cv.Optional(CONF_CYCLE_STATUS): text_sensor.text_sensor_schema(
                icon="mdi:washing-machine"
            ),
            cv.Optional(CONF_PROGRAM_INDICATOR): text_sensor.text_sensor_schema(
                icon="mdi:washing-machine"
            ),
            cv.Optional(CONF_TEMPERATURE_SELECTION): text_sensor.text_sensor_schema(
                icon="mdi:thermometer"
            ),
            cv.Optional(CONF_SPIN_SELECTION): text_sensor.text_sensor_schema(
                icon="mdi:rotate-right"
            ),
            cv.Optional(CONF_RINSE_SELECTION): text_sensor.text_sensor_schema(
                icon="mdi:water"
            ),
            cv.Optional(CONF_DISPLAYED_TIME): text_sensor.text_sensor_schema(
                icon="mdi:timer-outline"
            ),
            cv.Optional(CONF_OPTION): text_sensor.text_sensor_schema(
                icon="mdi:tune-variant"
            ),
            cv.Optional(CONF_PANEL_ON): binary_sensor.binary_sensor_schema(
                icon="mdi:power"
            ),
            cv.Optional(CONF_UART_LINK_ACTIVE): binary_sensor.binary_sensor_schema(
                icon="mdi:lan-connect",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
            cv.Optional(CONF_BUBBLE_SOAK_INDICATOR): binary_sensor.binary_sensor_schema(
                icon="mdi:chart-bubble"
            ),
            cv.Optional(CONF_EASY_IRON_INDICATOR): binary_sensor.binary_sensor_schema(
                icon="mdi:iron-outline"
            ),
            cv.Optional(CONF_DOOR_LOCK_INDICATOR): binary_sensor.binary_sensor_schema(
                icon="mdi:lock",
                entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
            ),
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

    for key, setter in TEXT_SENSORS:
        if conf := config.get(key):
            sens = await text_sensor.new_text_sensor(conf)
            cg.add(getattr(var, setter)(sens))

    for key, setter in BINARY_SENSORS:
        if conf := config.get(key):
            sens = await binary_sensor.new_binary_sensor(conf)
            cg.add(getattr(var, setter)(sens))

    if conf := config.get(CONF_UART_RAW_CAPTURE):
        sw = await switch.new_switch(conf)
        await cg.register_component(sw, conf)
        cg.add(var.set_raw_capture_switch(sw))
