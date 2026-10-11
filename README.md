# Kingdom Two Crowns 内存修改器 — 工程说明

面向 **Steam《王国：两位君主》2.4.2**（Unity 6000.0.66f2 / IL2CPP / x64）的注入式内存修改器。

---

## 一、目录结构

| 路径 | 说明 |
|---|---|
| `release\` | **成品（分享给别人的就是这 4 个文件）** |
| `release\KTCTrainer.exe` | 控制面板：自动注入游戏、功能开关、实时状态 |
| `release\ktc_mod.dll` | 修改器本体，注入到游戏进程内运行（必须和 exe 同目录） |
| `release\使用说明.md` | 面向使用者的中文说明（含热键表、常见问题、实测记录） |
| `release\界面预览.png` | 面板界面截图 |
| `src\` | 源码（C++17，无第三方框架依赖） |
| `src\common\` | 公共头：IL2CPP API 绑定、面板↔修改器共享内存协议 |
| `src\mod\` | 修改器本体（注入 DLL）：10 项功能 + 8 个 hook |
| `src\launcher\` | 控制面板（Win32 GUI + 注入器） |
| `src\recon\` | 侦察工具：把游戏全部类/字段/方法导出成文本 |
| `src\tools\inject.cpp` | 命令行注入器 |
| `third_party\minhook-master\` | MinHook（内联 hook 库，BSD-2） |
| `tools\` | 开发/验证脚本（截图、模拟按键、日志、CT 表解析等） |
| `docs\` | 逆向分析报告：经济系统 / 单位与坐骑 / 建筑与王冠 |
| `build.bat` | 编译脚本（自动调用 vcvars64） |
| `ktc_root.txt` | 工程根标记：修改器靠它定位日志目录（用第三方注入器时） |

`build\`、`logs\`、`dump\` 属于构建/调试产物，随时可删、可重新生成。

---

## 二、直接使用

1. 启动游戏，进入主菜单或存档
2. 双击 `release\KTCTrainer.exe`
3. 面板显示 `修改器已生效 ✓` 后回游戏，按 `F1` 加金币

详细说明见 `release\使用说明.md`。

---

## 三、重新编译

需要 **MSVC（Visual Studio Build Tools，含 C++ 工具链）+ Windows SDK**。

```bat
build.bat all        :: 编译全部：recon.dll / inject.exe / ktc_mod.dll / KTCTrainer.exe
build.bat mod        :: 只编译修改器本体
build.bat launcher   :: 只编译控制面板
```

产物在 `build\`。`build.bat` 里 `VCVARS` 变量指向 `vcvars64.bat`，路径不同时改这一行即可。

编译好之后用 `tools\deploy.ps1` 注入测试（会自动拷贝成带时间戳的副本再注入，
因为 `LoadLibraryW` 对已加载的同名 DLL 不会重新执行 `DllMain`）。

---

## 四、技术要点

- **不写死任何内存地址**：全部通过游戏自带的 IL2CPP 运行时 API
  （`GameAssembly.dll` 导出 241 个 `il2cpp_*` 函数）按**类名/字段名/方法名**在运行时解析，
  天然免疫 ASLR，小版本更新通常不受影响。
- **8 个 hook**（MinHook 内联）：

  | Hook | 用途 |
  |---|---|
  | `Kingdom.Update` | 主线程 tick（所有托管调用都在这里执行，避免线程安全问题） |
  | `Player.LoseCrown` | 王冠不掉落（丢冠逻辑独立于血量，必须单独拦） |
  | `Damageable.ReceiveDamage` | 子民/建筑无敌（白名单直接吞掉伤害） |
  | `Damageable.ReceiveFatalDamage` | 同上，拦截"直接处死"路径 |
  | `BeggarCamp.<SlowUpdate>.MoveNext` | 刷新点快速刷人（改协程状态机内部的值） |
  | `ConstructionBuildingComponent.Awake` | 抓住每个新建工地（含 inactive 物体） |
  | `ConstructionBuildingComponent.InitializeBuild` | 同上 |
  | `ConstructionBuildingComponent.ConstructionComplete` | 观测游戏完工流程 / 兜底补调 |

- **面板 ↔ 修改器**用**命名共享内存**通信（`KTCTrainer_Cfg_v4`），
  所以面板能随时开关功能、实时显示金币/宝石/各功能状态。改结构体布局时必须同步改这个映射名。
- **热切换**：重复注入时新实例会让旧实例自动卸载 hook（`generation` 计数），不用重启游戏。
- 修改器线程创建于 `DllMain`，而 `DllMain` 期间加载器锁被持有 —— 因此线程先 `Sleep(700)`
  再碰 `GetModuleHandle`/`GetProcAddress`，否则会死锁。

---

## 五、版本

- **v1.1** — 修复建造秒完成（真正走游戏完工通道）、修复刷新点快速刷人（hook 协程状态机）、
  新增「清空钱袋」按钮
- **v1.0** — 首个版本，10 项功能

更新细节见 `release\使用说明.md` 的「更新记录」。

---

## 六、免责声明

- 本项目仅供**个人学习、研究与单机游戏体验**使用，请勿用于任何商业用途或联机竞技场景。
- 使用本工具产生的一切后果由使用者自行承担。请支持正版游戏。
- 本项目**不包含任何游戏文件、游戏资源或反编译的游戏代码**，
  只通过游戏自身公开的 IL2CPP 运行时接口在内存中读写数值。
- `docs\` 下的分析报告是运行时元数据（类名/字段名/偏移）的整理记录，用于技术研究。

## 七、致谢

- [MinHook](https://github.com/TsudaKageyu/minhook) — 内联 hook 库（BSD-2，见 `third_party`）
- 「刷新点快速刷人」的实现思路参考了 **ColonelRVH** 的 Cheat Engine 表
  （*Kingdom Two Crowns Trainer*）中 hook 刷新协程的做法；本项目仅参考思路并自行用 IL2CPP API
  重新实现，**未包含其任何代码或脚本**。
- 游戏《Kingdom Two Crowns》版权归 **Noio / Raw Fury** 所有。

## 八、许可

本项目源代码采用 **MIT License**（见 `LICENSE`）。
`third_party\minhook-master\` 为第三方库，遵循其自带的 BSD-2 许可证。
