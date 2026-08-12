# 99-esp-brookesia 硬化规格

> 目标：修复已知 P0 缺陷 + 落实生命周期硬化契约，使平台安全承载第二个 App。
> 策略：Legacy 最小修正 —— 不迁移 New Brookesia API，不改 `brookesia_core 0.6.0-beta2` 版本基线。

## 硬化域模型（已确认契约）

### 1. Close 事务：Commit Boundary

```
预提交（可逆）
  ├─ App::close()
  ├─ 停止 App 自有 producer（task/sensor/network/audio 回调）
  └─ display 预检（visual area、screen 存在性）
        ↓ 任一步失败 → 保持 RUNNING/Active，不修改任何共享状态
        ↓ 全部成功 → 进入提交阶段
提交阶段（不可逆）
  ├─ lv_screen_load(main_screen)  ← 同步触发 LV_EVENT_SCREEN_UNLOADED
  │    └─ cleanResource / cleanRecordResource / cleanDefaultScreen
  ├─ releaseAppSnapshot
  ├─ display.processAppClose
  ├─ processAppCloseExtra
  ├─ _id_running_app_map.erase
  ├─ _active_app = nullptr
  └─ _status = CLOSED
```

提交后错误是 **teardown fault**，不伪装成可回滚的 close 失败。LVGL 9.5 `lv_screen_load()` → `load_new_screen()` 同步发送 `LV_EVENT_SCREEN_UNLOADED`，`enableAutoClean` 注册的回调在 `lv_scr_load()` 返回前执行完毕。不可逆保护：预提交成功后才调用 `lv_scr_load(main)`。

### 2. PAUSED：全部暂停

```
processAppPause:
  ├─ App::pause()       ← App 停止自有 producer
  ├─ saveAppTheme()     ← 保存 _app_style.theme
  ├─ saveRecentScreen() ← 保存 Recents 入口
  ├─ loadDisplayTheme() ← 装回系统 theme
  ├─ saveAppSnapshot()  ← 截图（失败不阻塞暂停）
  └─ display.processAppPause
```

PAUSED 语义：App 的 **全部** 生产者（task、sensor、网络、音频回调）必须停止。App 通过 `pause()` hook 自行执行。

### 3. Snapshot：事务所有权

```
saveAppSnapshot:
  1. 创建新 snapshot buffer（新地址或同尺寸复用 → 无破坏）
  2. 填充新 buffer
  3. take: map[name] = new_buffer  ← 原子指针替换
  4. destroy(old_buffer)            ← 旧 buffer 释放
```

创建/填充失败 → 不修改 map，报告失败。take 成功后 map 持有有效新 buffer。旧的由 Manager 负责释放。

### 4. Install：事务提交 ID

```
processInstall:
  1. 暂存 candidate_id
  2. _system_context = candidate_context
  3. _active_config = candidate_config
  4. beginExtra / init
      ↓ 失败 → processUninstall (回滚)，_id 保持旧值
      ↓ 成功
  5. _id = candidate_id  ← 只在此处提交
```

### 5. Registry：继续 + 聚合失败

```
installAppFromRegistry:
  errors = []
  for each (name, factory):
    app = factory()
    if !installApp(app):
      errors += {name, reason}
      continue
    success_names += name
  if errors:
    ESP_LOGE("registry install: %d/%d failed", errors.size(), total)
    for each error: ESP_LOGE("  %s: %s", error.name, error.reason)
  return success_names  ← 只返回已成功安装的 App
```

### 6. Teardown：有效上下文

```
processUninstall:
  1. assert(_id != -1)                                    ← 防御性
  2. context = _system_context  (拷贝)                     ← 保持有效
  3. config = _active_config     (拷贝)
  4. id = _id                    (拷贝)
  5. delExtra(context, config, id)                         ← teardown hook
  6. deinit()
  7. _system_context = nullptr
  8. _active_config = {}
  9. _id = -1
```

### 7. App 析构：禁止隐式卸载

`~App()` 不再调用 `getSystem()->getManager().uninstallApp(this)`。App 析构是 App 自身的清理，不是 Phone System 的卸载入口。卸载必须由 Manager 显式调用。

### 8. Phone 关闭：Best-effort 聚合

```
Manager::del():
  errors = []
  for each installed app:
    if app is RUNNING or PAUSED:
      if !processAppClose(app):
        errors += {app, reason}
        processAppUninstall(app)  ← 强制清理
      else:
        processAppUninstall(app)
    else:
      processAppUninstall(app)
  snapshot_map 逐个释放  ← 修复当前 clear() 导致的内存泄漏
  if errors:
    ESP_LOGE("shutdown: %d apps failed to close cleanly", errors.size())
```

### 9. Batch Install：只展示成功项

```
installApps(names):
  installed = []
  for each name in names:
    if installApp(factory(name)):
      installed += name
  display.updateLauncher(installed)
  return installed
```

### 10. Uninstall：仅 CLOSED 可卸载

```
uninstallApp(app):
  if app.status != CLOSED:
    ESP_LOGE("uninstall %s: app must be CLOSED (current: %d)", app.name, app.status)
    return false
  display.processAppUninstall(app)
  app.processUninstall()
  _id_installed_app_map.erase(app.id)
```

---

## P0 缺陷（修复清单）

### P0-1: Theme 保存/恢复成员不匹配 **[1-liner]**
- **文件**: `esp_brookesia_base_app.cpp:763`
- **现状**: `saveAppTheme()` 写 `_app_style.theme`，`loadAppTheme()` 读 `_display_style.theme`
- **修复**: `loadAppTheme()` 改为读 `_app_style.theme`
- **影响**: App resume 时恢复错误 theme，UI 异常

### P0-2: Install 回滚破坏 installed map
- **文件**: `esp_brookesia_base_manager.cpp:36-80`
- **现状**: 失败时 `processUninstall` 重置 `_id=-1`，然后 `erase(-1)` 擦除错误 entry
- **修复**: 暂存 candidate_id，只在安装成功后提交；失败路径跳过 map erase

### P0-3: Registry 安装器吞掉失败
- **文件**: `esp_brookesia_base_manager.cpp:142-208`
- **现状**: 单项 install 失败后继续，打印 "successfully" 日志
- **修复**: 聚合错误列表，正确报告成功/失败计数

### P0-4: Running App 可直接卸载
- **文件**: `esp_brookesia_base_manager.cpp:88-120`
- **现状**: `uninstallApp()` 不检查状态，直接 display uninstall + processUninstall
- **修复**: 加 `assert(status == CLOSED)` 或返回 false（防御性）

### P0-5: Snapshot 失败悬挂指针
- **文件**: `esp_brookesia_base_manager.cpp:382-443`
- **现状**: old buffer 销毁后 map 仍持有 dangling pointer；候选创建成功但 take 失败时 map 未更新
- **修复**: 事务模型——创建新 buffer → 填充 → 原子 swap → 销毁旧

### P0-6: Active close 立即设 CLOSED，清理延迟
- **文件**: `esp_brookesia_base_app.cpp:497-536`
- **现状**: close() → enableAutoClean() → CLOSED，实际清理在 LV_EVENT_SCREEN_UNLOADED 异步触发
- **修复**: Commit boundary 模型——预提交成功后同步 `lv_scr_load(main)` 触发 unload 回调，清理完成后才设 CLOSED

### P0-7: Context::getDisplaySize() 隐式依赖
- **文件**: `esp_brookesia_base_context.cpp:47-62`
- **现状**: `_display_device == nullptr` 时 fallback 到 `lv_disp_get_default()`
- **修复**: 文档化或添加 assert；Phone(nullptr) 依赖全局 display 是隐式契约

---

## 实现顺序

```
Phase 1: P0-1 (theme)        ← 1-liner，无依赖
          P0-4 (uninstall)   ← 独立防御
          P0-3 (registry)    ← 独立修复

Phase 2: P0-5 (snapshot)     ← 事务模型，内核改动
          P0-2 (install)     ← 依赖事务 ID 概念

Phase 3: P0-6 (close)        ← commit boundary，最大改动
          P0-7 (displaySize) ← 文档化 / assert

每个 Phase 完成后：Build + Unity test 通过
全部完成后：烧录 + smoke（Launcher 启动、Home/Recents/Close/Uninstall）
```

## 验证门槛

- [ ] `idf.py build` 通过（ESP-IDF v6.0.1，target esp32s3）
- [ ] 4 MiB factory partition 可用空间 ≥ 35%
- [ ] Squareline App: install → run → Home pause → Recents resume → close → uninstall 完整周期无 crash
- [ ] 第二个 App（最小 mock）安装成功，Launcher 可见
