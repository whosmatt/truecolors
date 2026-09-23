idf_component_register(
    SRCS "audio.c" "frontend.c" "fe_ctx.c" "tempo.c" "beattrack.c" "musicgate.c"
    INCLUDE_DIRS "include"
    REQUIRES common
    PRIV_REQUIRES esp_driver_i2s
)
