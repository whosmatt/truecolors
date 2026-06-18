idf_component_register(
    SRCS "storage.c"
    INCLUDE_DIRS "include"
    REQUIRES common appstate nvs_flash
)
