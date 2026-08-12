# P0-5: Snapshot 失败悬挂指针

## 现状

`esp_brookesia_base_manager.cpp:382-443` `saveAppSnapshot()`

问题路径：

1. **Same-size 替换**: old buffer `lv_draw_buf_destroy()` → map 仍持有 dangling pointer → 新 buffer 创建失败 → map 持有已释放地址
2. **Different-size 替换**: old buffer destroy → 新 buffer create 成功 → `lv_draw_buf_take()` 失败 → 候选 destroy，但 map 仍持有旧（已释放）地址

## 修复：事务所有权模型

```cpp
bool BaseManager::saveAppSnapshot(App *app) {
    auto it = _snapshot_map.find(app->_id);
    lv_draw_buf_t *old_buf = (it != _snapshot_map.end()) ? it->second : nullptr;

    // 1. 创建候选 buffer
    lv_draw_buf_t *candidate = /* allocate new draw buf */;
    if (!candidate) {
        ESP_LOGE(TAG, "snapshot: alloc failed for %s", app->getName().c_str());
        return false;  // old buffer 未动
    }

    // 2. 填充候选
    if (!fillSnapshot(candidate)) {
        lv_draw_buf_destroy(candidate);
        ESP_LOGE(TAG, "snapshot: fill failed for %s", app->getName().c_str());
        return false;  // old buffer 未动
    }

    // 3. 原子替换 (take)
    _snapshot_map[app->_id] = candidate;

    // 4. 释放旧 buffer
    if (old_buf) {
        lv_draw_buf_destroy(old_buf);
    }

    return true;
}
```

## 接受标准

- [ ] 候选 buffer 创建/填充失败 → map 不变，old buffer 仍有效
- [ ] take 成功后 old buffer 正确释放
- [ ] Manager::del() 中 `_snapshot_map.clear()` 改为逐个 destroy
- [ ] `idf.py build` 通过

## 依赖

无（独立修复）

## 工作量

~40 行，< 2h
