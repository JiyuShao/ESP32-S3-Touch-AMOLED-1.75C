# 03 — State → pet animation pipeline

**What to build:** PC Bridge 推送的状态 JSON 驱动宠物动画。WS `{"state":"thinking"}` → 宠物切到 thinking row 帧动画。这是主 seam——一个 WS 消息改变整个显示。同时引入 `pet_bridge` 和 `pet_render` 两个独立 component。

**Blocked by:** 02 (WiFi + WebSocket link)

**Status:** done (2026-08-13, build+flash verified; animation visuals await user board check)

## Acceptance criteria

- [x] `components/pet_bridge/` — `AgentState` enum + `AgentStatus` struct + `PetBridge` 类（纯逻辑，无 ESP/LVGL 依赖，host 可测）
- [x] `PetBridge::setStatusCallback(StatusCallback)` — 状态变化时回调（host test 验证）
- [x] WS 消息解析 `{"state":"..."}` → `AgentState` → callback — 手写 bounds-checked 提取器（去掉 cJSON 依赖），支持 `waiting`/`failed` 别名
- [x] idle 超时：10s 无消息 → callback `IDLE`（int32 差值，防 lv_tick 回绕）
- [x] DISCONNECTED 检测 → callback `DISCONNECTED`（ws_client 新增 status callback，连接成功/断开时触发）
- [x] `components/pet_render/` — `PetRenderer` 类
- [x] `PetRenderer::loadPet(...)` — ponytail 简化：pet.json/spritesheet 路径改为编译期生成（无文件系统），`tools/pet-assets/gen_frames.py` 把 Codex pet spritesheet 切帧 → C 数组 + state→row 映射表
- [x] `PetRenderer::playState(AgentState)` — 切到对应 row，开始帧循环
- [x] `PetRenderer::tick()` — 推进帧，loop states 循环，one-shot states（error）播放完保持最后一帧
- [x] spritesheet 实现：预切割帧 C 数组（airi sprite，96×104 RGB565 × 20 帧，~400KB flash），无 WebP decode
- [x] 验证: WS 发 `{"state":"thinking"}` → waiting row 切换 — pet_bridge host test 覆盖状态机；设备端视觉待用户确认（占位 WiFi 凭据下无法端到端）
- [x] 验证: 停止 WS 消息 10s → 回到 IDLE — host test
- [x] 验证: 断 WS 连接 → DISCONNECTED（灰色占位）— host test + renderer 灰盒实现
- [x] `idf.py build` 通过 — 0x374b10（4M factory 分区剩余 14%）

## Notes

- 帧源：lencx/pet airi spritesheet（webp 已提交 `tools/pet-assets/`），换素材只需换图重跑 gen_frames.py。
- 二进制 14% free —— ticket 05 的 ≥30% free 标准需要分区策略（加 storage 分区或缩小素材）。
- 线程模型：ws_client task → PetBridge（无锁轮询），LVGL timer 在 GUI task 轮询 bridge 状态 → renderer。
