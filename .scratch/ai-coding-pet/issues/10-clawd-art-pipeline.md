# 10 — Clawd 美术管线:SVG 动画 → 设备像素帧

**What to build:** 实现 `tools/pet-assets/gen_clawd_frames.py`,把 8 个 Clawd CSS 动画 SVG 采样成 LVGL 像素帧(复用 `pet_frames.h/c` 格式),替换板子当前使用的 airi spritesheet 美术。设备侧 `pet_render` 不改。

**Blocked by:** None — can start immediately. 素材已由 186a0c3 入库(`tools/pet-assets/clawd/`,AGPL-3.0,NOTICE 已引用本脚本)。

**Status:** done(2026-08-15 真机验证:5 状态动画 + wake intro 全部按 8 帧×300ms 播放,完整螃蟹含腿、无滚动条、深色背景融合;列表页卡片化 + 任务详情字段;期间修复 3 个链路 bug:浏览器截图滚动条烘焙、WS 解析器 NUL 写坏下一帧、WS 任务优先级饿死)

## 背景

协议栈(POST /state、12 状态词表、priority、session 列表、permission once/deny)全部对齐 Clawd,但设备显示的仍是 Codex Pet 的通用 airi 精灵图,状态映射是凑的(thinking→waiting 行、working→running 行)。换成 Clawd 动画后语义一一对应:思考=气泡、打字=敲键盘、happy=挥手、error=报错、wake=醒来。

Clawd 的 SVG 是 **CSS `@keyframes` 动画**(12s 循环为主),不是 spritesheet。Pillow 只能画 t=0。生成管线需要沿动画时间轴采样。

## 技术路线(已验证)

1. **时间定位**:给 SVG 追加 `<style>*{animation-delay:-Tms !important}</style>` 再截图 —— 负 delay 让所有动画元素直接跳到周期 T 处。无需 CDP、无需浏览器自动化,已用 Edge headless 验证(t=0 与 t=2500 帧不同)。
2. **渲染**:headless Edge(Windows 默认 `C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe`)/ Chrome(mac,路径可 `--browser` 指定):`--headless --disable-gpu --window-size=500,500 --default-background-color=00000000 --screenshot`。透明背景已验证。
3. **后处理**:PIL 裁透明 bbox → 等比缩放到 96×104 内 → 合成到屏幕底色(SCREEN_BG 0x1A1A1A)→ RGB565 → 输出 `pet_frames.h/c`(与 gen_frames.py 输出格式完全一致)。

## 状态映射

| 显示状态 | SVG | 循环 |
|---|---|---|
| idle | clawd-idle-follow.svg | loop |
| thinking | clawd-working-thinking.svg | loop |
| working | clawd-working-typing.svg | loop |
| attention | clawd-happy.svg | loop |
| error | clawd-error.svg | one-shot(播一次停最后一帧,与现有 error 行为一致) |
| intro | clawd-wake.svg | one-shot(启动挥手,替代 airi waving row) |

未接入:`clawd-static-base.svg`(无动画的基座)、`clawd-collapse-sleep.svg`(sleeping 状态;当前 sleeping 仍映射 idle 行,若后续给 pet_render 增加 SLEEPING 显示状态再接——记入后续)。

## 采样策略

- 每状态 ≤4 帧(现有 `PET_MAX_FRAMES=4` 格式上限),均匀分布在一个动画周期内。周期从各 SVG 的 `animation-duration` 解析(默认 12s)。
- 视觉决定:**不用** airi 的蓝色圆角卡片,宠物直接站在屏幕底色上(更接近 Clawd on Desk 的桌面形态)。生成脚本加 `--card` 可选参数保留旧卡片样式,便于对比。

## Acceptance criteria

- [ ] `gen_clawd_frames.py` 无第三方运行时依赖地跑通(仅 Python 标准库 + PIL + 系统浏览器)
- [ ] 生成 `pet_frames.h/c` 替换 `pet_render/assets/`,pet_render 代码零改动
- [ ] 5 个显示状态 + intro 各 2-4 帧,每帧 96×104 RGB565,与 SVG 采样时间对应(脚本同时输出 PNG 预览供人工核对)
- [ ] 构建 + 烧录 + 板上验证:5 状态动画切换正确、intro 只播一次、error 播一次停住
- [ ] 帧数/体积不超现有约束(每帧 ~20KB,总帧数 ≤24,8M 分区余量充足)

## 实现备注

- 浏览器进程逐帧启动(8 个采样 ≈ 8-20 次启动,每次 ~1-2s),总量分钟级,可接受;后续若要提速可用 CDP 复用单进程。
- 临时 SVG/PNG 写到系统临时目录,不污染仓库。
- 负 delay 注入对 `animation-iteration-count: finite` 的元素:delay 超过周期后呈现 fill-mode 结束态,正好用于 one-shot 采样尾帧。
