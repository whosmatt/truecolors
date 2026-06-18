# Builds the web UI and stages the gzipped bundle plus an ETag header for embedding.
# Invoked with -DWEB_DIR, -DOUT_DIR, -DOUT_HDR.

find_program(NPM_EXE npm)
if(NOT NPM_EXE)
    message(FATAL_ERROR "npm/Node not found. Install Node 20+ to build the web UI.")
endif()

if(NOT EXISTS "${WEB_DIR}/node_modules")
    execute_process(COMMAND ${NPM_EXE} ci WORKING_DIRECTORY "${WEB_DIR}" RESULT_VARIABLE r)
    if(r)
        message(FATAL_ERROR "npm ci failed")
    endif()
endif()

execute_process(COMMAND ${NPM_EXE} run build WORKING_DIRECTORY "${WEB_DIR}" RESULT_VARIABLE r)
if(r)
    message(FATAL_ERROR "npm run build failed")
endif()

file(COPY "${WEB_DIR}/dist/index.html.gz" DESTINATION "${OUT_DIR}")
file(SHA256 "${OUT_DIR}/index.html.gz" HASH)
file(WRITE "${OUT_HDR}" "#pragma once\n#define WEB_INDEX_ETAG \"${HASH}\"\n")
