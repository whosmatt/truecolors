idf_component_register(
    SRCS "http_server.c" "ws.c"
    INCLUDE_DIRS "include"
    REQUIRES common appstate esp_http_server
    PRIV_REQUIRES effects wifi storage laser espressif__cjson esp_system lwip app_update esp_app_format
)

# Build the Svelte UI to a single gzipped index.html and embed it.
set(WEB_DIR ${CMAKE_CURRENT_SOURCE_DIR}/../../web)
set(WEB_GZ ${CMAKE_CURRENT_BINARY_DIR}/index.html.gz)
set(WEB_HDR ${CMAKE_CURRENT_BINARY_DIR}/web_assets.h)

file(GLOB_RECURSE WEB_SRCS ${WEB_DIR}/src/*)

add_custom_command(
    OUTPUT ${WEB_GZ} ${WEB_HDR}
    COMMAND ${CMAKE_COMMAND}
            -DWEB_DIR=${WEB_DIR} -DOUT_DIR=${CMAKE_CURRENT_BINARY_DIR} -DOUT_HDR=${WEB_HDR}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/build_web.cmake
    DEPENDS ${WEB_SRCS} ${WEB_DIR}/package.json ${WEB_DIR}/vite.config.ts ${WEB_DIR}/index.html
    COMMENT "Building embedded web UI"
    VERBATIM
)
add_custom_target(webui DEPENDS ${WEB_GZ} ${WEB_HDR})
add_dependencies(${COMPONENT_LIB} webui)
target_include_directories(${COMPONENT_LIB} PRIVATE ${CMAKE_CURRENT_BINARY_DIR})
target_add_binary_data(${COMPONENT_LIB} ${WEB_GZ} BINARY)
