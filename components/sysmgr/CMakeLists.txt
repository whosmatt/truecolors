idf_component_register(
    SRCS "sysmgr.c" "fancontrol.c" "status_led.c" "sysmgr_safety.c"
    INCLUDE_DIRS "include"
    REQUIRES common appstate laser led_strip
    PRIV_REQUIRES wifi audio esp_driver_i2c esp_adc esp_driver_pcnt esp_driver_ledc esp_driver_tsens esp_driver_gpio esp_timer
)
