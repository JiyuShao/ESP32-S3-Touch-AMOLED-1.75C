# 08 — PC 端权限生命周期(hook + bridge)

**What to build:** Claude Code 的权限请求经 PermissionRequest hook 同步阻塞 POST 到 bridge 并挂起;bridge 向板子推送 `permission`(permission_id/tool/hint),解析板子上行的 `permission_response`(decision: once/deny),挂起期间强制 display=attention,决策或超时(150s)后向 hook 返回 allow/deny/ask。设备缺席、不响应或超时一律 ask 回退原生权限提示,绝不阻塞或静默拒绝。

**Blocked by:** None — can start immediately.

**Status:** done（2026-08-14 host + e2e 测试全绿：全链路 allow/deny、排队顺序、超时 ask、真实 hook 进程输出 permissionDecision 断言、06 回归通过；settings.json 已合并第 8 条同步 hook）

## Acceptance criteria

- [x] PermissionRequest hook(matcher 覆盖全部工具,**同步阻塞型**,超时 300s):POST /permission 并挂起等待;拿到决策后按 hook 输出协议返回 `permissionDecision`(once→allow,deny→deny);bridge 不可达立即返回 ask(fail-open)
- [x] bridge 新增 POST /permission:阻塞响应直至决策或超时,响应 `{"decision":"allow"|"deny"|"ask"}`;挂起默认 150s(环境变量可调,须小于 hook 的 300s)
- [x] WS 消息:`permission`(bridge→板)、`permission_resolved`(bridge→板);bridge 解析板→bridge 的 `permission_response` 文本帧(此前文本帧被忽略,现改为解析)
- [x] 挂起期间 display 强制 attention(优先于一切 session 主导解析);决策或超时后按现有规则重算 dominant
- [x] 排队:多个权限请求按到达顺序处理,同一时刻只推送一个活跃请求
- [x] 测试(host):fake WS client 扮演板子完成全链路(POST /permission 挂起 → 收到 permission 推送且 display=attention → 回 permission_response → 挂起响应返回 allow);短超时路径返回 ask;permission_resolved 推送与排队顺序断言
- [x] 端到端脚本:真实 hook 进程 + fake 板子自动批准 → hook stdout 输出 permissionDecision=allow;fake 板子缺席 → ask
- [x] 现有 7 个事件 hook 与 06 协议回归不受影响(全量测试套件通过)

## 实现备注

- 与现有 async hook 的 settings 合并必须保留 async 差异:PermissionRequest 一条为同步,其余 7 条 async 照旧。
- 参考:Clawd HTTP permission hooks(通道形态与 fail-open)、buddy 的 once/deny 词表与超时回退。
