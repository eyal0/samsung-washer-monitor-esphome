import esphome.codegen as cg
from esphome.components import switch
import esphome.config_validation as cv
from esphome.const import CONF_ID, ENTITY_CATEGORY_DIAGNOSTIC
from esphome.types import ConfigType

from . import CONF_SAMSUNG_WASHER_ID, SamsungWasher, samsung_washer_ns

DEPENDENCIES = ["samsung_washer"]

CONF_RAW_CAPTURE = "raw_capture"

RawCaptureSwitch = samsung_washer_ns.class_(
    "RawCaptureSwitch",
    switch.Switch,
    cg.Component,
    cg.Parented.template(SamsungWasher),
)

CONFIG_SCHEMA = {
    cv.GenerateID(CONF_ID): cv.declare_id(cg.EntityBase),
    cv.GenerateID(CONF_SAMSUNG_WASHER_ID): cv.use_id(SamsungWasher),
    cv.Optional(CONF_RAW_CAPTURE): switch.switch_schema(
        RawCaptureSwitch,
        icon="mdi:code-braces",
        entity_category=ENTITY_CATEGORY_DIAGNOSTIC,
        default_restore_mode="ALWAYS_OFF",
    ),
}


async def to_code(config: ConfigType) -> None:
    if CONF_RAW_CAPTURE not in config:
        return
    hub = await cg.get_variable(config[CONF_SAMSUNG_WASHER_ID])
    var = await switch.new_switch(config[CONF_RAW_CAPTURE])
    await cg.register_component(var, config[CONF_RAW_CAPTURE])
    await cg.register_parented(var, hub)
    cg.add(hub.set_raw_capture_switch(var))
