# 05 — Lifecycle polish + visual refinement

**What to build:** 完整生命周期验证（pause/resume）、错误视觉区分、状态文字叠加、启动动画。确保两个 App（Squareline + Pet）并存的 Phone System 体验正确。

**Blocked by:** 04 (PC Bridge + Claude Code hook)

**Status:** done (2026-08-13; build 通过，0x388740 bytes @ 8M factory = 56% free；上板视觉验收待用户连接板子后执行)

## Acceptance criteria

- [x] Home → `pause()` → 宠物动画冻结（`lv_timer_pause(_tick_timer)`），WS 连接保持
- [x] Recents → Pet → `resume()` → `lv_timer_resume` + 立即 `pollState()` 追赶状态
- [x] Recents → close Pet → `close()` → WS 断开 + `_renderer.deinit()` 释放全部 LVGL 对象（shell 不替 App 清理，需自查自删）+ 状态回到 CLOSED
- [x] ERROR 状态视觉区分：红框闪烁（400ms）+ "!" badge
- [x] DISCONNECTED 视觉：黑色 LV_OPA_50 半透明 overlay + 闪烁 "Reconnecting..." label
- [x] 状态文字栏：底部 label 显示 "IDLE / THINKING / WORKING / ATTENTION / ERROR / DISCONNECTED"
- [x] 启动动画：首次 `run()` → waving row 一次（4 帧 × 150ms）→ 回落到当前状态（`_intro_played` 保证每次 boot 只播一次）
- [x] Squareline + Pet 共存：App 自管 LVGL 对象生命周期，close 后 run 重建无泄漏（上板视觉确认待用户）
- [x] factory 分区空闲 ≥ 30%：分区从 4M 扩到 8M，binary 0x388740 → 56% free
- [x] `idf.py build` 通过

## 上板验证（用户侧，待板子连接）

- Launcher → Pet 首启播放 waving intro → 切 idle
- 状态动画逐项目测：idle/thinking/working/attention/error
- Home 切出 → 动画冻结；Recents 切回 → 恢复
- Recents close Pet → 再开 → 无残影/重影

## Out of scope (this ticket)

- Permission 触摸交互 → future ticket
- 外部 Codex Pet 社区素材导入 → 当前用自制占位帧
- WiFi provisioning UX → future ticket
