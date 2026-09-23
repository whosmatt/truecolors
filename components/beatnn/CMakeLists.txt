idf_component_register(
    SRCS "beatnn.cc"
    INCLUDE_DIRS "include"
    REQUIRES audio
    PRIV_REQUIRES esp_timer
)

target_add_binary_data(${COMPONENT_LIB} "model/model.tflite" BINARY)
