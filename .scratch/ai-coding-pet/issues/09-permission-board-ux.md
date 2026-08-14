# 09 — 板端权限审批 UX

**What to build:** 板子收到 `permission` 消息后进入 attention 动画 + 全屏审批浮层(工具名 + 提示文案,上半屏=允许、下半屏=拒绝),点按后上行 `permission_response`;浮层期间屏蔽页面滑动手势;收到 `permission_resolved` 自动清除。用户在板子上点一下即可完成 Claude Code 权限审批,无需触碰终端。

**Blocked by:** 08 — PC 端权限生命周期(hook + bridge)(需要真实 bridge 上线做集成演示)。

**Status:** ready-for-agent

## Acceptance criteria

- [ ] 板端解析 `permission` / `permission_resolved` 消息,维护单个活跃权限请求(host 测试覆盖状态转换,含超时后回退语义)
- [ ] 浮层:上半屏点击区=允许(once)、下半屏=拒绝(deny),显示工具名与 hint;attention 动画播放中
- [ ] 点按后经 WS 上行 `permission_response`(客户端帧必须 mask);浮层清除、显示状态恢复原主导状态
- [ ] `permission_resolved` 到达时清除浮层(在别处已处理的场景)
- [ ] 浮层存在期间屏蔽页面滑动手势,边缘手势仍归 shell
- [ ] 真机串口验证:curl 模拟权限请求挂起 → 板子浮层出现 → 点按上半屏 → bridge 日志显示 allow 决策回传;点按下半屏 → deny;超时 → ask 且浮层清除

## 实现备注

- 板端 WS 客户端首次具备上行发送能力(此前仅接收);bridge 的文本帧解析分支由 08 交付。
- 浮层是宠物页/列表页之上的临时层,不改变页面模型;决策后恢复 07 的页面状态。
