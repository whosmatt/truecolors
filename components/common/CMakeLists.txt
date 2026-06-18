idf_component_register(
    SRCS "app_bus.c"
    INCLUDE_DIRS "include"
    # esp_event: app bus loop. esp_driver_gpio: public board_pins.h includes
    # driver/gpio.h (the umbrella "driver" no longer provides it transitively).
    REQUIRES esp_event esp_driver_gpio
)
