idf_component_register(
    SRCS "audio.c" "kick.c" "beatgrid.c"
    INCLUDE_DIRS "include"
    REQUIRES common
    PRIV_REQUIRES esp_driver_i2s
)
