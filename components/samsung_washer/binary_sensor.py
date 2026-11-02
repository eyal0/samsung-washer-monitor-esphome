import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC
from esphome.types import ConfigType

from . import CONF_SAMSUNG_WASHER_ID, SamsungWasher

DEPENDENCIES = ["samsung_washer"]

CONF_PANEL_ON = "panel_on"
CONF_UART_LINK = "uart_link"
CONF_BUBBLE_SOAK = "bubble_soak"
CONF_EASY_IRON = "easy_iron"
CONF_DOOR_LOCK = "door_lock"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_SAMSUNG_WASHER_ID): cv.use_id(SamsungWasher),
    cv.Optional(CONF_PANEL_ON): binary_sensor.binary_sensor_schema(
        icon="mdi:power",
    ),
    cv.Optional(CONF_UART_LINK): binary_sensor.binary_sensor_schema(
        icon="mdi:lan-connect",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
    cv.Optional(CONF_BUBBLE_SOAK): binary_sensor.binary_sensor_schema(
        icon="mdi:chart-bubble",
    ),
    cv.Optional(CONF_EASY_IRON): binary_sensor.binary_sensor_schema(
        icon="mdi:iron-outline",
    ),
    cv.Optional(CONF_DOOR_LOCK): binary_sensor.binary_sensor_schema(
        icon="mdi:lock",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
    ),
}


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_SAMSUNG_WASHER_ID])
    sensors = binary_sensor.sub_binary_sensors(config)
    await sensors(CONF_PANEL_ON, hub.set_panel_on_binary_sensor)
    await sensors(CONF_UART_LINK, hub.set_uart_link_binary_sensor)
    await sensors(CONF_BUBBLE_SOAK, hub.set_bubble_soak_binary_sensor)
    await sensors(CONF_EASY_IRON, hub.set_easy_iron_binary_sensor)
    await sensors(CONF_DOOR_LOCK, hub.set_door_lock_binary_sensor)
