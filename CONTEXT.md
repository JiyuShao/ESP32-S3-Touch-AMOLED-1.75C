# ESP32-S3-Touch-AMOLED-1.75C

本仓库维护 Waveshare ESP32-S3-Touch-AMOLED-1.75C Board 的示例 firmware、发布产物和恢复资源。本文固定仓库与 `99_esp-brookesia` Brookesia App Platform 的 canonical vocabulary；模块、启动链、数据流和实现约束见 [99 架构说明](docs/architecture/99-esp-brookesia.md)。

## Repository Language

**Board**：本仓库面向的 ESP32-S3-Touch-AMOLED-1.75C 硬件产品。
_Avoid_：把 Board、某个 Firmware Example 或某个发布镜像当作同一概念。

**Firmware Example**：仓库中可独立构建、用于展示或验证 Board 能力的源码工程。它可以是 ESP-IDF project 或 Arduino sketch，不等同于运行时的 `Phone App`。
_Avoid_：Demo App（当指整个源码工程时）、Release Firmware。

**Release Firmware**：从某个 `Firmware Example` 构建并打包、供用户烧录的发布产物。
_Avoid_：Factory Firmware、源码 Example。

**Factory Firmware**：随 Board 提供、用于恢复出厂状态或演示的既有 firmware 二进制；它不是 CI 从当前 Example 构建出的发布产物。
_Avoid_：Release Firmware、可持续维护的 Example 源码。

## Brookesia App Platform Language

**Brookesia App Platform**：`99_esp-brookesia` 面向当前 Board 的 App 运行与开发基线，目标是承载和验证多个 Brookesia `Phone App`。当前它不是具备动态安装、更新和完整系统服务承诺的 `Device OS`。
_Avoid_：Device OS（作为当前实现的正式名称）、单一 Squareline Demo。

**Phone System**：Brookesia App Platform 内负责承载 `Phone App`、协调其生命周期并提供系统级 UI 的运行时系统。
_Avoid_：把 Phone System 说成一个普通 `Screen` 或一个业务 App。

**Phone App**：具有独立注册身份和生命周期的应用单元，可以包含多个 `Screen`，并由 `Phone System` 统一协调其进入、暂停、恢复和关闭。
_Avoid_：Screen、Firmware Example、可独立下载的安装包。

**Installed App**：已被当前 `Phone System` 接受并出现在 `Launcher` 中的 `Phone App`。
_Avoid_：Running App；已安装不表示正在前台运行。

**Running App**：已打开并由 `Phone System` 保留运行状态的 `Phone App`，可以是前台的 `Active App`，也可以是暂停状态。
_Avoid_：只指当前可见 App、Installed App 的同义词。

**Active App**：当前显示在前台、接收当前 App 交互的 `Running App`。
_Avoid_：把所有 Running App 都称为 Active App。

**App Lifecycle**：`Phone App` 从未安装、安装、运行、暂停、恢复、关闭到卸载所经历的状态和转换。
_Avoid_：把 Screen 切换或页面动画称为 App Lifecycle。

**Screen**：`Phone App` 或 `Phone System` 内部的一个 UI surface，用于呈现内容或系统界面；它不是可独立注册、安装或运行的 `Phone App`。
_Avoid_：Page、View（作为本项目的 canonical 领域术语）、独立 App。

**Launcher**：`Phone System` 提供的系统 UI，用于展示和进入已安装的 `Phone App`。
_Avoid_：把 Launcher 中的一项称为一个 Screen。

**Launcher Icon**：`Phone App` 在 `Launcher` 中的可识别入口。
_Avoid_：把图标资源文件本身称为 Phone App。

**Main Screen**：没有 App 处于前台时由 `Phone System` 展示的系统主界面。
_Avoid_：Squareline App 的首页 Screen。

**Status Bar**：由 `Phone System` 管理、用于展示系统状态或 App 状态入口的系统 UI 区域。
_Avoid_：把 Status Bar 当作某个 Phone App 自己拥有的 Screen。

**Navigation Bar**：由 `Phone System` 管理、用于提供系统导航入口的 UI 区域。
_Avoid_：App 内部的导航控件。

**Recents Screen**：由 `Phone System` 管理、用于查看、选择或关闭 `Running App` 的系统 UI。
_Avoid_：App 自己的历史记录 Screen。

**Snapshot**：`Phone System` 为 `Recents Screen` 保存的 App 画面摘要，用于展示最近运行的 App；它不是 App 的业务数据持久化。
_Avoid_：备份、存档、业务数据库。

**Gesture Navigation**：把 Board 的触摸手势转换为 `Back`、Home 或 Recents 等系统导航意图的交互方式。
_Avoid_：App 内部的 Screen 滑动（除非明确说明两者的关系）。

**Visual Area**：`Phone System` 为 `Phone App` 计算出的可用显示区域，反映系统 UI 对 App 内容空间的约束。
_Avoid_：物理屏幕尺寸的同义词。

**Squareline Reference App**：当前 99 中用于验证 `Phone App` 注册、生命周期和 UI 集成方式的参考 App；它包含多个静态 mock `Screen`，其中的 Call、Chat、Music、Weather 和 Alarm 内容不代表已经接入对应的真实服务。
_Avoid_：多个独立 App、完整的电话/聊天/音乐/天气/闹钟产品。
