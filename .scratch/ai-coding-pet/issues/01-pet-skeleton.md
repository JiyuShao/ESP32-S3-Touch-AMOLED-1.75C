# 01 — Pet App skeleton + static pet on screen

**What to build:** 用户从 Launcher 点击 Pet 图标，屏幕显示一张硬编码的宠物 idle 静态帧。不涉及网络、不涉及动画，只验证 App 注册 + LVGL 渲染通路。

**Blocked by:** None — can start immediately.

**Status:** done (2026-08-13, board-verified)

## Acceptance criteria

- [x] WiFi driver + lwIP + DHCP 已随 IDF 默认开启（sdkconfig 确认 `CONFIG_ESP_NETIF_*`）；esp_websocket_client 依赖留给 ticket 02
- [x] `components/ai_coding_pet/` 独立 component：`CMakeLists.txt` + `WHOLE_ARCHIVE` 注册
- [x] `ai_coding_pet.cpp` 继承 `phone::App`，实现 `run()` / `back()` / `close()` / `pause()` / `resume()`
- [x] `run()` 显示硬编码占位宠物（LVGL 圆角矩形 + 眼睛 + 笑脸 + 标签）
- [x] Launcher 图标：112×112 ARGB8888 自制图标
- [x] `idf.py build` 通过 → 烧录 → Launcher 两个图标（Squareline + AI Coding Pet）→ 点 Pet → 屏上显示占位图
- [x] `back()` 回到 Launcher（板级验证通过）
- [x] 4 MiB factory 分区空闲 37% ≥ 35%

## Notes

- 显示 DMA underflow 修复：BSP `use_psram=false`（见 `patches/0001-*.patch`），已提交 2199b63
