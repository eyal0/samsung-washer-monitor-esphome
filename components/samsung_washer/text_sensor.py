import esphome.codegen as cg
from esphome.components import text_sensor
import esphome.config_validation as cv
from esphome.const import CONF_ID
from esphome.types import ConfigType

from . import CONF_SAMSUNG_WASHER_ID, SamsungWasher

DEPENDENCIES = ["samsung_washer"]

CONF_CYCLE_STATUS = "cycle_status"
CONF_PROGRAM = "program"
CONF_TEMPERATURE = "temperature"
CONF_SPIN = "spin"
CONF_RINSE = "rinse"
CONF_DISPLAYED_TIME = "displayed_time"
CONF_OPTION = "option"

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_SAMSUNG_WASHER_ID): cv.use_id(SamsungWasher),
    cv.Optional(CONF_CYCLE_STATUS): text_sensor.text_sensor_schema(
        icon="mdi:washing-machine",
    ),
    cv.Optional(CONF_PROGRAM): text_sensor.text_sensor_schema(
        icon="mdi:washing-machine",
    ),
    cv.Optional(CONF_TEMPERATURE): text_sensor.text_sensor_schema(
        icon="mdi:thermometer",
    ),
    cv.Optional(CONF_SPIN): text_sensor.text_sensor_schema(
        icon="mdi:rotate-right",
    ),
    cv.Optional(CONF_RINSE): text_sensor.text_sensor_schema(
        icon="mdi:water",
    ),
    cv.Optional(CONF_DISPLAYED_TIME): text_sensor.text_sensor_schema(
        icon="mdi:timer-outline",
    ),
    cv.Optional(CONF_OPTION): text_sensor.text_sensor_schema(
        icon="mdi:tune-variant",
    ),
}


async def to_code(config: ConfigType) -> None:
    hub = await cg.get_variable(config[CONF_SAMSUNG_WASHER_ID])
    sensors = text_sensor.sub_text_sensors(config)
    await sensors(CONF_CYCLE_STATUS, hub.set_cycle_status_text_sensor)
    await sensors(CONF_PROGRAM, hub.set_program_text_sensor)
    await sensors(CONF_TEMPERATURE, hub.set_temperature_text_sensor)
    await sensors(CONF_SPIN, hub.set_spin_text_sensor)
    await sensors(CONF_RINSE, hub.set_rinse_text_sensor)
    await sensors(CONF_DISPLAYED_TIME, hub.set_displayed_time_text_sensor)
    await sensors(CONF_OPTION, hub.set_option_text_sensor)
