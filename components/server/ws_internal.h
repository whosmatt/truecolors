// ws_internal.h
#pragma once

#include "esp_http_server.h"

// Register the /ws handler and the bus subscriptions on a running server.
void ws_start(httpd_handle_t server);

// Remove a closed socket from the client table.
void ws_on_close(int fd);

// OTA progress to all clients: stage "write" (progress 0..1), "done" or "error".
void ws_broadcast_ota(const char *stage, float progress);
