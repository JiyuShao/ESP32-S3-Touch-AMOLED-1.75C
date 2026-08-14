# Spec — 设备交互升级:Sessions 列表页 + 触摸权限审批

**Status:** ready-for-agent

> 取代原 ticket 07(滑动切 session 列表页);权限审批为原计划 ticket 08。本文是两者的合并规格,获批后重新拆 ticket。

## Problem Statement

ticket 06 之后,PC Bridge 与板端 PetBridge 已能感知并维护全部活跃 session,但设备仍是单页宠物——用户看不到 session 全景,宠物只是"状态显示器"。同时,Claude Code 的权限确认依然只能在终端里操作:实体设备就摆在桌上,却连"点一下批准"都做不到。对照 Clawd(HTTP permission hooks 远程批准)与 claude-desktop-buddy(BLE 推送审批、上半屏允许/下半屏拒绝),设备应从旁观者升级为可交互的控制面。

## Solution

设备交互分两层:

1. **Sessions 列表页**:宠物主页面向左滑动切到第二页,展示全部活跃 session(状态、项目名、更新时间,按优先级排序),实时刷新,右滑返回宠物页。
2. **触摸权限审批**:Claude Code 权限请求经 bridge 推送到板子,宠物进入 attention 动画 + 全屏审批浮层(工具名与提示),点上半屏=允许、下半屏=拒绝,决策回传 Claude Code;设备不在线或超时自动回退原生权限提示,绝不阻塞或静默拒绝。

## User Stories

1. 作为开发者,我想在宠物页向左滑动切换到 session 列表页,以便一眼看到所有活跃会话。
2. 作为开发者,我想在列表页向右滑动回到宠物页,以便继续看宠物动画。
3. 作为开发者,我想列表按优先级排序(error 最前、同级按更新时间新在前),以便先关注出问题的会话。
4. 作为开发者,我想每一行显示会话状态、项目名与更新时间,以便判断每个会话在做什么。
5. 作为开发者,我想列表实时刷新(新会话出现、状态变化、会话结束),而不需要任何手动操作。
6. 作为开发者,当没有任何活跃会话时,我想看到一个明确的空态提示,而不是空白页。
7. 作为开发者,我想列表页显示活跃会话计数,以便快速知道当前有几个会话在跑。
8. 作为开发者,我想列表页的滑动只响应屏幕中心区域,边缘滑动仍归 Phone Shell 导航手势,以便两个手势体系不冲突。
9. 作为开发者,当 Claude Code 有工具调用需要权限时,我想宠物立刻进入 attention 动画并显示审批浮层,以便不盯着终端也能注意到请求。
10. 作为开发者,我想审批浮层显示工具名与提示文本,以便在批准前确认这次请求是什么。
11. 作为开发者,我想点上半屏=允许、点下半屏=拒绝,以便手势明确、低头看设备时不易误触。
12. 作为开发者,我在板子上批准后,想浮层消失且宠物恢复之前的显示状态,以便交互闭环。
13. 作为开发者,当多个 session 同时有权限请求时,我想它们按到达顺序排队、一次只显示一个,以便互不覆盖。
14. 作为开发者,当设备不在线、不响应或超时(约 2.5 分钟)时,我想 Claude Code 自动回退到原生权限提示,以便工作流绝不因宠物而卡死或被静默拒绝。
15. 作为开发者,当权限请求已在别处(如桌面端超时回退后)被处理时,我想板子上的浮层自动清除,以便不留过期提示。
16. 作为开发者,权限挂起期间,我想显示状态锁定为 attention,不被任何 session 状态抢走,以便审批浮层始终醒目。
17. 作为开发者,权限决策不依赖任何固定 IP 或配对流程,我想它复用现有 hooks→bridge→WS 链路,以便零新增基础设施。
18. 作为开发者,当 bridge 不可达时,我想权限 hook 立即放行原生提示(fail-open),以便宠物缺席时 Claude Code 行为与未安装宠物完全一致。

## Implementation Decisions

### 协议(Clawd 信封的延伸,决策词表对齐 buddy)

- 新增三类 WS 消息,沿用 `version:"v1"` / `type` / `timestamp` 信封:
  - `permission`(bridge→板):携带 `permission_id`、`tool`、`hint`
  - `permission_response`(板→bridge):携带 `permission_id`、`decision`,decision 取 buddy 词表 `"once" | "deny"`(once = 允许一次)
  - `permission_resolved`(bridge→板):携带 `permission_id`,用于清除浮层(含"在别处已处理"场景)
- 这是板→bridge 的首个上行文本帧:板端 WS 客户端新增发送能力(客户端帧必须 mask);bridge 端现有"忽略文本帧"的分支改为解析 `permission_response`。
- bridge 新增 HTTP 端点:POST `/permission` 阻塞挂起直至收到决策或超时,响应 `{"decision":"allow"|"deny"|"ask"}`;`ask` 语义 = 回退 Claude Code 原生权限提示。挂起默认 150 秒。
- 权限挂起期间,bridge 强制 display 状态为 `attention`(优先于一切 session 主导解析);决策或超时后按现有规则重算 dominant。
- 权限请求按到达顺序排队;同一时刻只有一个活跃请求推送到板子,其余等待。

### Hook(Claude Code PermissionRequest 事件)

- 新增 PermissionRequest hook,matcher 覆盖全部工具,同步阻塞型(与现有 7 个 fire-and-forget 的 async hook 不同),超时设 300 秒(大于 bridge 的 150 秒挂起上限,保证 ask 回退先发生)。
- hook 收到权限请求后 POST `/permission` 并阻塞等待;拿到 decision 后按 Claude Code hook 输出协议返回 `permissionDecision`(`once`→`allow`,`deny`→`deny`);bridge 不可达或连接失败时立即返回 `ask`,零延迟放行原生提示。
- 现有 7 个状态事件 hook 不改动。

### 板端

- PetBridge 状态模型新增 pending permission(解析 `permission` / `permission_resolved` 消息,维护单个活跃请求),暴露只读访问器;决策由 UI 层通过 WS 客户端发送上行文本帧。
- 列表页数据源直接复用现有 session 列表访问器(06 已交付);UI 以固定周期轮询渲染,不引入事件总线。
- 手势:屏幕中心区域手写滑动判定(按下→抬起,水平位移阈值 ~60px);边缘区域手势不处理,留给 Phone Shell(导航手势默认开启)。
- 审批浮层:全屏两个点击区(上半=允许、下半=拒绝),配 attention 动画与工具名/hint 文案;浮层存在期间屏蔽页面滑动手势。
- 页面模型:宠物页 ⇄ 列表页两页切换;App back 时恢复宠物页;浮层是宠物页与列表页之上的临时层。

## Testing Decisions

- 好测试的标准:只测外部行为——消息语义、状态转换、排序、超时与回退——不测内部实现细节。
- 三层 seam(高→低):
  1. **bridge 全链路(PC host,最高 seam)**:扩展 WS 客户端测试,让 fake WS client 扮演板子——POST `/permission` 挂起 → 断言收到 `permission` 推送与 display=attention → 回 `permission_response` → 断言挂起响应返回 allow;超时路径(用短超时环境变量)断言返回 ask;断言 `permission_resolved` 推送与队列顺序。
  2. **板端解析器 host 测试**:`permission` / `permission_resolved` 消息 → pending 状态转换断言,沿用现有纯 C++ 无硬件测试模式。
  3. **真机点按验证**:真实权限请求(或 curl 模拟)触发浮层,点按上/下半屏,串口日志确认决策回传与浮层清除;列表页验证复用 06 的串口模式(渲染日志断言双会话可见、排序正确)。
- Prior art:`priority-test.js`(纯逻辑单测)、`simulate-session.sh`(端到端日志断言)、`ws-client-test.js`(隔离端口自建 bridge 的 raw socket 测试)、PetBridge host test(g++ 编译、无框架断言)。

## Out of Scope

- 快速批准的心形动画、每 50K tokens 庆祝动画(buddy heart/celebrate,需要新显示状态与美术)
- 摇晃=晕眩、扣着=睡觉等 IMU 手势
- 权限规则持久化(always allow / deny 规则)
- transcript 推送与滚动浏览(当前 hook 链路拿不到 transcript 数据)
- 远程批准(Telegram/Feishu/手机 PWA)
- GIF 角色包推送

## Further Notes

- 参考来源:Clawd 的 HTTP permission hooks(通道形态与 fail-open 原则)、claude-desktop-buddy(上半屏允许/下半屏拒绝、`once`/`deny` 词表、attention 置顶、超时回退)、本仓 06 已落地的 Clawd 信封与 session 字段。
- PermissionRequest hook 是同步阻塞型,与现有 async hook 的 settings 合并必须保留两者的 async 差异;hook 超时(300s)必须大于 bridge 挂起上限(150s)。
- 板端首次具备上行发送能力,注意 WS 客户端帧掩码与 bridge 文本帧解析分支的对接;上行能力将来可复用于其他交互(如输入确认)。
- 本 spec 获批后,按新拆分重新 to-tickets(原 07 ticket 文件被取代)。
