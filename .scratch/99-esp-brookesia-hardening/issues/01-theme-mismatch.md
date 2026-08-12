# P0-1: Theme 保存/恢复成员不匹配

## 现状

`esp_brookesia_base_app.cpp:741-775`

- `saveAppTheme()` (line 755): 保存 `_app_style.theme`
- `loadAppTheme()` (line 763): 读取 `_display_style.theme`

App resume 时恢复的是 display style theme，不是 App 自己的 theme。导致恢复后 App UI 主题错误。

## 修复

`loadAppTheme()` 第 763 行：`_display_style.theme` → `_app_style.theme`

```cpp
// Before:
lv_style_t *theme = (lv_style_t *)_display_style.theme;

// After:
lv_style_t *theme = (lv_style_t *)_app_style.theme;
```

## 接受标准

- [ ] `loadAppTheme()` 读取 `_app_style.theme` 而非 `_display_style.theme`
- [ ] `idf.py build` 通过
- [ ] PAUSE → RESUME 后 App theme 正确恢复

## 依赖

无

## 工作量

1-liner，< 5 min
