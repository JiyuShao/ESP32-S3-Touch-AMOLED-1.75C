# Research: ESP32-S3 + Claude Code physical dashboard, Waveshare AMOLED-1.75C board

Date: 2026-08-12. Sources: Reddit post (blocked — identified as Claudeq via title match), Claudeq repo, Clawdmeter repo, lysenko.dev blog, local board repo inspection (this repo is the Waveshare `ESP32-S3-Touch-AMOLED-1.75C` checkout, branch `feat/99-esp-brookesia`).

## 1. ESP32 ↔ Claude Code architecture (the Reddit project = Claudeq)

The Reddit post (`r/esp32/comments/1uvwv1k`, "Built a WiFi touchscreen dock ESP32-S3 that acts as...") is unreachable — Reddit blocks both HTML and JSON fetches. The title uniquely matches **Claudeq** (https://github.com/Positronico/claudeq, MIT), which turns a Waveshare ESP32-S3 touchscreen into a tap-to-answer control surface for Claude Code. Architecture:

- **Host side — a Node.js bridge** (`bridge/bridge.mjs`) runs on each computer, listening on port **8787**. It wraps the Claude Code CLI: you launch `claudeq` instead of `claude`; the launcher starts the bridge if needed, names the session, and runs Claude inside a tmux session.
- **Claude Code integration is via hooks, not MCP**: repo ships `bridge/hooks/` + `ccdeck-settings.json`. Hooks carry `session_id` + tmux target and feed session status/questions to the bridge over **HTTP**. `AskUserQuestion` prompts are answered via the hook's own HTTP long-poll; macros/voice replies are injected locally via `tmux send-keys` into the focused session's pane. Falls back to the normal in-terminal picker when no deck is connected.
- **Device side — ESP32 firmware**: ESP-IDF 5.4.1, LVGL UI, WiFi + WebSocket client. Connects to **all bridges concurrently over WebSocket**, merging sessions from every machine on the LAN (also works over Tailscale, via an embedded `microlink` submodule). Pairing uses a live numeric-comparison code; all traffic is AES-256-GCM encrypted. Message format documented in `docs/PROTOCOL.md`.
- **Board used by Claudeq: a different Waveshare board** — ESP32-S3-Touch-LCD-3.49 (3.49", portrait 172×640). Not the AMOLED-1.75C.
- **WiFi provisioning**: never at build time — on-device SoftAP setup portal. First boot (or network unreachable) creates hotspot `Claudeq-setup`, captive portal at `http://192.168.4.1`; enter 2.4 GHz SSID/password. Bridge address can be left blank — auto-discovered via mDNS (`_claudeq._tcp`); fixed-IP fallback if mDNS is blocked. OTA updates from GitHub over HTTPS. Browser-based flashing via ESP Web Tools (no toolchain).

### Alternatives / context (same niche)

- **Clawdmeter** (https://github.com/HermannBjorgvin/Clawdmeter): ESP32-S3-Touch-AMOLED-2.16 desk dashboard showing Claude Code token usage. Host Python daemon (`bleak` + `httpx`) reads the Claude Code OAuth token from macOS Keychain / `~/.claude/.credentials.json` and pushes JSON over **BLE** every ~60s; firmware is LVGL + NimBLE, doubles as BLE HID keyboard. A community fork ported it to **WiFi (HTTP POST + mDNS)**.
- **lysenko.dev "Claude Code usage gauge"** (https://lysenko.dev/posts/2026-06-esp32-claude-usage-gauge/): on the **ESP32-S3-Touch-AMOLED-1.8** (same family as our board, SH8601 QSPI panel), a Mac daemon pings Claude API once a minute and pushes usage over **BLE**; LVGL renders gauges; second button auto-approves permission prompts. Fork of Clawdmeter; PlatformIO build.
- **ESPClaude**: ESP32-S3 Box 3 over WiFi to a usage-stats server, LVGL dashboards (Chinese project).

**Takeaway for our project**: the proven patterns are (a) host-side daemon/bridge that owns the Claude Code integration (hooks/CLI wrapper) and (b) device-side thin client. WiFi + WebSocket + mDNS (Claudeq) is the interactivity route; BLE (Clawdmeter) is the simpler display-only route. No project drives Claude Code from the ESP32 itself — the bridge always mediates.

## 2. WiFi + WebSocket specifics (from Claudeq firmware)

- ESP-IDF 5.4.1, `idf.py build`, cmake/ninja.
- WiFi STA + SoftAP provisioning portal; settings persisted in NVS.
- WebSocket **client** connecting to multiple bridges; mDNS service discovery (`_claudeq._tcp`), fixed-IP fallback.
- HTTPS OTA; TLS everywhere; AES-256-GCM device↔bridge channel.

## 3. Waveshare ESP32-S3-Touch-AMOLED-1.75C board capabilities

From the board BSP and official repo (`waveshareteam/ESP32-S3-Touch-AMOLED-1.75C`, Apache-2.0):

- MCU: ESP32-S3 (dual-core LX7 @ 240 MHz, WiFi 4 / 2.4 GHz + BLE 5.0), **8 MB octal PSRAM** (80 MHz, XIP-from-PSRAM), **32 MB flash** (per `sdkconfig.defaults`: `CONFIG_ESPTOOLPY_FLASHSIZE_32MB`, `CONFIG_SPIRAM_MODE_OCT`, `CONFIG_SPIRAM_XIP_FROM_PSRAM`).
- Display: 1.75" **466×466 QSPI AMOLED**, controller **CO5300** (driver component `espressif/esp_lcd_co5300`).
- Touch: **CST9217** capacitive, I2C (`waveshare/esp_lcd_touch_cst9217`).
- Power: AXP2101 PMIC. Motion: QMI8658 IMU. Audio: ES7210 dual mics, ES8311 codec.
- BSP component `waveshare/esp32_s3_touch_amoled_1_75c` (^3.0.0) requires `idf >= 5.5`; pulls esp_lcd_co5300, esp_lcd_panel_io_additions, esp_lvgl_adapter (~0.6), lvgl `>=8,<10`, esp_codec_dev (~1.5), cst9217 touch.
- Official ESP-IDF examples: `01_AXP2101`, `02_lvgl_demo_v9`, `03_esp-brookesia`, `04_Immersive_block`, `05_Spec_Analyzer`; Arduino examples; `Firmware/` factory binaries; `Schematic/` public.
- **No networking examples ship with the board** — the repo has zero WiFi/mDNS/WebSocket code. WiFi is available at the driver level (ESP-IDF enables it by default) but nothing on the board uses it yet.

## 4. LVGL on this board

- LVGL **9.5.0** pinned via `lvgl/lvgl` in `brookesia_core` (99 example; official 03 example same).
- LVGL is configured for **pure software rendering** with 2 draw units, Freertos OS integration, 15 ms refresh (`CONFIG_LV_DEF_REFR_PERIOD=15`), full Montserrat font set, PSRAM-backed XIP. No GPU/SSD — AMOLED panel is just a framebuffer target over QSPI.
- Display path: `esp_lcd_co5300` (QSPI panel driver) + `esp_lvgl_adapter` + `esp_lv_fs`/`esp_lv_decoder`/`esp_mmap_assets` for images/fonts. Touch via `esp_lcd_touch` (CST9217), max 5 touch points.
- Two ways to build UI on this board: (a) plain LVGL 9 app (example 02), (b) **ESP-Brookesia** framework (examples 03/99) — app launcher + status bar + nav bar built on LVGL, with `brookesia_core` component.

## 5. WiFi stack on this board today (local 99_esp-brookesia sdkconfig)

What's already compiled in by default (ESP-IDF driver level):

- `CONFIG_ESP_WIFI_ENABLED=y` — STA + SoftAP support, WPA3-SAE/OWE, enterprise, dynamic TX buffers, NVS-backed config, `CONFIG_ESP_WIFI_TASK_PINNED_TO_CORE_0`.
- `CONFIG_ESP_NETIF_TCPIP_LWIP=y` — lwIP: DHCP client + **DHCPS** (SoftAP AP-mode DHCP server built in), DNS, IPv4+IPv6, `CONFIG_LWIP_MAX_SOCKETS=10`, TCP syn/retrans tuning.
- ESP-TLS / mbedTLS enabled; `esp_http_client` with HTTPS (`CONFIG_ESP_HTTP_CLIENT_ENABLE_HTTPS=y`).
- **Not enabled**: `mdns` component (no mdns dependency anywhere; only lwIP's mDNS-query answering is on), WebSocket (no `esp_websocket_client` in manifest; `HTTPD_WS_SUPPORT` off), MQTT, NVS is on via wifi.

**Gap to close for a Claude dashboard on the AMOLED-1.75C**: add `mdns` + `esp_websocket_client` components (both standard ESP-IDF components, just missing from the manifest), write WiFi provisioning (SoftAP portal pattern from Claudeq), and keep the host-side bridge pattern — ESP32 stays a thin WebSocket client.

### Local repo state (branch `feat/99-esp-brookesia`)

- `examples/esp-idf/99_esp-brookesia/` = ESP-Brookesia squareline demo: components `brookesia_core` (vendored espressif esp-brookesia core, v0.6.0-beta2, `idf >= 5.3`) + `brookesia_app_squareline_demo` (Squareline-generated UI under `ui/`, app `SquarelineDemo` with launcher icon). Custom `partitions.csv`; `sdkconfig` generated with target esp32s3.
- Managed components include the board BSP, CO5300 driver, CST9217 touch, LVGL 9.5.0, esp_lvgl_adapter, cJSON, freetype, jpeg/png decoders, knob, usb.
- Branch history: brookesia_core App-lifecycle hardening commits + scratch docs — matches the parent project's in-flight work.
