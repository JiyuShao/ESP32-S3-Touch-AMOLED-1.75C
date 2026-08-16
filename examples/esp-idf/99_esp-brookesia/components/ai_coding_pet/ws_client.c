/*
 * Minimal RFC 6455 WebSocket client over lwIP sockets.
 * Only what the pet needs: unmasked server text frames, ping→pong,
 * close handling, 5 s reconnect. No TLS, no fragmentation of text frames
 * from the bridge (it sends single-frame messages).
 */
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <fcntl.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
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
#define SEND_TEXT_MAX   125 // single-byte length header only (client masking)

#define RX_BUF_SIZE 2048

static TaskHandle_t s_task = NULL;
static volatile bool s_stop = false;
static volatile bool s_task_exited = false; // set by the task right before vTaskDelete
static volatile int s_fd = -1; // live socket, closed by stop() to unblock recv
static SemaphoreHandle_t s_tx_mutex = NULL; // guards writes: task (pong/close) vs UI task (send_text)
static ws_client_msg_cb_t s_cb = NULL;
static ws_client_status_cb_t s_status_cb = NULL;
static void *s_cb_user = NULL;
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

    /* Non-blocking connect so stop() can abort within one poll slice. */
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    int ret = connect(fd, (struct sockaddr *)&addr, sizeof(addr));
    if (ret != 0 && errno != EINPROGRESS) {
        close(fd);
        return -1;
    }

    int elapsed = 0;
    while (elapsed < timeout_ms) {
        if (s_stop) {
            close(fd);
            return -1;
        }
        fd_set wfds;
        FD_ZERO(&wfds);
        FD_SET(fd, &wfds);
        struct timeval ptv = { .tv_sec = 0, .tv_usec = 100000 };
        int sel = select(fd + 1, NULL, &wfds, NULL, &ptv);
        if (sel > 0) {
            int sock_err = 0;
            socklen_t sock_err_len = sizeof(sock_err);
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &sock_err, &sock_err_len);
            if (sock_err != 0) {
                close(fd);
                return -1;
            }
            fcntl(fd, F_SETFL, flags); // back to blocking for the session
            /* The connect-phase SO_RCVTIMEO must not linger: a quiet bridge
             * (no state changes) would make recv() EAGAIN every 5 s and we'd
             * read that as a disconnect. Block until data/close/stop() instead. */
            struct timeval no_timeout = { .tv_sec = 0, .tv_usec = 0 };
            setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &no_timeout, sizeof(no_timeout));
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &no_timeout, sizeof(no_timeout));
            return fd;
        } else if (sel < 0 && errno != EINTR) {
            close(fd);
            return -1;
        }
        elapsed += 100;
    }
    close(fd);
    return -1;
}

/* Read until "\r\n\r\n" or timeout. Returns number of bytes read (>= 0) or -1;
 * on success *header_end is the offset just past "\r\n\r\n". Bytes after it
 * (the first WS frame sharing the 101 segment) belong to the caller. */
static int read_until_headers_end(int fd, char *buf, int cap, int *header_end)
{
    int total = 0;
    while (total < cap - 1) {
        int n = recv(fd, buf + total, cap - 1 - total, 0);
        if (n <= 0) {
            return -1;
        }
        total += n;
        buf[total] = '\0';
        const char *end = strstr(buf, "\r\n\r\n");
        if (end != NULL) {
            *header_end = (int)(end - buf) + 4;
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
        ESP_UTILS_LOGD("ws frame op=%02x len=%d buf=%d", opcode, (int)payload_len, rx->len);
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
        if (header_len + (int)payload_len >= RX_BUF_SIZE) {
            // 1009 message too big — no room for the NUL terminator we promise
            xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
            send(fd, "\x88\x02\x03\xf1", 4, 0);
            xSemaphoreGive(s_tx_mutex);
            return -1;
        }

        char *payload = rx->data + header_len;
        if (opcode == 0x1) { // text
            /* NOTE: never write a NUL terminator into rx->data here — when
             * several frames share the buffer, payload[payload_len] is the
             * FIRST BYTE OF THE NEXT FRAME and zeroing it corrupts that
             * frame's opcode (0x81 → 0x00, silently dropped as a
             * continuation). All downstream parsing is length-bounded. */
            ESP_UTILS_LOGD("ws rx %d B: %.*s", (int)payload_len,
                           (int)(payload_len > 60 ? 60 : payload_len), payload);
            if (s_cb) {
                s_cb(payload, (int)payload_len, s_cb_user);
            }
        } else if (opcode == 0x9) { // ping → pong (same payload, unmasked)
            char pong[2 + 125];
            pong[0] = (char)0x8a;
            pong[1] = (char)payload_len;
            memcpy(pong + 2, payload, payload_len);
            xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
            send(fd, pong, 2 + (int)payload_len, 0);
            xSemaphoreGive(s_tx_mutex);
        } else if (opcode == 0x8) { // close
            xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
            send(fd, "\x88\x00", 2, 0);
            xSemaphoreGive(s_tx_mutex);
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

        ws_rx_t rx = { .len = 0 };

        char resp[512];
        int header_end = 0;
        int resp_len = read_until_headers_end(fd, resp, sizeof(resp), &header_end);
        if (resp_len < 0 || strstr(resp, "101") == NULL) {
            ESP_UTILS_LOGW("Handshake rejected: %.100s", resp);
            close(fd);
            goto retry_wait;
        }
        /* The bridge's snapshot often shares the 101 TCP segment — bytes past
         * the header are already WS frames and must feed the frame parser,
         * or the rx stream stays misaligned forever. */
        int carry = resp_len - header_end;
        if (carry > 0) {
            memcpy(rx.data, resp + header_end, carry);
            rx.len = carry;
        }
        ESP_UTILS_LOGI("WS connected to %s:%d", s_ip, s_port);
        if (s_status_cb) {
            s_status_cb(true, s_cb_user);
        }

        /* Receive loop */
        while (!s_stop) {
            int n = recv(fd, rx.data + rx.len, sizeof(rx.data) - rx.len, 0);
            if (n <= 0) {
                if (!s_stop) {
                    ESP_UTILS_LOGI("WS disconnected (reconnecting in %d s)", RECONNECT_MS / 1000);
                    if (s_status_cb) {
                        s_status_cb(false, s_cb_user);
                    }
                }
                break;
            }
            rx.len += n;
            if (ws_parse_frames(&rx, fd) < 0) {
                ESP_UTILS_LOGI("WS closed by server");
                if (s_status_cb) {
                    s_status_cb(false, s_cb_user);
                }
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
    s_task_exited = true;
    vTaskDelete(NULL);
}

esp_err_t ws_client_send_text(const char *text)
{
    if (text == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    size_t len = strlen(text);
    if (len == 0 || len > SEND_TEXT_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    int fd = s_fd;
    if (fd < 0) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t mask[4];
    esp_fill_random(mask, sizeof(mask));
    char frame[2 + 4 + SEND_TEXT_MAX];
    frame[0] = (char)0x81;             // FIN + text
    frame[1] = (char)(0x80 | len);     // masked + payload length
    memcpy(frame + 2, mask, 4);
    for (size_t i = 0; i < len; i++) {
        frame[2 + 4 + i] = text[i] ^ (char)mask[i & 3];
    }

    xSemaphoreTake(s_tx_mutex, portMAX_DELAY);
    int sent = send(fd, frame, 2 + 4 + (int)len, 0);
    xSemaphoreGive(s_tx_mutex);
    return (sent == 2 + 4 + (int)len) ? ESP_OK : ESP_FAIL;
}

esp_err_t ws_client_start(const char *ip, uint16_t port, const char *path,
                          ws_client_msg_cb_t cb, ws_client_status_cb_t status_cb,
                          void *user_data)
{
    if (s_task != NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_tx_mutex == NULL) {
        s_tx_mutex = xSemaphoreCreateMutex();
        if (s_tx_mutex == NULL) {
            return ESP_ERR_NO_MEM;
        }
    }
    strncpy(s_ip, ip, sizeof(s_ip) - 1);
    s_port = port;
    strncpy(s_path, path, sizeof(s_path) - 1);
    s_cb = cb;
    s_status_cb = status_cb;
    s_cb_user = user_data;
    s_stop = false;
    s_task_exited = false;

    /* Priority must beat the LVGL task (prio 6): the SW renderer can hog the
     * CPU for seconds per frame, starving a low-priority recv loop — display
     * pushes then queue in the socket and apply late/out of window (the demo
     * lost the whole attention state to this). It must stay BELOW lwIP's
     * TCP/IP task (prio 18): at 22 we preempted the TCP stack itself and the
     * connect never completed. The task blocks on recv() almost always, so
     * the elevated priority costs nothing when idle. */
    if (xTaskCreate(ws_client_task, "ws_client", 8192, NULL, 12, &s_task) != pdPASS) {
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
    if (s_task != NULL) {
        // Wait for the task to exit before clearing the handle so a later
        // start() can't spawn a second task while this one still lingers.
        for (int i = 0; i < 40 && !s_task_exited; i++) {
            vTaskDelay(pdMS_TO_TICKS(50)); // up to 2 s (bounded by connect poll)
        }
        s_task = NULL;
    }
}
