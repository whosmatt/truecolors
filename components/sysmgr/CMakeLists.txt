idf_component_register(
    SRCS "sysmgr.c" "fancontrol.c" "status_led.c"
    INCLUDE_DIRS "include"
    REQUIRES common appstate laser led_strip
    PRIV_REQUIRES driver esp_adc esp_driver_i2c
)
