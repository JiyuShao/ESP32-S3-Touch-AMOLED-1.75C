# 02 — WiFi + WebSocket link

**What to build:** ESP32 连上 WiFi，建立 WebSocket 连接到 PC Bridge。PC Bridge 收到 HTTP POST 后通过 WS 推送 JSON 给 ESP32，ESP32 serial log 打印收到的消息。不涉及动画切换——只验证通信链路。

**Blocked by:** 01 (Pet App skeleton)

**Status:** ready-for-agent

## Acceptance criteria

- [ ] `main.cpp` 中在 `Phone::begin()` 之前初始化 WiFi STA（硬编码 SSID/密码）
- [ ] WiFi 连接成功 → `ESP_LOGI` 打印 IP 地址
- [ ] `pet_app` 内部创建 WebSocket client task，连接 `ws://<PC_IP>:8787/pet`
- [ ] 收到 WS 消息 → 解析 JSON → `ESP_LOGI` 打印 `state` 字段
- [ ] WS 断连 → 自动重连（5s 间隔）
- [ ] PC Bridge: `tools/pet-bridge/bridge.js` — HTTP `POST /state` + WS server `/pet` + 消息转发
- [ ] 验证: `curl -X POST localhost:8787/state -H "Content-Type: application/json" -d '{"version":"v1","agent_id":"claude-code","session_id":"test","state":"thinking","event":"","timestamp":0}'` → ESP32 serial 打印 `state: thinking`
- [ ] `idf.py build` 通过
