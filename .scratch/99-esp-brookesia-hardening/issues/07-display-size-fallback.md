# P0-7: Context::getDisplaySize() 隐式 fallback + Phone(nullptr)

## 现状

`esp_brookesia_base_context.cpp:47-62`

```cpp
lv_area_t BaseContext::getDisplaySize() const {
    lv_area_t area = {0, 0, 0, 0};
    if (_display_device != nullptr) {
        area.x2 = lv_display_get_horizontal_resolution(_display_device) - 1;
        area.y2 = lv_display_get_vertical_resolution(_display_device) - 1;
    } else {
        // 隐式 fallback
        lv_display_t *disp = lv_disp_get_default();
        area.x2 = lv_display_get_horizontal_resolution(disp) - 1;
        area.y2 = lv_display_get_vertical_resolution(disp) - 1;
    }
    return area;
}
```

`main.cpp:59` 用 `new Phone()` (nullptr display) 创建 Phone。Phone::begin() 检查 `_display_device` 但只影响 stylesheet 选择，不阻止后续 `getDisplaySize()` 走 fallback。

**这不是 crash bug**（BSP 已注册 default display），但是隐式依赖：如果 default display 未注册或顺序变化，静默行为改变。

## 修复

最小方案：文档化 + assert

```cpp
// main.cpp 改为显式传入 display
lv_display_t *disp = lv_disp_get_default();
assert(disp != nullptr);
Phone *phone = new Phone(disp);
```

或在 `BaseContext::getDisplaySize()` fallback 路径加 `ESP_LOGW` 一次：

```cpp
} else {
    static bool warned = false;
    if (!warned) {
        ESP_LOGW(TAG, "getDisplaySize: _display_device is null, falling back to lv_disp_get_default()");
        warned = true;
    }
    // ...
}
```

## 接受标准

- [ ] Phone 构造显式传入 display 指针（或 fallback 加 warn/assert）
- [ ] `idf.py build` 通过

## 依赖

无

## 工作量

~3 行，< 15 min
