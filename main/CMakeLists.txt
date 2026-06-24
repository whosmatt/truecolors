idf_component_register(
    SRCS "truecolors_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        common
        appstate
        storage
        laser
        effects
        audio
        sysmgr
        wifi
        server
        nvs_flash
        app_update
)
