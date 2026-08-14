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
 * @brief Called on connect/disconnect transitions (true = connected).
 */
typedef void (*ws_client_status_cb_t)(bool connected, void *user_data);

/**
 * @brief Start the WebSocket client task.
 *
 * Connects to ws://ip:port/path, reconnects every 5 s on failure or
 * disconnect, answers server pings, and invokes cb for every text frame
 * and status_cb on connection state transitions.
 * Only one connection is supported (singleton task).
 */
esp_err_t ws_client_start(const char *ip, uint16_t port, const char *path,
                          ws_client_msg_cb_t cb, ws_client_status_cb_t status_cb,
                          void *user_data);

/**
 * @brief Stop the client task and close the socket. Safe to call twice.
 */
void ws_client_stop(void);

/**
 * @brief Send one text frame to the server (masked, RFC 6455 client rules).
 *
 * Thread-safe (may be called from any task); best-effort. The frame is
 * dropped if the client is not connected. Text must be at most 125 bytes —
 * enough for the pet's one upstream message (permission_response).
 * @return ESP_OK on send, ESP_ERR_INVALID_STATE if not connected,
 *         ESP_ERR_INVALID_ARG if the text is empty or too long.
 */
esp_err_t ws_client_send_text(const char *text);

#ifdef __cplusplus
}
#endif
