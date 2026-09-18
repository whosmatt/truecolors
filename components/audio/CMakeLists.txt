idf_component_register(
    SRCS "audio.c" "frontend.c" "kick.c" "snare.c" "beatgrid.c"
    INCLUDE_DIRS "include"
    REQUIRES common
    PRIV_REQUIRES esp_driver_i2s
)
