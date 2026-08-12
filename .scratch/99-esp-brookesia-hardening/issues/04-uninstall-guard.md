# P0-4: Running App 可直接卸载

## 现状

`esp_brookesia_base_manager.cpp:88-120`

```cpp
bool BaseManager::uninstallApp(App *app) {
    // ...
    _display.processAppUninstall(app);  // 直接清理 display
    app->processUninstall();            // 直接卸载
    _id_installed_app_map.erase(app->_id);
    return true;
}
```

不检查 App 当前状态。RUNNING/PAUSED 的 App 可以直接被卸载，导致：
- App screen 仍在 display 中但 App 对象已 deinit
- 可能的 use-after-free（display/timer 回调引用已销毁 App）

## 修复

在 `uninstallApp()` 开头添加状态检查：

```cpp
if (app->getStatus() != App::CLOSED) {
    ESP_LOGE(TAG, "uninstallApp(%s): app must be CLOSED, current status=%d",
             app->getName().c_str(), (int)app->getStatus());
    return false;  // 或 assert(0) 在 debug build
}
```

## 接受标准

- [ ] RUNNING/PAUSED App 调用 `uninstallApp()` 返回 false
- [ ] CLOSED App 正常卸载
- [ ] `idf.py build` 通过

## 依赖

无

## 工作量

~5 行，< 15 min
