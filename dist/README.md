# dist/ —— CI 构建好的成品

这个目录下的文件由 GitHub Actions 自动提交，**不要手动改**。

| 文件 | 说明 | 来源 |
|---|---|---|
| `xlengine.exe` | Windows 64 位可执行程序（静态链接，无需额外 DLL） | `build-exe.yml` |
| `xlengine.apk` | Android 安装包（arm64-v8a / armeabi-v7a / x86_64） | `build-apk.yml` |

每次推送到 `main` 会自动重建并覆盖。也可以到
[Actions 页面](../../actions) 手动触发，产物同样会出现在这里。

## 用法

**Windows：**

```
xlengine.exe                 启动终端表格界面
xlengine.exe 文件.xlsx       打开 xlsx
xlengine.exe --help          查看全部命令
```

建议在 Windows Terminal 或 PowerShell 里运行，并确认控制台字体支持中文。

**Android：**

装 APK 后打开即可，会直接跑一批示例公式（覆盖 `-2^2`、`MOD(-3,2)`、
`INT(-2.5)`、中文 `LEN` 等 Excel 典型暗坑）。也可以在输入框里输入公式求值。

注意：这是**最小可用的外壳**，不是完整的表格应用 —— 引擎核心（495 个函数、
xlsx、PDF、图表）已完整复用，但网格控件与交互层还需要另外实现。
