# AI Coding Agent Desktop Pet

> Status: ready-for-agent

## Problem Statement

开发者使用 Claude Code、Codex 等 AI coding agent 时，需要频繁在终端和 IDE 之间切换来确认 Agent 状态——是否在思考、是否在执行工具、是否出错。没有一种"瞥一眼就知道"的物理反馈方式。现有的桌面端方案（Clawd on Desk、Codex Pet）依赖主显示器，当屏幕被终端/IDE 占满时无法同时可见。

用户需要一块 **独立的物理设备**，放在桌面上，实时显示 AI coding agent 的运行状态。设备不做复杂终端交互，只负责把 Agent 状态用宠物动画表达出来。

## Solution

一台基于 **Waveshare ESP32-S3-Touch-AMOLED-1.75C** 的实体桌宠。PC 端的 lightweight daemon 采集 Claude Code（及后续 Codex、Cursor 等）Agent 状态，通过 WiFi + WebSocket 推送到 ESP32 设备。设备端用 LVGL 播放 Codex Pet 格式的 spritesheet 动画，呈现对应状态的宠物行为。

核心架构：**Clawd on Desk 的 Agent Core（状态采集 + 优先级引擎）+ Codex Pet 的视觉规范（9-row spritesheet）+ Waveshare 的硬件底座（BSP + LVGL + brookesia_core）**。

设备作为 99_esp-brookesia Phone System 中的一个常规 Phone App 存在，走完整生命周期（init → run → pause → resume → close），从 Launcher 启动。

## User Stories

1. As a developer, I want the pet device to show an **idle animation** when Claude Code has no active session, so I know at a glance that nothing is happening.
2. As a developer, I want the pet to switch to a **thinking animation** when my model is processing, so I can see work is in progress without alt-tabbing.
3. As a developer, I want the pet to switch to a **working animation** when Claude Code runs a tool (Write, Edit, Bash), so I know the agent is executing actions.
4. As a developer, I want the pet to show an **error animation** when a tool call fails or the agent encounters an error, so I can react promptly.
5. As a developer, I want the pet to show an **attention animation** (e.g., waving) when the agent completes a task and needs my attention, so I know to check the result.
6. As a developer, I want the pet to automatically return to **idle** when no agent updates arrive for 10 seconds, so I can detect connectivity loss.
7. As a developer, I want the pet to show a **disconnected** indicator when the WiFi or WebSocket connection drops, so I can distinguish "agent idle" from "link broken."
8. As a developer, I want the pet to appear in the **Launcher** alongside the existing Squareline app, so I can start it from the device home screen.
9. As a developer, I want to **pause** the pet when switching to another app, and **resume** it when switching back, so it doesn't waste CPU when not visible.
10. As a developer, I want the PC Bridge to support **multiple agent types** (Claude Code first, Codex next) through the same Clawd-compatible protocol, so I don't need to rebuild the bridge when adding agents.
11. As a developer, I want the PC Bridge to resolve **multiple concurrent sessions** to a single display state using Clawd's priority algorithm, so the most critical session (e.g., error) always takes over the display.
12. As a developer, I want to tap a **permission approval** on the pet screen to allow or deny a tool call (v1.1), so I can approve actions without returning to the terminal.
13. As a developer, I want to use **any Codex Pet community sprite** on the device without format conversion, so I can customize the pet appearance.

## Implementation Decisions

### Architecture division

The PC side owns agent semantics: session monitoring, state ingestion, priority resolution, and multi-agent aggregation. The ESP32 side owns rendering: spritesheet animation, display, and touch input. The boundary is a WebSocket protocol at the network layer.

PC Bridge is a Node.js daemon exposing Clawd-compatible `POST /state` over HTTP. It resolves state via the Clawd priority engine and pushes the resolved display state to all connected ESP32 clients over WebSocket.

### State model

The PC Bridge tracks the full Clawd 12-state priority model internally (`error:8 → notification:7 → attention:5 → carrying/juggling:4 → working:3 → thinking:2 → idle/roam:1 → sleeping:0`). It resolves multiple concurrent sessions via `max(priority)` to a single display state.

The ESP32 receives a reduced 6-display-state set (`IDLE, THINKING, WORKING, ATTENTION, ERROR, DISCONNECTED`) mapped to 9-row Codex Pet spritesheet rows.

State mapping from Clawd to Codex rows:

| Clawd State (priority) | Display State | Spritesheet Row |
|---|---|---|
| error (8) | ERROR | 5: failed |
| notification (7) | ATTENTION | 3: waving |
| attention (5) | ATTENTION | 3: waving |
| carrying/juggling (4) | WORKING | 7: running |
| working (3) | WORKING | 7: running |
| thinking (2) | THINKING | 6: waiting |
| idle/roam (1) | IDLE | 0: idle (loops) |
| sleeping (0) | IDLE | 0: idle |
| (no connection) | DISCONNECTED | — |

### WebSocket protocol

PC Bridge → ESP32, single JSON message per state change:

```json
{
  "version": "v1",
  "agent_id": "claude-code",
  "session_id": "abc123",
  "state": "working",
  "event": "PreToolUse:Write",
  "timestamp": 1692000000000
}
```

ESP32 implements idle timeout: 10 seconds without a message → auto-transition to IDLE. WS disconnect → DISCONNECTED state, auto-reconnect every 5 seconds.

### ESP32 component structure

Two new independent components under `components/`:

- **pet_bridge**: Transport abstraction (`TransportInterface` with WS and BLE implementations), WiFi management, WebSocket client lifecycle, status callback dispatch, idle timeout timer.
- **pet_render**: Spritesheet loader (Codex `pet.json` + `.webp` format), LVGL image descriptor, row-based frame animation, state-to-row mapping.

The Pet App component wraps both and implements the Brookesia Phone App lifecycle.

### Animation format

Codex Pet spritesheet standard: single WebP image 1536×1872 px (8 columns × 9 rows), each cell 192×208 px. Row 0 = idle (looping, ≤8 frames), row 3 = waving, row 5 = failed, row 6 = waiting, row 7 = running. `pet.json` manifest: `{id, displayName, description, spritesheetPath}`.

ESP32 renders one cell at a time via `lv_image` crop. Full spritesheet is too large for RAM; frames are decoded on-demand or pre-cut into individual cell files.

### Phone App integration

Pet exists as a standard Phone App registered via `WHOLE_ARCHIVE` static registry. Launcher icon derived from spritesheet idle row first frame. Lifecycle: `init` creates bridge + renderer, `run` connects WiFi + WS + starts animation timer, `pause` pauses tick timer (keeps connection), `resume` restores tick, `close` disconnects + cleans resources.

### WiFi configuration

MVP: hardcoded SSID/password in sdkconfig. Future: SoftAP provisioning portal (Claudeq model: `Pet-setup` AP, `192.168.4.1` captive portal, NVS persistence). ESP-IDF components required: `esp_wifi`, `esp_netif`, `lwIP`, `esp_websocket_client`, `mdns`.

### Claude Code hook integration

Hook scripts installed into `~/.claude/settings.json` (UserPromptSubmit, PreToolUse, PostToolUse, Stop, SubagentStart events). Each hook POSTs to `http://127.0.0.1:8787/state` with Clawd-compatible JSON. The bridge resolves and pushes. Claude Code hooks are fail-open — if the bridge is down, hooks return silently.

## Testing Decisions

### Testing philosophy

The primary test seam is the **WebSocket message**. All functional verification starts by sending a JSON message to the WS endpoint and observing the correct LVGL frame on the ESP32 display. This exercises the entire chain (network → deserialize → state machine → renderer → display) through a single externally observable behavior.

Unit-level seams (`PetBridge::callback`, `PetRenderer::playState`) are secondary and tested by isolating the component from its dependencies (mock WS message → verify callback; call playState → verify LVGL image crop offset).

Tests are verification steps performed at each commit slice, not a persistent test framework. The ESP32 has no local test runner.

### Test seams (priority order)

1. **WebSocket `/pet`**: Send `{"state":"thinking"}` via Node script or `wscat` → verify ESP32 serial log prints the parsed state, then verify LVGL frame row changes. This is the highest-value seam — one message exercises the full chain.
2. **HTTP `POST /state`**: `curl` to PC Bridge → verify 200 OK + WS message forwarding. Exercises PC Bridge ingress.
3. **PC Bridge priority resolution**: Send two concurrent sessions with different states → verify the higher-priority state wins on the WS output.
4. **PetBridge callback isolation**: Feed a mock `AgentStatus` struct → verify the registered callback fires with the correct status. Exercises the component boundary.
5. **PetRenderer row isolation**: Call `playState(ERROR)` → verify the renderer selects row 5 and begins frame cycling. Exercises the spritesheet mapping.

### Acceptance gates per phase

Each implementation phase gates on: (a) `idf.py build` passes, (b) flash + boot + serial log matches expected behavior for that phase's seam.

## Out of Scope

- BLE transport implementation (MVP uses WiFi only; BLE is a v1.1 stub in the transport interface)
- Permission touch approval (v1.1 feature; v1 is display-only from device to user)
- OTA firmware updates
- Audio/speaker feedback
- Battery/PMU power management (device runs on USB-C)
- Multi-device support (one PC Bridge → one ESP32 in v1)
- Custom pet creation tools (use existing Codex Pet community sprites)
- SoftAP WiFi provisioning (hardcoded credentials in v1)
- The 99_esp-brookesia Phone System itself is not modified — only a new App component is added

## Further Notes

Three reference projects were studied during the grilling phase; research notes are at `.scratch/ai-coding-pet/research-clawd.md`, `research-codexpet.md`, and `research-esp32.md`.

The 99_esp-brookesia Phone System lifecycle was hardened prior to this work (commit `a957c0c`). The hardening corrected 7 P0 defects and 2 hardening gaps in the Core's install, close, snapshot, and teardown paths. The Pet App relies on these corrected contracts.

Codex Pet spritesheet memory is a known risk: 1536×1872 RGBA ≈ 11.3 MiB exceeds ESP32 RAM. Solution: pre-cut spritesheet into individual 192×208 cell files (~160 KiB each), or stream-decode cells on demand via LVGL's decoder. This design decision is deferred to Phase 2 implementation research.
