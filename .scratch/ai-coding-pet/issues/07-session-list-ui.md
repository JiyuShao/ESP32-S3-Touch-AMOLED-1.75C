# 07 — 滑动切 session 列表页

**What to build:** 宠物主页保持单宠物不变;水平滑动 >60px 在宠物页 ⇄ session 列表页之间切换,列表实时展示所有活跃会话(每行 state + basename + 更新时间,按优先级排序,error 最上),页头显示活跃会话计数,底部小圆点指示页码。范围以 spec-device-interactions.md 的 Sessions 部分为准。

**Blocked by:** None — can start immediately.

**Status:** ready-for-agent

## Acceptance criteria

- [ ] 主页:单宠物 + dominant 状态动画不变;底部小圆点指示当前页
- [ ] 手势:视觉区中心任意水平滑动翻页(手写判定:PRESSED 记录起点、RELEASED 计算 `dx > 60px`,单点触摸即可);屏幕**边缘**手势仍归 shell(返回/Home 不受影响,App 不动 `enable_navigation_gesture` 配置)
- [ ] 列表页:每 session 一行(state + basename + 更新时间),按 priority desc、updated_at desc 排序(error 最上);随 bridge 推送实时刷新(新增/状态变化/移除/TTL 过期)
- [ ] 列表空态:无活跃 session 时显示占位提示,而非空白页
- [ ] 页头显示活跃会话计数
- [ ] App back() 时恢复宠物页
- [ ] `close()` 释放列表页全部 LVGL 对象(沿用 ticket 05 的 deinit 模式——shell 不替 App 清理)
- [ ] 上板验收:PC 双会话跑 Claude Code → 滑动看到 2 行实时状态且排序正确;结束一个会话 → 该行消失

## 实现备注

- 数据源复用 06 已交付的 session 列表访问器;UI 以固定周期轮询渲染,不引入事件总线。
- 手势手写判定,不依赖 LVGL 手势识别(当前构建未开启)。
