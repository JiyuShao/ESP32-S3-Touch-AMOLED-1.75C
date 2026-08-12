# P0-6: Active close 立即设 CLOSED，清理延迟到异步回调

## 现状

`esp_brookesia_base_app.cpp:497-536` `processClose()`

```cpp
int BaseApp::processClose() {
    // ...
    if (_active_screen != nullptr) {
        close();                    // App hook
        enableAutoClean();          // 注册 LV_EVENT_SCREEN_UNLOADED 回调
        _status = CLOSED;           // ← 立即设 CLOSED，但清理未执行
        return 0;
    }
    // ...
}
```

`enableAutoClean()` (line 686-703) 在 `last_screen` 上注册 `LV_EVENT_SCREEN_UNLOADED` 回调。但 `load_new_screen()` 是同步的——回调在 `lv_scr_load()` 返回前就执行了。当前代码的问题不是异步性，而是：

1. **顺序错误**: 先设 CLOSED，后切 screen → 切 screen 期间 App 已标记 CLOSED 但 screen 仍活跃
2. **enableAutoClean 不触发**: `lv_scr_load(main)` 同步发送 UNLOADED 事件，但如果 `enableAutoClean` 注册后 screen 已经被切走，回调可能不触发

## 修复：Commit Boundary

```cpp
int BaseApp::processClose() {
    if (_status == CLOSED) return 0;

    if (_active_screen != nullptr) {
        // === 预提交阶段（可逆） ===
        // 1. App hook
        if (close() != 0) {
            ESP_LOGE(TAG, "close: app hook failed");
            return -1;  // 保持 RUNNING
        }

        // 2. 停止 App 生产者（pause 语义子集）
        //    App::close() 内部已停止

        // 3. Display 预检
        if (!_display->canCloseApp(this)) {
            ESP_LOGE(TAG, "close: display preflight failed");
            return -1;  // 保持 RUNNING
        }

        // === 提交阶段（不可逆） ===
        // 4. 同步切屏 → 触发 UNLOADED → cleanResource
        lv_scr_load(_display->getMainScreen());

        // 5. 补充清理（cleanResource 已处理 recorder 内资源）
        cleanExtraResource();

        // 6. 状态提交
        _status = CLOSED;
        _active_screen = nullptr;

        return 0;
    }

    // inactive close（无 screen 的 App）
    close();
    _status = CLOSED;
    return 0;
}
```

Manager 侧同步 `releaseAppSnapshot` + `display.processAppClose` + `erase running map`。

## LVGL 9.5 证据

`managed_components/lvgl__lvgl/src/display/lv_display.c:763-766,1428-1492`

```c
void lv_screen_load(lv_obj_t *new_screen) {
    lv_screen_load_anim(new_screen, LV_SCR_LOAD_ANIM_NONE, 0, 0, false);
}
// → load_new_screen() 同步发送:
//   LV_EVENT_SCREEN_UNLOAD_START → LV_EVENT_SCREEN_LOADED → LV_EVENT_SCREEN_UNLOADED
```

`lv_scr_load(main)` 返回时，旧 screen 的 `LV_EVENT_SCREEN_UNLOADED` 回调已执行完毕。

## 接受标准

- [ ] close 失败 → App 保持 RUNNING，screen 不变
- [ ] close 成功 → screen 同步切换到 main，清理完成，CLOSED
- [ ] HARD FAULT / 不可恢复错误在提交阶段 → 报 teardown fault，不伪装 RUNNING
- [ ] 移除 `enableAutoClean` 机制（替换为同步清理）
- [ ] `idf.py build` 通过
- [ ] 烧录 smoke: Home → Recents close → 重新从 Launcher 启动正常

## 依赖

P0-5 (snapshot transaction) — Manager 侧 close 路径涉及 snapshot 释放

## 工作量

最大改动，~100 行，< 4h
