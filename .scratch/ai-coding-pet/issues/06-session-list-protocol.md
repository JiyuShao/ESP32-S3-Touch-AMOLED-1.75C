# 06 — Clawd 对齐的 session 列表协议

**What to build:** PC Bridge 从「只推 dominant 状态」升级为「推送全部活跃 session」，板子 PetBridge 解析并维护 session 列表。用户在串口日志即可验证多会话已被板子感知（界面在 07）。

**Blocked by:** None — can start immediately.

**Status:** done（2026-08-14 串口验证通过：板子日志显示 4 个 session、完整 UUID、按 priority 排序、live 更新）

## Acceptance criteria

- [x] Wire 协议信封对齐 Clawd（mobile-protocol-v1）：所有下发消息含 `version:"v1"` / `type` / `timestamp`
- [x] 连接时下发 `snapshot`（全量 sessions + 解析后的 dominant 显示态），之后 `state`（单 session 增量更新）与 `session_deleted`（会话离开缓存）
- [x] session 记录字段：`session_id` / `basename`（cwd 最后一段）/ `state` / `updated_at`；**不包含 agent_id**（单 agent 时代无信息量，多 agent 时再加）
- [x] hook 新增 `SessionStart→idle`、`SessionEnd→sleeping` 事件映射；POST 携带 cwd 的 basename；从 payload 删除 `agent_id`
- [x] 会话清理：`SessionEnd→sleeping` 从活跃列表移除；TTL 兜底——idle 5 分钟、非 idle 30 分钟（长工具调用不误过期）
- [x] 板子 PetBridge 解析三类消息并维护 session 列表（沿用现有手写解析风格，无动态内存）
- [x] 测试：`simulate-session.sh` 扩断言（session_deleted、非 idle TTL 豁免）；`ws-client-test.js` 隔离端口自建 bridge（snapshot/state/display 全断言）；`priority-test.js` 通过；PetBridge host test 覆盖三类消息 + 同态消息刷新超时
- [x] `idf.py build` 通过 + 烧录后串口验证（板子日志显示 4 条 session、完整 36 字符 UUID、priority 排序、error→working 重排序 live 可见）

## 实现备注

- 板端 `PetBridge::onWsMessage` 对任何有效消息刷新 `last_update_ms`（不触发回调）——display 状态不再变化时长工具调用期间宠物不会误入 IDLE（host test 断言覆盖）。
- `bridge.js` 支持 `PET_BRIDGE_PORT` env，`ws-client-test.js` 自建 18787 隔离实例，避免真实 daemon（8787）上本 session hook 流量干扰断言。
- 串口验证揪出两个板端 bug 并已修复（本 ticket 的「板子日志验证」验收项正是为此设计）：
  1. **ws_client 帧流错位**：`read_until_headers_end` 吞掉与 101 响应同段到达的首个 WS 帧字节 → snapshot 前缀丢失后整个 rx 流永久错位、全部消息静默（旧协议消息小只丢第一条，从未暴露）。修复：`header_end` 出参 + 残留字节转入 rx 缓冲。
  2. **UUID 截断**：`session_id[32]` 装不下 36 字符 UUID → 扩到 37；同时 RX_BUF_SIZE 1024→2048（8 个全尺寸 session 的 snapshot 约 1.4KB）+ ws_client 任务栈 4096→6144。
- **DHCP 漂移**：Mac 的 LAN IP 从 192.168.124.4 变为 .2 导致板子连不上（无任何症状，只有 5s 重试警告）。已改 `pet_bridge_config.h` 重烧。长期建议路由器 DHCP 保留或 mDNS。
- daemon 已用新协议代码运行（`nohup node bridge.js > /tmp/pet-bridge.log 2>&1 &`）。
