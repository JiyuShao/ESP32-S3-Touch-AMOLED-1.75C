# 03 — State → pet animation pipeline

**What to build:** PC Bridge 推送的状态 JSON 驱动宠物动画。WS `{"state":"thinking"}` → 宠物切到 thinking row 帧动画。这是主 seam——一个 WS 消息改变整个显示。同时引入 `pet_bridge` 和 `pet_render` 两个独立 component。

**Blocked by:** 02 (WiFi + WebSocket link)

**Status:** ready-for-agent

## Acceptance criteria

- [ ] `components/pet_bridge/` — `AgentState` enum + `AgentStatus` struct + `PetBridge` 类
- [ ] `PetBridge::setStatusCallback(StatusCallback)` — 状态变化时回调
- [ ] WS 消息解析 `{"state":"..."}` → `AgentState` → callback
- [ ] idle 超时：10s 无消息 → callback `IDLE`
- [ ] DISCONNECTED 检测 → callback `DISCONNECTED`
- [ ] `components/pet_render/` — `PetRenderer` 类
- [ ] `PetRenderer::loadPet(spritesheet_path, pet_json_path)` — 解析 pet.json + 建立 state→row 映射
- [ ] `PetRenderer::playState(AgentState)` — 切到对应 row，开始帧循环
- [ ] `PetRenderer::tick()` — 推进帧，loop states 循环，one-shot states 播放完保持最后一帧
- [ ] spritesheet 实现：暂用**预切割单帧 PNG 文件**（6 个 state × 4-8 frames），避免 WebP decode + 内存风险
- [ ] 验证: WS 发 `{"state":"thinking"}` → 宠切换到 waiting row → WS 发 `{"state":"working"}` → 宠切换到 running row
- [ ] 验证: 停止 WS 消息 10s → 宠回到 IDLE
- [ ] 验证: 断 WS 连接 → 宠切到 DISCONNECTED（灰色占位）
- [ ] `idf.py build` 通过
