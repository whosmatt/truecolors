idf_component_register(
    SRCS "laser.c" "laser_math.c"
    INCLUDE_DIRS "include"
    REQUIRES common esp_driver_mcpwm
)
