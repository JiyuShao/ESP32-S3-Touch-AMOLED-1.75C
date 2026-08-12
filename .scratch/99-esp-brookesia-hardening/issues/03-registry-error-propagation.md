# P0-3: Registry 安装器吞掉失败

## 现状

`esp_brookesia_base_manager.cpp:142-208`

`initAppFromRegistry()` 和 `installAppFromRegistry()` 在单项 install 失败时：
- 吞掉错误，继续下一项
- 始终打印 "successfully" 日志
- 始终返回 true

调用方（`main.cpp:70-73`）无法知道哪些 App 安装成功、哪些失败。

## 修复

1. 聚合错误列表
2. 正确报告成功/失败计数
3. 返回成功安装的 App 列表（或修改为返回实际结果）

```cpp
struct InstallResult {
    std::vector<std::string> installed;
    std::vector<std::pair<std::string, std::string>> failed;  // name, reason
};

InstallResult installAppsFromRegistry() {
    InstallResult result;
    for (auto &[name, factory] : registry) {
        auto app = factory();
        if (!app) {
            result.failed.push_back({name, "factory returned null"});
            continue;
        }
        if (!installApp(app.get())) {
            result.failed.push_back({name, "install failed"});
            continue;
        }
        result.installed.push_back(name);
    }
    if (!result.failed.empty()) {
        ESP_LOGE(TAG, "registry install: %d/%d failed", 
                 result.failed.size(), result.failed.size() + result.installed.size());
        for (auto &[name, reason] : result.failed) {
            ESP_LOGE(TAG, "  %s: %s", name.c_str(), reason.c_str());
        }
    }
    return result;
}
```

## 接受标准

- [ ] 失败时不再打印 "successfully"
- [ ] 错误日志记录每个失败的 App 名称和原因
- [ ] 调用方可以区分成功/失败的 App
- [ ] `idf.py build` 通过

## 依赖

无（可能需要先看 P0-2 install 回滚修复，因为 installApp 本身的失败处理也有 bug）

## 工作量

~30 行，< 1h
