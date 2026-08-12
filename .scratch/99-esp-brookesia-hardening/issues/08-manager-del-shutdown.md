# H-8: Manager::del() memory leak — snapshot_map 未逐个释放

## 现状

`esp_brookesia_base_manager.cpp:550-576` `Manager::del()`

```cpp
void BaseManager::del() {
    // ...
    for (auto &[id, app] : _id_installed_app_map) {
        app->processUninstall();
    }
    _id_installed_app_map.clear();
    _snapshot_map.clear();  // ← lv_draw_buf_t* 泄漏！只清 map，不 destroy buffer
    // ...
}
```

## 修复

```cpp
void BaseManager::del() {
    // 1. close all running apps
    for (auto &[id, app] : _id_running_app_map) {
        if (app->getStatus() == App::RUNNING || app->getStatus() == App::PAUSED) {
            if (processAppClose(app) != 0) {
                ESP_LOGE(TAG, "del: close failed for %s, forcing uninstall", app->getName().c_str());
            }
        }
    }

    // 2. uninstall all installed apps
    for (auto &[id, app] : _id_installed_app_map) {
        app->processUninstall();
    }
    _id_installed_app_map.clear();

    // 3. release all snapshot buffers
    for (auto &[id, buf] : _snapshot_map) {
        if (buf) {
            lv_draw_buf_destroy(buf);
        }
    }
    _snapshot_map.clear();
}
```

## 接受标准

- [ ] `_snapshot_map.clear()` 替换为逐个 `lv_draw_buf_destroy()`
- [ ] 关闭顺序：close running → uninstall all → release snapshots
- [ ] 聚合 close 失败的 App 名称并 log
- [ ] `idf.py build` 通过

## 依赖

P0-6 (close commit boundary) — close 逻辑变更

## 工作量

~20 行，< 1h
