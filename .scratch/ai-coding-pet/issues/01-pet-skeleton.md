# 01 — Pet App skeleton + static pet on screen

**What to build:** 用户从 Launcher 点击 Pet 图标，屏幕显示一张硬编码的宠物 idle 静态帧。不涉及网络、不涉及动画，只验证 App 注册 + LVGL 渲染通路。

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

## Acceptance criteria

- [ ] `sdkconfig.defaults` 开启 WiFi STA + lwIP DHCP + esp_websocket_client（一次改完，后续 ticket 不改 sdkconfig）
- [ ] `main/idf_component.yml` 添加 `mdns` 和 `esp_websocket_client`（如果需要 managed component）
- [ ] `components/pet_app/` 作为独立 component：`CMakeLists.txt` + `WHOLE_ARCHIVE` 注册
- [ ] `pet_app.cpp` 继承 `phone::App`，实现 `init()` / `run()` / `close()` 最小生命周期
- [ ] `run()` 中创建 `lv_image`，显示一张硬编码 RGB565 占位图（120×130，居中或原始大小）
- [ ] Launcher 图标：用任意占位 icon（112×112 或复用 Squareline 格式）
- [ ] `idf.py build` 通过 → 烧录 → Launcher 两个图标（Squareline + Pet）→ 点 Pet → 屏上显示占位图像
- [ ] `back()` 回到 Launcher
- [ ] 4 MiB factory 分区空闲 ≥ 35%
