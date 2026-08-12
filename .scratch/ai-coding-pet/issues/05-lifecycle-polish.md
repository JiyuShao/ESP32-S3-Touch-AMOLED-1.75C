# 05 — Lifecycle polish + visual refinement

**What to build:** 完整生命周期验证（pause/resume）、错误视觉区分、状态文字叠加、启动动画。确保两个 App（Squareline + Pet）并存的 Phone System 体验正确。

**Blocked by:** 04 (PC Bridge + Claude Code hook)

**Status:** ready-for-agent

## Acceptance criteria

- [ ] Home → `pause()` → 宠物动画冻结（停止 tick timer），WS 连接保持不重连
- [ ] Recents → Pet → `resume()` → 动画恢复 + WS 重连
- [ ] Recents → close Pet → `close()` → WS 断开 + LVGL 资源释放 + 状态回到 CLOSED
- [ ] ERROR 状态视觉区分：红框闪烁 + 错误标识
- [ ] DISCONNECTED 视觉：灰色半透明 overlay + 重连指示器
- [ ] 状态文字栏：底部 mini label 显示 "IDLE / THINKING / WORKING / ERROR"
- [ ] 启动动画：首次 `run()` → 宠物播放 waving row 一次 → 切到 idle
- [ ] Squareline + Pet 共存：正常切换、Recents 两个 snapshot、各自 close 不互相影响
- [ ] 4 MiB factory 分区空闲 ≥ 30%
- [ ] `idf.py build` 通过

## Out of scope (this ticket)

- Permission 触摸交互 → future ticket
- 外部 Codex Pet 社区素材导入 → 当前用自制占位帧
- WiFi provisioning UX → future ticket
