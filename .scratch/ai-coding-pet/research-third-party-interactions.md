# 第三方实现调研：状态采集与用户交互

> 2026-08-13 调研。来源：GitHub 一手 README/仓库页面。
> 对比对象：Clawd on Desk（rullerzhou-afk/clawd-on-desk）、Codex Pet 素材仓库（lencx/pet）。

## Clawd on Desk

Electron 桌面像素宠物（Win/macOS/Linux），AGPL-3.0。三个内置主题 + 自定义主题 + Codex Pet 素材导入。

**状态采集机制**：
- Claude Code：command hooks + HTTP permission hooks（后者用于权限截获）
- Codex CLI：官方 hooks，另有 **JSONL 日志回退**——读 `~/.codex/sessions/` 的 session 日志
- Cursor：IDE hooks（`~/.cursor/hooks.json`，通过 Settings → Agents 或 npm 脚本安装）
- 另有 ~20 个其他 agent（Copilot CLI、Gemini CLI、Qwen Code 等）通过可选 hooks/插件/config 接入；部分仅 state-only（只报生命周期，不截权限）

**多 session 处理**：
- 每个 agent session 独立跟踪，跨 session 解析最高优先级状态（与我们 bridge 同语义）
- **subagent 数量驱动动画**：1 个 subagent → 戴耳机律动；≥2 → 三球杂耍
- sessions dashboard + 紧凑 HUD 查看活跃 session
- 进程存活检测清理孤儿 session（agent 崩溃/退出时）

**用户交互清单**：
- 点击：双击戳宠物、连点 4 次慌乱
- 拖拽：任意状态可抓取（pointer capture 防脱手）；迷你模式贴屏幕边缘 + 悬停 peek
- 眼动/睡眠：idle 时视线跟随光标；~60 秒无操作入睡，鼠标靠近惊醒
- **权限批准**：eligible agent 的权限请求弹浮动气泡，一键 allow/deny（部分 agent 支持 always/rules）；全局热键（Ctrl+Shift+Y/N）；请求堆叠布局；可选 Telegram / Feishu/Lark 远程批准
- 通知音效：完成与权限提示短音效（10 秒冷却）；notification/attention 状态可自动聚焦对应终端窗口
- 其他：托盘菜单 DND 睡眠、自动启动、自动更新、quota 环、LAN-token 只读移动端 PWA 镜像、点击穿透（只有宠物本体可交互）

## Codex Pet 素材仓库（lencx/pet）

**只是素材分发仓库**（MIT，12 stars）：`codex/<pet-id>/` 下放 `pet.json`（元数据）+ `spritesheet.webp`（动画帧），一段托管 shell/PowerShell 脚本拷进 `${CODEX_HOME:-~/.codex}/pets/`。交互为零——全部行为由 Codex 自己的宠物渲染器决定。对我们是纯美术来源（airi 素材即来自这里）。不支持 Claude Code。

## 可借鉴清单（结合 ESP32 实体设备形态）

| 交互 | 出处 | 我们的形态 |
|---|---|---|
| HTTP permission hooks 截获权限请求 → 一键 allow/deny | Clawd | 实体设备天然是「远程批准终端」（Clawd 要靠 Telegram 实现，我们板子即终端）——Permission ticket 核心参考 |
| subagent 数量 → 动画（1 律动 / ≥2 杂耍） | Clawd | bridge 广播加 subagent_count 字段即可，juggling 优先级我们已有 |
| 进程存活检测清理孤儿 session | Clawd | 可替代/补充我们 TTL 的「长工具调用误过期」问题 |
| 触摸交互：戳、连点慌乱、闲置睡觉 + 触摸唤醒 | Clawd | 触摸屏直接映射（眼动跟随不适用，可做视线朝随机方向漂移） |
| 完成/权限提示音效 | Clawd | 板子有 ES8311 codec |
| 日志回退（hooks 失败时读 session 日志） | Clawd(Codex) | 我们纯 hooks fail-open，可作后续增强 |

不适用项：拖拽/屏幕边缘吸附、窗口聚焦、PWA 镜像、点击穿透（桌面窗口专属）。

## claude-desktop-buddy-esp32（vthinkxie，官方 buddy 的 Waveshare 板移植）

Anthropic 官方 claude-desktop-buddy（原 M5StickC）的移植，**支持我们这块 1.75C 圆屏**。BLE Nordic UART 接入 Claude 桌面端（Cowork / Claude Code）的 developer mode「Hardware Buddy」，非官方支持特性。架构：单 main.cpp + 每板 capability flags；逻辑画布 184×224，各板缩放。

**状态模型**（比我们的 6 态更粗）：sleep（断链，闭眼呼吸）/ idle（连接，眨眼）/ busy（running>0，冒汗）/ attention（waiting>0，红胶囊置顶脉冲）/ celebrate（每 50K tokens 撒花）/ dizzy（摇晃）/ heart（5 秒内批准）。heartbeat snapshot 每 10 秒 + 状态变更即发，含计数 `total/running/waiting`、最近消息 `entries`（新的在前）、内嵌 `prompt{id,tool,hint}`（仅当有待决审批）；30 秒无 snapshot 判断链。审批回包 `{"cmd":"permission","id":...,"decision":"once"|"deny"}`，id 必须精确匹配。turn 事件每回合一次性（>4KB 丢弃）。角色包走 folder push：char_begin/file/chunk(base64)/file_end/char_end 逐步 ack，1.8MB 上限。

**交互清单**：
- 按键为主：A=确认/下一页（长按开菜单），B=拒绝/滚动 transcript（1s 熄屏/6s 关机）；上下文相关，9 个页面（时钟/pet/info/审批等）上下滑切换
- 触摸为辅：审批页点上半屏=同意、点下半屏=拒绝；HUD 点宠物=爱心；底部 32px 搓 transcript；时钟页左右滑换 ASCII 物种（18 种）
- 手势：摇晃=晕；扣着=睡觉回能量（能量槽）
- 18 ASCII 物种 × 7 状态动画 + GIF 角色包（BLE 推流）；CJK 专用字体渲染 transcript
- 配对：6 位 PIN + LE Secure Connections 加密
- 无 LED（注意力=屏幕红胶囊）、无音效（硬件有 codec 未用）

**对我们可借鉴**（结合 ESP32 实体设备形态）：

| 交互 | 出处 | 我们的形态 |
|---|---|---|
| 审批页点上半屏=同意、下半屏=拒绝 + 5s 内批准→爱心 | buddy | 我们 1.75C 全触摸，比按键方案更自然——Permission ticket（08）交互直接可用 |
| 审批内嵌 heartbeat 而非独立消息类型 | buddy | 我们的 snapshot/state 也可加 `prompt` 字段，比 Clawd 的独立 HTTP 通道简单 |
| 计数驱动状态 total/running/waiting | buddy | bridge 已有 session 表，加计数即可（07 列表页标题「N 个活跃」） |
| 断链判定 30s 心跳 | buddy | 我们 WS 无 keepalive，bridge 可 10s 心跳 |
| 摇晃=晕 / 扣着=睡觉 | buddy | 板子有 QMI8658 IMU，ticket 09+ 可做 |
| 每 50K tokens 庆祝 | buddy | 我们的 milestone 动画候选 |
| 底部搓动看 transcript | buddy | 我们拿不到 transcript（hooks 无此数据）；搓动手势本身 07 可复用 |
| GIF 角色包 BLE 推流 | buddy | 我们走 WiFi/WS，角色包可走 WS 分块（远期） |

不适用项：BLE/developer mode 通道（我们 hooks+WiFi 更通用）、9 页面 HUD（我们 brookesia 双页）、ASCII 物种切换。
