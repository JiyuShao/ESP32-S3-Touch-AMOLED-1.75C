# H-9: App 析构禁止自动卸载

## 现状

`esp_brookesia_phone_app.cpp:41-50`

```cpp
PhoneApp::~PhoneApp() {
    // 析构时自动调用 Manager::uninstallApp
    getSystem()->getManager().uninstallApp(this);  // ← 禁止
}
```

问题：
1. 隐式副作用：销毁 App 对象不等价于从 Phone System 卸载
2. Manager 可能先于 App 销毁（析构顺序问题）
3. 违反单一职责：App 管理自己内部资源，Manager 管理 App 注册

## 修复

```cpp
PhoneApp::~PhoneApp() {
    // App 析构只释放 App 自有资源，不调用 Manager uninstall
    // 卸载必须由 Manager 显式调用 processUninstall()
}
```

移除非自有资源的清理代码。

## 接受标准

- [ ] `~PhoneApp()` 不再调用 `uninstallApp(this)`
- [ ] 析构只清理 App 自己分配的资源（如果有）
- [ ] `idf.py build` 通过
- [ ] smoke: Phone::del() → 正常关闭（验证析构不 crash）

## 依赖

H-8 (Manager::del shutdown)

## 工作量

~3 行删除，< 10 min
