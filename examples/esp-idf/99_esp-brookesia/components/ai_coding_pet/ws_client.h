#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Callback for text frames received from the server.
 * @param payload  null-terminated text (buffer reused after callback returns)
 * @param len      payload length
 * @param user_data opaque pointer passed to ws_client_start
 */
typedef void (*ws_client_msg_cb_t)(const char *payload, int len, void *user_data);

/**
 * @brief Start the WebSocket client task.
 *
 * Connects to ws://ip:port/path, reconnects every 5 s on failure or
 * disconnect, answers server pings, and invokes cb for every text frame.
 * Only one connection is supported (singleton task).
 */
esp_err_t ws_client_start(const char *ip, uint16_t port, const char *path,
                          ws_client_msg_cb_t cb, void *user_data);

/**
 * @brief Stop the client task and close the socket. Safe to call twice.
 */
void ws_client_stop(void);

#ifdef __cplusplus
}
#endif
