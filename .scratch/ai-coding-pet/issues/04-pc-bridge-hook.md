# 04 — PC Bridge + Claude Code hook

**What to build:** PC Bridge daemon 做 Clawd priority resolution，Claude Code hook 脚本自动 POST 状态。用户正常使用 Claude Code → pet 自动切换动画，无需手动操作。

**Blocked by:** 03 (State → animation pipeline)

**Status:** ready-for-agent

## Acceptance criteria

- [ ] PC Bridge: `tools/pet-bridge/priority.js` — Clawd 12-state priority 表 + `resolveDominantState(sessions)`
- [ ] PC Bridge: session 5 分钟无更新 → auto-remove
- [ ] PC Bridge: 多 session 聚合——两个 session (thinking + working) → display state = WORKING
- [ ] PC Bridge: session error 优先级最高——error + working → display ERROR
- [ ] PC Bridge: `POST /state` 只接受有效 state 值，返回 200/400
- [ ] Claude Code hook: `tools/pet-bridge/hooks/claude-code-hook.js`
- [ ] Hook 安装: `~/.claude/settings.json` → UserPromptSubmit/PreToolUse/PostToolUse/Stop/SubagentStart
- [ ] Hook 行为: PreToolUse → `state:"working"`, PostToolUse → `state:"thinking"`, Stop → `state:"idle"`, SubagentStart → `state:"working"`
- [ ] Hook fail-open: bridge 不在时 hook 静默跳过
- [ ] 端到端验证: 用户在 Claude Code 中发 prompt → pet 切 THINKING → 执行 tool → pet 切 WORKING → 完成 → pet 切 IDLE
- [ ] `idf.py build` 通过（ESP32 侧无改动）

## 验证脚本

- [ ] `tools/pet-bridge/test/simulate-session.sh` — 模拟两个 session 的状态序列来验证 priority resolution
