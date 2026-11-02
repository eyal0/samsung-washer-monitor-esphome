import esphome.codegen as cg
from esphome.components import uart
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_UART_ID
from esphome.types import ConfigType

DEPENDENCIES = ["uart"]

samsung_washer_ns = cg.esphome_ns.namespace("samsung_washer")
SamsungWasher = samsung_washer_ns.class_("SamsungWasher", cg.Component, uart.UARTDevice)

CONF_SAMSUNG_WASHER_ID = "samsung_washer_id"
CONF_RAW_UART_ID = "raw_uart_id"

CONFIG_SCHEMA = (
    cv.Schema(
        {
            cv.GenerateID(): cv.declare_id(SamsungWasher),
            cv.Optional(CONF_RAW_UART_ID): cv.use_id(uart.UARTComponent),
        }
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(uart.UART_DEVICE_SCHEMA)
)


def _validate_uarts(config: ConfigType) -> ConfigType:
    # Compare ids before final validation replaces uart_id with the UART config.
    if CONF_RAW_UART_ID in config and str(config[CONF_RAW_UART_ID]) == str(
        config[CONF_UART_ID]
    ):
        raise cv.Invalid(
            "raw_uart_id must reference a different UART than uart_id",
            path=[CONF_RAW_UART_ID],
        )
    config = uart.final_validate_device_schema(
        "samsung_washer",
        baud_rate=9600,
        require_rx=True,
        data_bits=8,
        parity="NONE",
        stop_bits=1,
    )(config)
    if CONF_RAW_UART_ID not in config:
        return config
    return uart.final_validate_device_schema(
        "samsung_washer",
        uart_bus=CONF_RAW_UART_ID,
        baud_rate=9600,
        require_rx=True,
        data_bits=8,
        parity="NONE",
        stop_bits=1,
    )(config)


FINAL_VALIDATE_SCHEMA = _validate_uarts


async def to_code(config: ConfigType) -> None:
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)
    if CONF_RAW_UART_ID in config:
        raw = await cg.get_variable(config[CONF_RAW_UART_ID])
        cg.add(var.set_raw_uart(raw))
