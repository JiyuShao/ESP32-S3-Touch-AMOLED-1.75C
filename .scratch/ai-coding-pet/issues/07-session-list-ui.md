# 07 — 滑动切 session 列表页

**What to build:** 宠物主页保持单宠物不变;水平滑动 >60px 在宠物页 ⇄ session 列表页之间切换,列表实时展示所有活跃会话(每行 state + basename + 更新时间,按优先级排序,error 最上),页头显示活跃会话计数,底部小圆点指示页码。范围以 spec-device-interactions.md 的 Sessions 部分为准。

**Blocked by:** None — can start immediately.

**Status:** done（2026-08-14 板端验证通过：左右滑动双向切换多次往返、空态"list empty"日志、行实时增长与排序全部串口确认；实现附带用户要求的 Monokai 暗色主题且配置化——pet_theme.h 调色板注册表一处切换）

## Acceptance criteria

- [x] 主页:单宠物 + dominant 状态动画不变;底部小圆点指示当前页
- [x] 手势:视觉区任意起点(含边缘)水平滑动翻页(手写判定:PRESSED 记录起点、RELEASED 计算 `dx > 60px` 且水平主导,单点触摸即可);上板实测确认本构建 shell 边缘导航手势失效(LV_USE_GESTURE_RECOGNITION=0),故边缘滑动归 App,App 不动 `enable_navigation_gesture` 配置;未来开启手势识别时需恢复边缘豁免
- [x] 列表页:每 session 一行(state + basename + 更新时间),按 priority desc、updated_at desc 排序(error 最上);随 bridge 推送实时刷新(新增/状态变化/移除/TTL 过期)
- [x] 列表空态:无活跃 session 时显示占位提示,而非空白页
- [x] 页头显示活跃会话计数
- [x] App back() 时恢复宠物页
- [x] `close()` 释放列表页全部 LVGL 对象(沿用 ticket 05 的 deinit 模式——shell 不替 App 清理)
- [x] 上板验收:PC 双会话跑 Claude Code → 滑动看到 2 行实时状态且排序正确;结束一个会话 → 该行消失

## 实现备注

- 数据源复用 06 已交付的 session 列表访问器;UI 以固定周期轮询渲染,不引入事件总线。
- 手势手写判定,不依赖 LVGL 手势识别(当前构建未开启)。
- **上板排障实录(2026-08-14)**:右滑失效根因不是边缘区,而是 LVGL 9 事件冒泡——按压目标解析到列表页全屏容器 `_list_screen` 后,事件只发给它;只有带 `LV_OBJ_FLAG_EVENT_BUBBLE` 的对象才继续冒泡(lv_obj_event.c)。给 `_list_screen`/`_overlay`/`_error_border`/`_dots` 补 flag 后双向滑动恢复。此前左滑能用纯属巧合(起点落在空白区,目标直接是屏幕)。
