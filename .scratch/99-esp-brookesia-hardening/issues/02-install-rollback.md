# P0-2: Install 回滚破坏 installed map

## 现状

`esp_brookesia_base_manager.cpp:36-80` `installApp()`

```cpp
bool BaseManager::installApp(App *app) {
    // ...
    if (!app->processInstall(context, config)) {
        app->processUninstall();  // ← 重置 _id = -1
        _id_installed_app_map.erase(app->_id);  // ← erase(-1)，擦错 entry
        return false;
    }
    _id_installed_app_map[app->_id] = app;  // ← processInstall 成功后 _id 已赋值
    // ...
}
```

问题：
1. `processInstall` 内部先设 `_id = candidate_id`，失败调 `processUninstall` 重置 `_id = -1`
2. `erase(-1)` 可能擦除其他 entry（或 no-op，取决于实现）
3. `_id` 在验证完所有前置条件前就被修改，回滚不干净

## 修复：事务提交 ID

```cpp
// processInstall 内部：
bool BaseApp::processInstall(const Context *ctx, const Config &cfg) {
    int candidate_id = _id;  // 暂存当前值（初始为 -1）

    _system_context = ctx;
    _active_config = cfg;
    _id = candidate_id;  // 先保持原值

    if (!beginExtra()) {
        goto rollback;
    }
    if (init() != 0) {
        goto rollback;
    }

    _id = candidate_id;  // ← 只在成功路径提交
    return true;

rollback:
    // processUninstall 但保留 _id 原值
    delExtra();
    deinit();
    _system_context = nullptr;
    _active_config = {};
    // _id 不变（保持 pre-install 值）
    return false;
}
```

Manager 侧：

```cpp
bool BaseManager::installApp(App *app) {
    int prev_id = app->_id;  // 保存安装前 ID

    if (!app->processInstall(context, config)) {
        // _id 未变，_installed_map 未插入 → 无需回滚
        return false;
    }

    // _id 已提交新值 → 安全插入
    _id_installed_app_map[app->_id] = app;
    // ...
}
```

## 接受标准

- [ ] processInstall 失败 → `_id` 保持安装前值
- [ ] Manager 不在失败路径调用 `erase(-1)`
- [ ] 连续 install → fail → install → success 正确（_id 不冲突）
- [ ] `idf.py build` 通过

## 依赖

P0-1 (theme fix) — 同为 `processInstall/processUninstall` 路径

## 工作量

~50 行，< 3h
