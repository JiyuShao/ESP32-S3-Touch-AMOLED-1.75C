# `99_esp-brookesia` 架构

## 范围与平台定位

`99_esp-brookesia` 是面向 ESP32-S3-Touch-AMOLED-1.75C Board 的 ESP-IDF firmware example，也是正在优化中的 **Brookesia App Platform** 基线。它的职责是提供一个可运行和开发多个静态 Brookesia `Phone App` 的 Phone System；当前实现不是具备动态安装、OTA、文件系统和完整共享系统服务的 `Device OS`。

当前平台只有一个产品侧注册 App：`Squareline`。它是 **Squareline Reference App**，用于验证 App 注册、Phone System 生命周期和 UI 集成方式；它不是平台本身。研究和规划材料中的未来 App、网络服务、传感器服务和云端能力不属于当前实现事实。

## 版本与配置身份

当前 tracked source 直接声明：

- 本地 `brookesia_core` component 版本为 `0.6.0-beta2`：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/idf_component.yml:1-5`。
- 99 直接依赖 LVGL `9.5.0` 与 Waveshare BSP `^3.0.0`：`examples/esp-idf/99_esp-brookesia/main/idf_component.yml:1-6`。
- 运行配置面向 `esp32s3`、32 MiB flash、Octal PSRAM、FreeRTOS LVGL 和 snapshot：`examples/esp-idf/99_esp-brookesia/sdkconfig.defaults:1-3,6-27,56-57`。
- AI framework、GUI animation player、services 和 Speaker System 当前关闭：`examples/esp-idf/99_esp-brookesia/sdkconfig.defaults:28-33`。
- tracked partition table 只有 NVS、PHY 和一个 4 MiB factory App 分区，没有 OTA slot、`otadata` 或 filesystem 分区：`examples/esp-idf/99_esp-brookesia/partitions.csv:1-5`。

`build/`、`managed_components/`、`dependencies.lock` 和生成的 `sdkconfig` 被 `.gitignore` 排除：`.gitignore:12-19`。它们可以帮助本地诊断，但不代表可提交的工程意图；正式结论优先依据 tracked source、manifest、defaults 和 partition table。当前使用的是 legacy Phone API，不应用其他 Brookesia 版本的 App 模型反推本工程行为。

## 模块与职责

```text
Board hardware
    ↓
Waveshare BSP + LVGL adapter
    ↓
LVGL runtime + GUI lock
    ↓
Phone System (shell)
    ├── Launcher / Main Screen
    ├── Status Bar / Navigation Bar
    ├── Recents Screen / Snapshot
    └── Phone App lifecycle and navigation
             ↓
      Squareline Reference App
             └── internal Screens and generated UI
```

| 模块 | 当前职责 |
| --- | --- |
| `main/main.cpp` | Composition root：配置 BSP/LVGL、注册 GUI lock、创建并启动 Phone System、从 static registry 发现和安装 App、更新 Status Bar 时钟。 |
| Waveshare BSP 与 LVGL adapter | 提供共享 display/touch bring-up、LVGL worker 和输入/刷新适配。App 不应重新初始化共享 display 或 touch。 |
| LVGL runtime | 承载 Screen、timer、animation 和绘制生命周期；所有非 GUI task 对 LVGL 的操作必须遵循当前 GUI lock 约束。 |
| Brookesia Core / Phone System | 管理 App ID、installed/running/active 状态、生命周期 dispatch、Visual Area、Main Screen、系统栏、Launcher、Recents 和 Snapshot。 |
| Static App Registry | 保存编译期注册的 App factory；它发现的是已经链接进 firmware 的 App，不是下载包或 App Store。 |
| App component | 负责一个 Phone App 的内容、内部 Screen 路由、App hooks 以及未被 Core 自动记录的业务资源。 |
| Squareline component | 当前唯一的 Reference App；包含生成的 UI、静态资产和 App wrapper。 |

当前 App 级资源 recorder 只覆盖被 Core 记录的 LVGL Screen、timer 和 animation；App 自己创建的 task、queue、heap allocation、driver 或其他未记录资源仍属于 App 的清理责任。`base::App::Config` 对自动回收和 visual-area resize 的边界有明确说明：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_app.hpp:54-75,219-235,319-333,383-395`。

## 启动与 App 注册流

当前启动链为：

```text
app_main
  → 配置 LVGL worker（40 KiB，优先 PSRAM）
  → BSP display/touch 启动与 backlight
  → 注册 Brookesia GUI lock callbacks
  → Phone::begin()
  → static registry discovery
  → boot-time App install（ID、Visual Area、Launcher icon）
  → Launcher 等待用户启动 App
```

对应入口位于 `examples/esp-idf/99_esp-brookesia/main/main.cpp:20-73`。静态 App component 使用 `WHOLE_ARCHIVE` 保留注册对象：`examples/esp-idf/99_esp-brookesia/components/brookesia_app_squareline_demo/CMakeLists.txt:1-10`；Squareline 以 `Squareline` 名称注册 singleton-like 的 firmware-lifetime instance：`examples/esp-idf/99_esp-brookesia/components/brookesia_app_squareline_demo/esp_brookesia_app_squareline_demo.cpp:16-38,379-382`。

这里的 **install** 是把已静态链接的 App 初始化并纳入当前 Phone System，不是从网络、文件系统或用户操作中下载和安装一个二进制包。当前 Launcher icon 的加入路径由 Phone Display 处理：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/phone/esp_brookesia_phone_display.cpp:108-129`。

## 交互、数据与生命周期流

### 交互流

```text
touch input
  → BSP/LVGL input device
  → Phone gesture / Launcher callbacks
  → system navigation or App event
  → App lifecycle transition or internal Screen update
```

Launcher 入口触发 `START` 后，关闭状态的 App 首次执行 `run()`，已经暂停的 App 执行 `resume()`：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_manager.cpp:211-224,245-258`。

Phone System 的当前导航语义是：

- `BACK`：交给当前 `Active App` 的 `back()` hook：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/phone/esp_brookesia_phone_manager.cpp:585-592`。
- `HOME`：暂停当前 App、切换到 `Main Screen` 并清除 active 引用：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/phone/esp_brookesia_phone_manager.cpp:593-602`。
- `RECENTS_SCREEN`：暂停当前 App、生成/更新 Snapshot 并显示 Recents；选择项可以再次 resume，关闭项则结束运行但仍可安装：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/phone/esp_brookesia_phone_manager.cpp:603-635`。

### 当前真实数据流

```text
system time()
  → localtime_r()
  → Phone Status Bar clock
```

该周期更新位于 `examples/esp-idf/99_esp-brookesia/main/main.cpp:75-90`。当前没有在 99 启动链中发现 NTP、RTC 同步、timezone 配置或 Call/Chat/Music/Weather/Alarm 的真实数据源；这些 Screen 由 Squareline 生成 UI 和静态内容组成。

### App Lifecycle

| 当前状态 | 进入方式 | 当前含义 |
| --- | --- | --- |
| `UNINSTALLED` | 初始状态或 uninstall 后 | App 不属于当前 Phone System。 |
| `CLOSED` | install 后，或 close 后 | App 已安装但不在 running set 中。 |
| `RUNNING` | 从 Launcher 首次启动并执行 `run()` | App 在 running set 中；它可能是 active 或随后被暂停。 |
| `PAUSED` | Home 或 Recents 导航触发 `pause()` | App 仍属于 running set，可由 Recents resume。 |

状态名称和 hooks 由 `examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_app.hpp:78-85,241-343` 定义；Manager 对 run、pause、resume、close 的协调位于 `examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_manager.cpp:260-380`。**Close** 与 **uninstall** 不同：close 保留 installed App，uninstall 才移除 Launcher entry 并执行 deinitialization。

## Squareline Reference App 当前结构

Squareline wrapper 的 App 名称是 `Squareline`，关闭状态下首次运行调用 `phone_app_squareline_ui_init()`，Back hook 当前直接通知 Core 关闭 App：`examples/esp-idf/99_esp-brookesia/components/brookesia_app_squareline_demo/esp_brookesia_app_squareline_demo.cpp:36-63`。

当前生成 UI 包含七个内部 Screen：Splash、Clock、Call、Chat、Music Player、Weather、Alarm。它们是一个 Phone App 内的 UI surfaces，不是七个注册 App；生成的 screen 创建和导航集中在 `examples/esp-idf/99_esp-brookesia/components/brookesia_app_squareline_demo/ui/ui.c:27-147,166-337`。因此这些名称目前描述演示画面，而不是已实现的电话、消息、音乐、天气或闹钟领域服务。

## 当前状态分类

### 领域决定

这些名称和边界来自本次领域梳理：

- 99 的正式平台名称是 `Brookesia App Platform`。
- `Phone System` 是运行时 Shell，不把当前实现称为完整 `Device OS`。
- `Phone App` 是独立注册/生命周期单元；`Screen` 不等于 `Phone App`。
- `Squareline` 是 `Squareline Reference App`，其功能 Screen 当前是静态 mock。

### 源码事实与架构约束

- 当前只有一个产品侧注册 App，即 `Squareline`。
- App 以静态 registry 方式编译进 firmware；没有动态下载或安装包边界。
- 默认 stylesheet 当前允许最多三个 running App，并启用 Recents Snapshot：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/phone/stylesheets/default/dark/core_data.hpp:58-74`。这是当前配置事实，不是长期 eviction 产品决策。
- 当前 partition table 只有 4 MiB factory App，没有 OTA 或 filesystem；新增 App 受 firmware 空间约束：`examples/esp-idf/99_esp-brookesia/partitions.csv:1-5`。
- 当前 AI framework、services、animation player 和 Speaker disabled；Board 虽具备更多传感器、音频、无线和电源能力，但当前 99 启动流没有把它们建模为共享平台服务：`examples/esp-idf/99_esp-brookesia/sdkconfig.defaults:28-33`。
- LVGL 操作通过共享 GUI lock 串行化；App 不能自行重启共享 display/touch 组件。启动侧 lock 注册可见：`examples/esp-idf/99_esp-brookesia/main/main.cpp:35-53`。
- Core 的自动资源边界是被 recorder 记录的 LVGL Screen、timer、animation；App 自己拥有的 task、queue、heap、driver 和外部服务资源不在该自动边界内：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_app.hpp:219-235,319-395`。
- 忽略目录中的生成配置和 managed dependencies 不能替代 tracked 工程意图：`.gitignore:12-19`。

### 已知缺陷（仅记录现状）

- App theme 保存到 `_app_style.theme`，恢复却读取 `_display_style.theme`，因此恢复路径可能使用错误的 theme：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_app.cpp:705-775`。
- running App 达到上限时，Manager 将 `std::unordered_map` 的迭代结果称为 oldest；该容器不表达 FIFO、LRU 或稳定运行时间顺序：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_manager.cpp:232-243`。
- Registry initializer/installer 当前会吞掉单项失败并继续打印 success，属于错误传播与状态报告缺陷：`examples/esp-idf/99_esp-brookesia/components/brookesia_core/systems/base/esp_brookesia_base_manager.cpp:142-208`。
- 当前安装/卸载和 snapshot 失败路径还存在状态回滚、悬空 draw buffer 和资源清理顺序风险；这些是待另行修复的实现问题，不在本文档阶段改动。

### 未决事项

以下问题需要未来产品或架构决策，不能从当前源码推断：

- `PAUSED` App 是否允许后台执行，以及哪些 task、audio、sensor、network 工作可以持续。
- 多 App 的稳定 Launcher 顺序、Recents 顺序、running 上限和 eviction policy（FIFO、LRU、显式 close 或其他规则）。
- Wi-Fi、BLE、QMI8658、AXP2101、ES7210、ES8311 等能力由 Phone System 提供共享服务，还是由具体 App 直接拥有。
- 时间同步、真实 Call/Chat/Music/Weather/Alarm 数据源、持久化、隐私和 reset/migration 语义。
- OTA、filesystem、partition 扩容和 App 更新模型。
- 466 × 466 专用 stylesheet、PSRAM/flash 预算、性能和板级可靠性门槛。
- 是否继续维护 legacy Brookesia `0.6.0-beta2`，以及何时迁移到新的上游 API。

## 证据与验证边界

本文记录的是当前 tracked source 能证明的架构，不是板级验收报告。完成 build、烧录和真实触摸/显示/内存压力验证前，不应把硬件稳定性、性能预算或未启用 Board 外设描述为已验证平台能力。

更完整的源码研究、资源预算和新增 App checklist 保留在本地 scratch research 中；它不是本正式架构文档的替代品。
