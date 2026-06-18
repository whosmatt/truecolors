// ws_internal.h
#pragma once

#include "esp_http_server.h"

// Register the /ws handler and the bus subscriptions on a running server.
void ws_start(httpd_handle_t server);

// Remove a closed socket from the client table.
void ws_on_close(int fd);
