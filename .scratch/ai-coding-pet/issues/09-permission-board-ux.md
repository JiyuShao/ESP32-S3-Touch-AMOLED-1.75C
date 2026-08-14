# 09 — 板端权限审批 UX

**What to build:** 板子收到 `permission` 消息后进入 attention 动画 + 全屏审批浮层(工具名 + 提示文案,上半屏=允许、下半屏=拒绝),点按后上行 `permission_response`;浮层期间屏蔽页面滑动手势;收到 `permission_resolved` 自动清除。用户在板子上点一下即可完成 Claude Code 权限审批,无需触碰终端。

**Blocked by:** 08 — PC 端权限生命周期(hook + bridge)(需要真实 bridge 上线做集成演示)。

**Status:** done（2026-08-14 真机验证全链路：curl POST /permission → 浮层出现(工具名+hint) → 点上半屏 → bridge 日志 allow、HTTP 返回 {"decision":"allow"}；点下半屏 → deny、HTTP 返回 {"decision":"deny"}；超时 → ask（真实 bridge 150s 日志）；host 测试 13 断言含断连清权路径）

## Acceptance criteria

- [x] 板端解析 `permission` / `permission_resolved` 消息,维护单个活跃权限请求(host 测试覆盖状态转换,含超时后回退语义)
- [x] 浮层:上半屏点击区=允许(once)、下半屏=拒绝(deny),显示工具名与 hint;attention 动画播放中
- [x] 点按后经 WS 上行 `permission_response`(客户端帧必须 mask);浮层清除、显示状态恢复原主导状态
- [x] `permission_resolved` 到达时清除浮层(在别处已处理的场景)
- [x] 浮层存在期间屏蔽页面滑动手势,边缘手势仍归 shell
- [x] 真机串口验证:curl 模拟权限请求挂起 → 板子浮层出现 → 点按上半屏 → bridge 日志显示 allow 决策回传;点按下半屏 → deny;超时 → ask 且浮层清除

## 实现备注

- 板端 WS 客户端首次具备上行发送能力(此前仅接收);bridge 的文本帧解析分支由 08 交付。
- 浮层是宠物页/列表页之上的临时层,不改变页面模型;决策后恢复 07 的页面状态。
- `permission_response` 帧有意省略 `timestamp`:`SEND_TEXT_MAX`=125 字节上限内放不下(带 UUID 的完整信封 ~145B),且 bridge 只读 type/permission_id/decision;上线词表 once/deny。
- AC「边缘手势仍归 shell」按 spec-device-interactions 修正后的现实处理:本构建 `LV_USE_GESTURE_RECOGNITION=0`,shell 边缘导航手势本身无效,浮层不设 EVENT_BUBBLE 吞掉全部点击(含边缘)即为预期行为。
- 验证中发现的链路加固(79b3c4a):WiFi 抖动断连时板端清浮层(链接没了答案送不回去),bridge 在 WS 重连时重推挂起的 permission;permission-test 的 finish 门控混用 WS/HTTP 双 socket 事件导致负载下偶发超时,改为任一事件到达即求值。
- hint 跨端契约:bridge 发送前剥离引号/转义(b373497),板端 extractString 遇 `\` 即停。
