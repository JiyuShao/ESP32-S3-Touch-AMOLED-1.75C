# 04 — PC Bridge + Claude Code hook

**What to build:** PC Bridge daemon 做 Clawd priority resolution，Claude Code hook 脚本自动 POST 状态。用户正常使用 Claude Code → pet 自动切换动画，无需手动操作。

**Blocked by:** 03 (State → animation pipeline)

**Status:** done (2026-08-13; hook 安装方式改为文档化，由用户自行合并到 ~/.claude/settings.json)

## Acceptance criteria

- [x] PC Bridge: `tools/pet-bridge/priority.js` — Clawd priority 表 + `resolveDominantState(sessions)`（含 tie 规则与未知 state 处理）
- [x] PC Bridge: session 5 分钟无更新 → auto-remove（`PET_BRIDGE_SESSION_TTL_MS` 可覆盖，供测试）
- [x] PC Bridge: 多 session 聚合——thinking + working → WORKING
- [x] PC Bridge: session error 优先级最高——error + working → ERROR
- [x] PC Bridge: `POST /state` 只接受有效 Clawd state 值，返回 200/400
- [x] Claude Code hook: `tools/pet-bridge/hooks/claude-code-hook.js`（fail-open，实测：PreToolUse→working、Stop→idle、坏 stdin 静默退出 0）
- [x] Hook 安装: 用户拒绝直接修改其 `~/.claude/settings.json` → 改为 `tools/pet-bridge/README.md` 提供完整 snippet（UserPromptSubmit/PreToolUse/PostToolUse/Stop/SubagentStart，async + timeout 5），待用户自行合并
- [x] Hook 行为: PreToolUse → `state:"working"`, PostToolUse → `state:"thinking"`, Stop → `state:"idle"`, SubagentStart → `state:"working"`, UserPromptSubmit → `state:"thinking"`
- [x] Hook fail-open: bridge 不在时 hook 静默跳过
- [x] 端到端验证: `simulate-session.sh` 通过（双 session 竞态 + TTL 过期 + 400 校验）；真实 Claude Code → pet 链路待用户安装 hook + 填 WiFi 凭据后验证
- [x] `idf.py build` 通过（ESP32 侧无改动 — 未重新构建，ticket 03 构建仍有效）

## 验证脚本

- [x] `tools/pet-bridge/test/simulate-session.sh` — 双 session 状态序列验证 priority resolution + TTL + 400
- [x] `tools/pet-bridge/test/priority-test.js` — priority 单元检查
- [x] `tools/pet-bridge/test/ws-client-test.js` — 改为自包含（自带 POST），对重写后的 bridge 回归通过
