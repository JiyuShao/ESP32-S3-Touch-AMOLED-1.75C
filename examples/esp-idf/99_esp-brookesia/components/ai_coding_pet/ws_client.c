/*
 * Minimal RFC 6455 WebSocket client over lwIP sockets.
 * Only what the pet needs: unmasked server text frames, ping→pong,
 * close handling, 5 s reconnect. No TLS, no fragmentation of text frames
 * from the bridge (it sends single-frame messages).
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/sockets.h"
#include "esp_random.h"
#include "ws_client.h"
#ifdef ESP_UTILS_LOG_TAG
#   undef ESP_UTILS_LOG_TAG
#endif
#define ESP_UTILS_LOG_TAG "BS:ws-client"
#include "esp_lib_utils.h"

#define WS_MAGIC_KEY    "258EAFA5-E914-47DA-95CA-C5AB0DC85B11"
#define RECONNECT_MS    5000
#define SOCK_TIMEOUT_MS 5000

#define RX_BUF_SIZE 1024

static TaskHandle_t s_task = nullptr;
static volatile bool s_stop = false;
static volatile int s_fd = -1; // live socket, closed by stop() to unblock recv
static ws_client_msg_cb_t s_cb = nullptr;
static void *s_cb_user = nullptr;
static char s_ip[32];
static uint16_t s_port;
static char s_path[64];

/* ---------------- tiny base64 for the 16-byte handshake key ---------------- */
static const char B64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

static void base64_encode(const uint8_t *in, int len, char *out)
{
    int i, j = 0;
    for (i = 0; i + 2 < len; i += 3) {
        out[j++] = B64[in[i] >> 2];
        out[j++] = B64[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
        out[j++] = B64[((in[i + 1] & 0x0f) << 2) | (in[i + 2] >> 6)];
        out[j++] = B64[in[i + 2] & 0x3f];
    }
    if (i + 1 == len) {
        out[j++] = B64[in[i] >> 2];
        out[j++] = B64[(in[i] & 0x03) << 4];
        out[j++] = '=';
        out[j++] = '=';
    } else if (i + 2 == len) {
        out[j++] = B64[in[i] >> 2];
        out[j++] = B64[((in[i] & 0x03) << 4) | (in[i + 1] >> 4)];
        out[j++] = B64[(in[i + 1] & 0x0f) << 2];
        out[j++] = '=';
    }
    out[j] = '\0';
}

/* ---------------- socket helpers ---------------- */

static int connect_with_timeout(const char *ip, uint16_t port, int timeout_ms)
{
    int fd = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    if (fd < 0) {
        return -1;
    }
    struct timeval tv = {
        .tv_sec = timeout_ms / 1000,
        .tv_usec = (timeout_ms % 1000) * 1000,
    };
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

    struct sockaddr_in addr = {
        .sin_family = AF_INET,
        .sin_port = htons(port),
        .sin_addr.s_addr = inet_addr(ip),
    };
    if (connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* Read until "\r\n\r\n" or timeout. Returns number of bytes read (>= 0) or -1. */
static int read_until_headers_end(int fd, char *buf, int cap)
{
    int total = 0;
    while (total < cap - 1) {
        int n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n <= 0) {
            return -1;
        }
        total += n;
        buf[total] = '\0';
        if (strstr(buf, "\r\n\r\n") != nullptr) {
            return total;
        }
    }
    return -1;
}

/* ---------------- frame parser ---------------- */

typedef struct {
    char data[RX_BUF_SIZE]; // accumilated rx bytes
    int len;
} ws_rx_t;

/* Parse as many complete frames as possible. Returns 0 on ok, -1 on close frame. */
static int ws_parse_frames(ws_rx_t *rx, int fd)
{
    while (rx->len >= 2) {
        uint8_t opcode = (uint8_t)rx->data[0] & 0x0f;
        uint64_t payload_len = (uint8_t)rx->data[1] & 0x7f;
        int header_len = 2;
        if (payload_len == 126) {
            if (rx->len < 4) {
                return 0;
            }
            payload_len = ((uint8_t)rx->data[2] << 8) | (uint8_t)rx->data[3];
            header_len = 4;
        } else if (payload_len == 127) {
            if (rx->len < 10) {
                return 0;
            }
            payload_len = 0;
            for (int i = 0; i < 8; i++) {
                payload_len = (payload_len << 8) | (uint8_t)rx->data[2 + i];
            }
            header_len = 10;
        }
        if (rx->len < header_len + (int)payload_len) {
            return 0; // incomplete frame, wait for more
        }

        char *payload = rx->data + header_len;
        if (opcode == 0x1) { // text
            payload[payload_len] = '\0';
            if (s_cb) {
                s_cb(payload, (int)payload_len, s_cb_user);
            }
        } else if (opcode == 0x9) { // ping → pong (same payload, unmasked)
            char pong[2 + 125];
            pong[0] = (char)0x8a;
            pong[1] = (char)payload_len;
            memcpy(pong + 2, payload, payload_len);
            send(fd, pong, 2 + (int)payload_len, 0);
        } else if (opcode == 0x8) { // close
            send(fd, "\x88\x00", 2, 0);
            return -1;
        }

        memmove(rx->data, rx->data + header_len + payload_len, rx->len - header_len - payload_len);
        rx->len -= header_len + (int)payload_len;
    }
    return 0;
}

/* ---------------- client task ---------------- */

static void ws_client_task(void *arg)
{
    while (!s_stop) {
        int fd = connect_with_timeout(s_ip, s_port, SOCK_TIMEOUT_MS);
        if (fd < 0) {
            ESP_UTILS_LOGW("Connect to %s:%d failed, retry in %d s", s_ip, s_port, RECONNECT_MS / 1000);
            goto retry_wait;
        }
        s_fd = fd;

        /* Handshake */
        uint8_t key_raw[16];
        esp_fill_random(key_raw, sizeof(key_raw));
        char key_b64[32];
        base64_encode(key_raw, sizeof(key_raw), key_b64);

        char req[256];
        int req_len = snprintf(req, sizeof(req),
                               "GET %s HTTP/1.1\r\n"
                               "Host: %s:%d\r\n"
                               "Upgrade: websocket\r\n"
                               "Connection: Upgrade\r\n"
                               "Sec-WebSocket-Key: %s\r\n"
                               "Sec-WebSocket-Version: 13\r\n"
                               "\r\n",
                               s_path, s_ip, s_port, key_b64);
        if (send(fd, req, req_len, 0) != req_len) {
            ESP_UTILS_LOGW("Handshake send failed");
            close(fd);
            goto retry_wait;
        }

        char resp[512];
        int resp_len = read_until_headers_end(fd, resp, sizeof(resp));
        if (resp_len < 0 || strstr(resp, "101") == nullptr) {
            ESP_UTILS_LOGW("Handshake rejected: %.100s", resp);
            close(fd);
            goto retry_wait;
        }
        ESP_UTILS_LOGI("WS connected to %s:%d", s_ip, s_port);

        /* Receive loop */
        ws_rx_t rx = { .len = 0 };
        while (!s_stop) {
            int n = recv(fd, rx.data + rx.len, sizeof(rx.data) - rx.len, 0);
            if (n <= 0) {
                if (!s_stop) {
                    ESP_UTILS_LOGI("WS disconnected (reconnecting in %d s)", RECONNECT_MS / 1000);
                }
                break;
            }
            rx.len += n;
            if (ws_parse_frames(&rx, fd) < 0) {
                ESP_UTILS_LOGI("WS closed by server");
                break;
            }
        }
        close(fd);
        if (s_fd == fd) {
            s_fd = -1;
        }

retry_wait:
        for (int i = 0; i < RECONNECT_MS / 100; i++) {
            if (s_stop) {
                break;
            }
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    vTaskDelete(nullptr);
}

esp_err_t ws_client_start(const char *ip, uint16_t port, const char *path,
                          ws_client_msg_cb_t cb, void *user_data)
{
    if (s_task != nullptr) {
        return ESP_ERR_INVALID_STATE;
    }
    strncpy(s_ip, ip, sizeof(s_ip) - 1);
    s_port = port;
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_cb = cb;
    s_cb_user = user_data;
    s_stop = false;

    if (xTaskCreate(ws_client_task, "ws_client", 4096, nullptr, 5, &s_task) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void ws_client_stop(void)
{
    s_stop = true;
    if (s_fd >= 0) {
        close(s_fd); // unblocks recv in the task
        s_fd = -1;
    }
    if (s_task != nullptr) {
        vTaskDelay(pdMS_TO_TICKS(200)); // let the task exit its loop and delete itself
        s_task = nullptr;
    }
}
