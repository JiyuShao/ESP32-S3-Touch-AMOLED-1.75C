# 02 — WiFi + WebSocket link

**What to build:** ESP32 连上 WiFi，建立 WebSocket 连接到 PC Bridge。PC Bridge 收到 HTTP POST 后通过 WS 推送 JSON 给 ESP32，ESP32 serial log 打印收到的消息。不涉及动画切换——只验证通信链路。

**Blocked by:** 01 (Pet App skeleton)

**Status:** done (2026-08-13, board-verified boot; end-to-end awaits real credentials)

## Acceptance criteria

- [x] `main.cpp` 中在 `Phone::begin()` 之前初始化 WiFi STA（硬编码 SSID/密码）— `main/wifi_config.h`（只放 WiFi 凭据；bridge 端点在组件的 `pet_bridge_config.h`）
- [x] WiFi 连接成功 → `ESP_LOGI` 打印 IP 地址 — 占位凭据下按预期 10s 超时告警（"pet app will stay disconnected"）
- [x] `pet_app` 内部创建 WebSocket client task，连接 `ws://<PC_IP>:8787/pet` — 手写 RFC 6455 client（`ws_client.h/c`，esp_websocket_client 组件注册表不可达）
- [x] 收到 WS 消息 → 解析 JSON → `ESP_LOGI` 打印 `state` 字段
- [x] WS 断连 → 自动重连（5s 间隔）
- [x] PC Bridge: `tools/pet-bridge/bridge.js` — HTTP `POST /state` + WS server `/pet` + 消息转发（零依赖 Node）
- [x] 验证: `curl -X POST localhost:8787/state ...` → ESP32 serial 打印 `state: thinking` — PC 侧用 `tools/pet-bridge/test/ws-client-test.js` 验证通过（POST 200/400、ping→pong、推送+重放）；ESP32 serial 段待用户填入真实凭据后验证
- [x] `idf.py build` 通过

## Notes

- 烧录验证：boot 干净、WiFi 超时告警、两个 App 正常安装。
- 二进制 0x3129a0（WiFi 栈 +29% 体积），注意 ticket 05 的 ≥30% flash free 标准。
- 端到端验证：在 `main/wifi_config.h` 填真实 SSID/密码、`pet_bridge_config.h` 填 PC IP，重启 bridge.js 后 curl POST。
