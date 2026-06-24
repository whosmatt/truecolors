// dns_server.h
// minimal captive-portal DNS server for use in AP mode

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Start the hijack DNS server on UDP :53 (idempotent).
void dns_server_start(void);

// Stop it (safe to call when not running).
void dns_server_stop(void);

#ifdef __cplusplus
}
#endif
