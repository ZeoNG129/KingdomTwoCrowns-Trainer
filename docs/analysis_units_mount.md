# Kingdom Two Crowns 2.4.2 (Unity 6000.0.66f2 / IL2CPP / x64) — 单位·子民·坐骑·刷人点 逆向分析报告

> **用途**：为注入式原生 DLL（直接读写内存 + 内联 hook 原生函数指针）提供可直接编码的偏移与地址。
> **数据来源**：`E:\AI\KTC-Trainer\dump\dump.txt`（38 MB，运行时从进程内 IL2CPP 导出）、`index.txt`、`images.txt`、`gameassembly_exports.txt`。
> **GameAssembly.dll 基址（导出时）**：`0x00007FFF79E50000`，SizeOfImage `0x4664000`（见 `dump.txt` 第 1 行）。
> 本报告中所有 `p0=` 绝对地址均属于该次运行；**实际 DLL 必须使用 RVA**（`实际地址 = GetModuleHandleA("GameAssembly.dll") + RVA`），ASLR 每次启动都会改变基址。RVA 对照表见 §11。

---

## 0. 摘要（先看这段）

| 目标 | 结论 | 核心字段 / 方法 |
|---|---|---|
| **1. 马匹快速奔跑 + 无限耐力** | 可行。速度真身在 `Mover`，耐力真身在 `Steed` | `Steed.walkSpeed` +0x170、`runSpeed` +0x174、`<Stamina>` +0x44C、`_mover` +0x348；`Mover._moveSpeed` +0x30、`_multiplier` +0x34 |
| **2. 己方子民无敌** | 可行。**不要**用 `Damageable._playerRef` 当唯一依据（推断，未经 IL 证实）；推荐用 `Kingdom._characters` 集合 + `Character._damageable` +0xC8，写 `Damageable._isInvulnerable` +0x100 | `Kingdom._characters` +0x1F8；`Character._damageable` +0xC8；`Damageable._isInvulnerable` +0x100、`_hitPoints` +0x104 |
| **3. 野外刷新点快速刷人** | 「野外刷人点」= **`BeggarCamp`**（不是 `Vagrant`，游戏里没有 `Vagrant` 类；流浪者类名是 `Beggar`） | `BeggarCamp.maxBeggars` +0x20、`spawnInterval` +0x24、`hungerInterval` +0x28、`_beggars` +0x30；`Kingdom.<BeggarCamps>` +0x2B8 |
| **4. 单位类体系** | 见 §7。**没有** `UnitBase`/`Subject`/`Soldier`/`Spearman`/`Swordsman`/`Squire` 这些类；持有 `Damageable` 引用的是各单位自身 + 通用组件 `Character` | §7 |

**三个必须纠正的直觉错误**（详见 §2、§4、§6）：

1. **`APSpeed` 不是速度数值，是 Animator 参数哈希。** `Player.APSpeed`、`Mover.APSpeed`、`Rider.APSpeed`、`Steed.APSpeed`、`Worker.APSpeed`、`Archer.APSpeed` 全是 `static System.Int32`，与 `APHammer / APSlash / APRally / APLeap / APRage / APStunned / APBump / APDie / APSpawn` 等同一批——典型 `static readonly int APSpeed = Animator.StringToHash("Speed")`。**写它不会改速度，只会破坏动画状态机。**
2. **`Damageable` 里没有 `TakeDamage`。** 真正的入口是 `ReceiveDamage`（argc=3 / argc=4）和 `ReceiveFatalDamage`（argc=2）。
3. **`DebugInfiniteStamina` 确实是无限耐力开关（证据链很强），但 `Damageable._playerRef` 的语义无法从 dump 证实**——dump 只有元数据没有 IL 体，本报告对推断项全部显式标注。

---

## 1. 内存模型基础（编码前必读）

### 1.1 对象布局

dump 中的实例字段 `off` **已经是相对对象指针（即 klass 指针所在地址）的最终偏移**，可直接 `*(T*)((uint8_t*)obj + off)`。

证据（`dump.txt`）：

```
[CLASS] Damageable  :  UnityEngine.MonoBehaviour   size=0x110 flags=0x100001 [MonoBehaviour]
    F i off=0x0020 Damageable.PreDamageEvent              OnPreReceiveDamage
[CLASS] SteedData  :  System.Object   size=0x28 flags=0x102001
    F i off=0x0010 SteedType                              steedType
```

- `System.Object` 派生类第一个字段在 **0x10** → 对象头 = 0x10 字节（`Il2CppObject{ klass; monitor; }`）。
- `MonoBehaviour` 派生类第一个字段在 **0x20** → Unity `Object` 再占 0x10。
- `Damageable size=0x110`，最后一个字段 `<isDead>k__BackingField` 在 0x0109 → 自洽。

### 1.2 静态字段

`F S off=0xXXXX` 的 off 是相对 **`il2cpp_class_get_static_field_data(klass)`** 返回块的偏移。

⚠️ **陷阱**：`il2cpp_field_get_offset` 对 **literal/const** 字段返回 `0`。判断方法看同一类里静态字段的偏移是否**递增且互不冲突**：

```
[CLASS] Player  :  UnityEngine.MonoBehaviour   size=0x290 ...
    F S off=0x0000 System.Boolean    DebugInfiniteStamina      <- 真字段（0）
    F S off=0x0001 System.Boolean    DebugReplaceSteed         <- 真字段（1）
    F S off=0x0004 System.Int32      APSpeed                   <- 真字段（4）
    F S off=0x0008 UnityEngine.Collider2D[] overlapResults      <- 真字段（8）
```

→ 4 个不同递增偏移，**都是真实静态存储**，可以写。

对比（全是 const 字面量，offset 全 0，不可写）：

```
[CLASS] RiderData  :  UnityEngine.ScriptableObject   size=0x38 ...
    F S off=0x0000 System.String  PATH
    F S off=0x0000 System.String  FILENAME
    F S off=0x0000 System.String  PREFAB_FOLDER
    F S off=0x0000 RiderData      _inst          <- 这个是真字段，恰好也是 0
    F S off=0x0008 Dictionary<...> _ridersLockedAsDLC
```

### 1.3 数组布局（枚举时用）

`Il2CppArray`：`[0x00] klass`、`[0x08] monitor`、`[0x10] bounds`、`[0x18] max_length (uintptr)`、**`[0x20]` 起为元素**。
即 `count = *(uintptr_t*)((char*)arr + 0x18); elem[i] = *(void**)((char*)arr + 0x20 + 8*i);`

### 1.4 方法地址（p0）与 hook 安全性

- `p0` = `*(void**)MethodInfo`，即 IL2CPP 编译后的原生入口。`p1` = `*((void**)m+1)`，绝大多数方法与 `p0` 相同；不同时（如 `Payable.Pay p0=00007FFF7A134F80 p1=00007FFF7A134F60`）说明它是共享泛型桩。
- **`flags` 里的 0x40 = virtual**。含 0x40 的方法（如 `Damageable.ReceiveDamage` argc=4 `flags=0x1C6`）通过接口/vtable 分派时可能走别的实现，**优先 hook 非虚版本**。
- hook 前务必用长度反汇编器确认前 5 字节可覆盖；IL2CPP 方法常见开头 `48 83 EC 28`（`sub rsp,28h`）后接 `call`，一般足够。保险做法写 12 字节绝对跳转（`48 B8 <imm64> FF E0`）。

### 1.5 进程入口链（怎么拿到 Kingdom / Player / Managers）

**没有** `Kingdom._instance` 静态字段（`Manager` 只有 4 个方法、0 个字段）。正确入口是 `Managers` 单例：

```
[CLASS] Managers  :  Coatsink.Common.SingletonMonoBehaviour`1   size=0x130 ...
    F i off=0x0020 World        world
    F i off=0x0038 EnemyManager enemies
    F i off=0x0058 Kingdom      kingdom        <-- 关键
    F i off=0x0080 Game         game
    F i off=0x0108 PoolManager  pools

[CLASS] Coatsink.Common.SingletonMonoBehaviour`1  :  UnityEngine.MonoBehaviour   size=0x0 ...
    F S off=0x0000 T          _Inst            <-- 静态单例（在泛型基类里）
    M get_Inst  ret=T argc=0 flags=0x896 p0=00007FFF7A235DD0(GA)
```

链：`Managers` klass → `il2cpp_class_get_parent()` 得到已膨胀的 `SingletonMonoBehaviour`1<Managers>` → `il2cpp_class_get_field_from_name(parent, "_Inst")` → `il2cpp_field_static_get_value()` → `Managers*` → `+0x58` = `Kingdom*`。

其他备选根：

| 路径 | 偏移 | 说明 |
|---|---|---|
| `MainCamera._instance` | 静态 +0x08 | `F S off=0x0008 MainCamera _instance` |
| `Game._mainCamera` | +0x98 | 需先有 `Game`（`Managers.game` +0x80） |
| `Kingdom.playerOne / playerTwo` | +0x80 / +0x88 | `F i off=0x0080 Player playerOne` |
| `Kingdom._activePlayers` | +0xD0 | `Player[]` |
| `Kingdom.GetPlayer(int)` | p0=00007FFF7A3F3110 (RVA 0x5A3110) | 实例方法 |
| `DroppableRegistrar._kingdomRef` | 静态 +0x08 | `F S off=0x0008 Kingdom _kingdomRef` |

---

## 2. `Player` 类的三个静态调试开关

### 2.1 原始定义行（dump 证据）

```
[CLASS] Player  :  UnityEngine.MonoBehaviour   size=0x290 flags=0x100001 [MonoBehaviour]
    F S off=0x0000 System.Boolean                         DebugInfiniteStamina
    F S off=0x0001 System.Boolean                         DebugReplaceSteed
    F S off=0x0004 System.Int32                           APSpeed
    F S off=0x0008 UnityEngine.Collider2D[]               overlapResults
    F S off=0x0000 System.Single                          directionTimeout
    F S off=0x0000 System.Int32                           directionFlipReqCount
```

（最后两行 offset=0 与前面的冲突 → 是 `const`，忽略。）

### 2.2 `APSpeed` —— **不是速度值，是 Animator 参数哈希**

`AP*` 命名在全工程反复出现，全部是 `System.Int32` 静态字段，且每类各有一份：

| 类 | 静态 `AP*` 字段（off 递增） |
|---|---|
| `Player` | `APSpeed`@0x04 |
| `Mover` | `DebugDisableMovement`@0x00(bool), `APSpeed`@0x04 |
| `Rider` | `SPECIAL_ANIMATION`(const)@0, `APSpeed`@0x00 |
| `Steed` | `APRear`@0x00, `APEat`@0x04, `APPuff`@0x08, `APSpeed`@0x0C, `APIsBorn`@0x10, `APSpit`@0x14, `APManualAttack`@0x18 |
| `Worker` | `ASStand`@0x00, `APHammer`@0x04, `APPush`@0x08, `APAttack`@0x0C, `APShieldWall`@0x10, `APChopTree`@0x14, `APSpeed`@0x18 |
| `Archer` | `APPrepare`@0x54, `APShoot`@0x58, `APShootPerfect`@0x5C, `APAttack`@0x60, `APUseSpear`@0x64, `APUseShield`@0x68, `APIdleness`@0x6C, `APSpeed`@0x70 |
| `Knight` | `APSlash`@0x00, `APBlock`@0x04, `APRally`@0x08, `ASStand`@0x0C, `APLand`@0x10, `APRetreat`@0x14, `APIdleness`@0x18 |
| `Berserker` | `APRage`@0x00, `APAttack`@0x04, `APLeap`@0x08, `APLeapLand`@0x0C |
| `Peasant` | `APBump`@0x00, `APGetUp`@0x04, `APGround`@0x08 |
| `Beggar` | `APStunned`@0x00, `APEat`@0x04 |
| `Damageable` | `APDie`@0x08 |
| `CaveEnemySpawner` / `EnemyWaveSpawner` | `APSpawn` |
| `SteedAbility` | `APSpit`@0x00, `APManualAttack`@0x04, `APManualAttackDone`@0x08 |

**判定依据**：
1. 语义上「AP = Animation Parameter」；`APShootPerfect`、`APShieldWall`、`APChopTree`、`APLeapLand`、`APGetUp` 这些**不可能是速度或数值参数**，只能是 Animator 参数名。
2. 每个类都有 `.cctor`（静态构造）——正是 `Animator.StringToHash` 初始化的位置：
   ```
   M .cctor   ret=System.Void argc=0 flags=0x1891 p0=00007FFF7A4EEEB0(GA)   <- Player..cctor, RVA 0x69EEB0
   ```
3. `UnityEngine.Animator.StringToHash` 在 dump 中存在：`M StringToHash ret=System.Int32 argc=1 flags=0x96 p0=00007FFF7CA843D0(GA)`。

> **结论**：不要写 `APSpeed`。真正的速度在 `Mover`（§4.3）和 `Steed`（§3.2）。

### 2.3 `DebugInfiniteStamina` —— 证据链与结论

**dump 里能直接看到的引用点**（dump 无 IL 体，只能靠符号名 + 结构模式，下面标注置信度）：

| 证据 | 原始行 | 置信度 |
|---|---|---|
| 调试 UI 有「无限耐力」按钮字段 | `DebugTools  F i off=0x00E0 UnityEngine.UI.Button  infiniteStaminaButton` | 确定 |
| 该按钮的处理函数存在 | `DebugTools  M OnButtonInfiniteStamina ret=System.Void argc=0 flags=0x86 p0=00007FFF7A8236A0(GA)` | 确定 |
| 全工程 `stamina` 相关成员里，唯一的静态布尔开关就是它 | 见下方全量搜索结果 | 确定 |
| 同模式的旁证：`Mover.DebugDisableMovement`（静态 bool@0x00）↔ `DebugTools.OnButtonDisableMovement p0=00007FFF7A822340` + `disableMovementButton@0x02E0`；`Payable.DebugDisableHighlights` ↔ `DebugTools.OnButtonHideShowHighlights p0=00007FFF7A823510` + `showHighlightsButton@0x02D0` | `DebugTools` 字段表 | 强 |
| 「`OnButtonInfiniteStamina` 内部写 `Player.DebugInfiniteStamina`」 | 无 IL，无法直接证实 | 推断（高） |

全工程 `stamina` 关键字搜索结果（`dump.txt` 全量）：

```
DebugTools  || F i off=0x00E0 Button          infiniteStaminaButton
DebugTools  || M OnButtonInfiniteStamina      p0=00007FFF7A8236A0
NetworkPostbox || F S off=0x0000 Int16        SteedStaminaID
Player      || F S off=0x0000 Boolean         DebugInfiniteStamina
Player      || F i off=0x01FC Int32           _staminaRPCIndex
Player      || M SendStaminaState             p0=00007FFF7A4E9EA0
Player      || M ReceiveStaminaState          p0=00007FFF7A4E8530
Steed       || F i off=0x017C Single          walkStaminaRate
Steed       || F i off=0x0180 Single          runStaminaRate
Steed       || F i off=0x0184 Single          glideStaminaRate
Steed       || F i off=0x0188 Single          standStaminaRate
Steed       || F i off=0x018C Single          reserveStamina
Steed       || F i off=0x01B0 Single          eatFullStaminaDelay
Steed       || F i off=0x044C Single          <Stamina>k__BackingField
Steed       || M get_Stamina                  p0=00007FFF7A5F3C10
Steed       || M set_Stamina                  p0=00007FFF7A5F4050
SteedAbility|| F i off=0x0020 Single          _staminaCost
GlideMovementSteedAbility || F i off=0x0068 Single _staminaGlideRegen
RunningAttackSteedAbility || F i off=0x0060 Single _attackStaminaCost
RunningAttackSteedAbility || F i off=0x0064 Single _attackStaminaGain
SteedType   || F S off=0x0000 SteedType       HorseStamina
```

**关键推论**：`DebugInfiniteStamina` 挂在 **`Player`** 上，而**耐力数值**挂在 **`Steed.<Stamina>k__BackingField`（+0x44C）** 上，两者通过 `Player._steed`（+0x190）关联。所以这个开关几乎肯定是「消耗耐力前先查 `Player.DebugInfiniteStamina`，为 true 就跳过扣除」，最可能的位置是 `Steed.Update`（p0=00007FFF7A5F3630, RVA 0x7A3630）。

**回答你的问题：把 `DebugInfiniteStamina` 设成 true 能不能实现无限耐力？**

> **能，但置信度约 85%，不是 100%。** 判断依据：
> - ✅ 名字 + 类型（`static bool`）+ 调试按钮一一对应，且同类模式（`DebugDisableMovement` / `DebugDisableHighlights`）在本工程里都确实是「静态 bool 开关 + DebugTools 按钮」结构；
> - ✅ 静态偏移 0x0000 是真实存储（不是 const），可写；
> - ⚠️ 但 dump 无 IL 体，无法证明 `Steed.Update` 真的读它；也无法排除它只在某个 Debug 构建路径下生效。
>
> **建议做法（两段式）**：
> 1. 先写 1 字节 `*(uint8_t*)(Player_static + 0x00) = 1`，骑马冲刺，用 Cheat Engine / 你自己的 DLL 观察 `*(float*)(steed + 0x44C)` 是否停止下降。**这是唯一确定的验证手段。**
> 2. **无论如何都加上兜底**：在后台线程每帧（或每 2 帧）把 `*(float*)(steed + 0x44C)` 写回 `1.0f`，同时把 `Steed._tiredTimer`（+0x3F8）写 0、`Player.get_IsTired` 自然为 false。兜底方案不依赖任何推断。

**`DebugReplaceSteed`（off=0x0001）**：同为 `static bool`，紧邻 `DebugInfiniteStamina`。结合 `Player.standardSteedPrefab`（+0x0030，类型 `Steed`）、`Player._steed`（+0x190）、`Player.Ride(Steed,bool,bool)`（p0=00007FFF7A4E8F40, RVA 0x698F40）与 `DebugTools.removeSteedButton`（+0x02F0）+ `OnButtonRemoveSteedAfterSwitch`（p0=00007FFF7A824750）判断：它是「换马时用 `standardSteedPrefab` 替换/移除当前坐骑」的调试开关，**与耐力无关**。写它风险高（会换掉你的坐骑），不建议动。

---

## 3. 坐骑类体系

### 3.1 体系全貌（重要：**`Steed` 没有子类**）

`index.txt` 中 `parent=Steed ` 的类：**0 个**。所有坐骑共用同一个 `Steed` 类，靠 `SteedType` 枚举 + 挂载不同的 `SteedAbility` 组件区分。

```
Payable (MonoBehaviour, size=0x140)
  └── Steed (size=0x490, F=133, M=87, MB)          klass=000001CFB1B73FF0
        ├── 组件 SteedAbility (size=0x60, MB)       klass=000001CFB17C18E0
        │     ├── BuffUnitsSteedAbility         size=152 F=10 M=13
        │     ├── ChargeAttackSteedAbility      size=112 F=2  M=6
        │     ├── ChargeFireSteedAbility        size=136 F=5  M=6
        │     ├── ChargeFlySteedAbility         size=144 F=8  M=7
        │     ├── DayNightRecolorSteedAbility   size=296 F=18 M=16
        │     ├── DayNightSpeedSteedAbility     size=120 F=5  M=14   <-- 速度
        │     ├── DropCoinsSteedAbility         size=112 F=2  M=8
        │     ├── EmpowerGroundSteedAbility     size=136 F=6  M=7
        │     ├── GlideMovementSteedAbility     size=184 F=16 M=11  <-- 速度/耐力
        │     ├── JumpAttackSteedAbility        size=208 F=18 M=15
        │     ├── KelpieSteedAbility            size=208 F=15 M=19
        │     ├── PushAttackSteedAbility        size=104 F=1  M=6
        │     ├── RunningAttackSteedAbility     size=128 F=5  M=7   <-- 耐力
        │     ├── SpeedBoostSteedAbility        size=200 F=13 M=9   <-- 速度
        │     ├── SpitSteedAbility              size=192 F=14 M=23
        │     ├── SummonGhostSteedAbility       size=232 F=20 M=26
        │     ├── WheelieSteedAbility           size=112 F=2  M=5
        │     └── WolfAttackSteedAbility        size=112 F=2  M=11
        ├── 数据 SteedData (System.Object, size=0x28)
        ├── 数据 SteedSpawnData (size=0x20)
        ├── 值类型 SteedStatus (size=0x20)
        ├── 枚举 SteedType (42 项) / SteedMode (4 项) / SteedPosition (6 项)
        └── 辅助 SteedAnimHelper / SteedSpawn(Payable) / SteedWaveShaderAnimator / BiomeSteedAudioSwapper
```

`SteedType` 全部取值（`F S off=0x0000 SteedType XXX`）：
`INVALID, Bear, P1Griffin, Lizard, Reindeer, Spookyhorse, Stag, Unicorn, P1Warhorse, P1Default, P2Default, HorseStamina, HorseBurst, HorseFast, P1Wolf, Trap, Barrier, Bloodstained, P2Wolf, P2Griffin, P2Warhorse, P2Stag, Gullinbursti, Sleipnir, Reindeer_Norselands, CatCart, Kelpie, DayNight, P2Kelpie, P2Reindeer_Norselands, Hippocampus, Cerberus, Spider, TheChariotDay, TheChariotNight, Pegasus, Donkey, MolossianHound, Chimera, RainbowPony, Total`

`SteedMode`：`Free, Player, Loader, Stabled`

### 3.2 `Steed` 速度 / 耐力 / 冲刺相关字段（**核心表**）

```
[CLASS] Steed  :  Payable   size=1168 (0x490) F=133 M=87 [MonoBehaviour]
```

| 偏移 | 类型 | 字段名 | 用途 |
|---|---|---|---|
| +0x0150 | SteedType | `steedType` | 坐骑种类（枚举） |
| +0x0170 | float | **`walkSpeed`** | 走速（**改这个**） |
| +0x0174 | float | **`runSpeed`** | 跑速 / 冲刺速度（**改这个**） |
| +0x0178 | float | `forestSpeedMultiplier` | 林地速度倍率 |
| +0x017C | float | **`walkStaminaRate`** | 走时耐力消耗率 |
| +0x0180 | float | **`runStaminaRate`** | 跑时耐力消耗率（**改 0 = 无限耐力**） |
| +0x0184 | float | **`glideStaminaRate`** | 滑翔耐力消耗率（狮鹫/飞马） |
| +0x0188 | float | **`standStaminaRate`** | 站立耐力回复率 |
| +0x018C | float | `reserveStamina` | 保底耐力 |
| +0x0190 | float | `reserveProbability` | 触发保底的概率 |
| +0x0194 | float | `glidePayThreshold` | 滑翔扣耐力阈值 |
| +0x0198 | float | `_puffLoopTimer` | 喘气循环计时 |
| +0x019C | float | `PuffThreshold` | 喘气阈值 |
| +0x01A0 | float | `rearingDuration` | 扬蹄时长 |
| +0x01A4 | float | `rearingCooldown` | 扬蹄冷却 |
| +0x01A8 | bool | `canEat` | 能否吃草 |
| +0x01AC | float | `eatDelay` | 进食延迟 |
| +0x01B0 | float | `eatFullStaminaDelay` | 吃饱后满耐力延迟 |
| +0x01B4 | float | `eatDuration` | 进食时长 |
| +0x01B8 | bool | `onlyEatsAtNight` | 仅夜间进食 |
| +0x01BC | float | `wellFedDuration` | 饱腹持续时间 |
| +0x01C0 | float | **`tiredDuration`** | 疲劳持续时间（**改 0 = 不疲劳**） |
| +0x01C4 | float | `eatAmbientThreshold` | 环境进食阈值 |
| +0x01D4 | bool | `attractsDeer` | 吸引鹿 |
| +0x01D8 | float | `wanderRange` | 游荡范围 |
| +0x0233 | bool | `resumesGallopingAfterUsingAbility` | 技能后恢复奔跑 |
| +0x0340 | Rigidbody2D | `_rigidbody` | 刚体 |
| +0x0348 | **Mover** | **`_mover`** | **真正的移动组件** |
| +0x03E0 | Farmhouse | `_currentFarmhouse` | 所属马厩 |
| +0x03E8 | float | `_homePosition` | 家位置 |
| +0x03EC | bool | `_hasBeenRidden` | 是否被骑过 |
| +0x03F0 | float | `_allowedIdleTime` | 允许闲置时间 |
| +0x03F4 | bool | `_birthing` | 出生中 |
| +0x03F5 | bool | `_canPay` | 可支付 |
| +0x03F8 | float | **`_tiredTimer`** | **疲劳计时器（写 0）** |
| +0x0400 | SteedAbility[] | `steedAbilities` | 技能组件数组 |
| +0x0440 | Player | `<Rider>k__BackingField` | 当前骑手 |
| +0x0448 | float | `<WellFedTimer>k__BackingField` | 饱腹计时 |
| +0x044C | float | **`<Stamina>k__BackingField`** | **耐力真身（0.0~1.0）（写 1.0f）** |
| +0x0450 | Persistent | `<Persistent>k__BackingField` | 持久化 |
| +0x0458 | Transform | `<Passenger>k__BackingField` | 乘客挂点 |
| +0x0460 | SteedMode | `<CurrentMode>k__BackingField` | 当前模式 |
| +0x0468 | Transform | `riderAnchor` | 骑手锚点 |
| +0x0470 | Rider | `activeRider` | 当前 Rider 视觉 |
| +0x0478 | List\<Rider\> | `_steedRiders` | 骑手视觉列表 |
| +0x0488 / +0x0489 | bool | `_footSparklesActive` / `_puffActive` | 特效状态 |

**全部含 stamina/speed/run/gallop/tired 关键字的字段名清单**：
`walkSpeed, runSpeed, forestSpeedMultiplier, walkStaminaRate, runStaminaRate, glideStaminaRate, standStaminaRate, reserveStamina, reserveProbability, glidePayThreshold, PuffThreshold, _puffLoopTimer, rearingDuration, rearingCooldown, eatDelay, eatFullStaminaDelay, eatDuration, wellFedDuration, tiredDuration, eatAmbientThreshold, resumesGallopingAfterUsingAbility, _tiredTimer, _allowedIdleTime, <Stamina>, <WellFedTimer>, <CurrentMode>`
静态：`APSpeed, APRear, APEat, APPuff, APIsBorn, APSpit, APManualAttack`（**全是动画参数哈希，勿写**）

### 3.3 `Steed` 关键方法（p0 / RVA）

| 方法 | argc | flags | p0 | RVA |
|---|---|---|---|---|
| `get_Stamina` | 0 | 0x886 | 00007FFF7A5F3C10 | 0x7A3C10 |
| `set_Stamina` | 1 | 0x886 | 00007FFF7A5F4050 | 0x7A4050 |
| `get_WellFedTimer` | 0 | 0x886 | 00007FFF7A5F3C60 | 0x7A3C60 |
| `set_WellFedTimer` | 1 | 0x886 | 00007FFF7A5F4060 | 0x7A4060 |
| `get_IsTired` | 0 | 0x886 | 00007FFF7A5F3B90 | 0x7A3B90 |
| `get_TiredTime` | 0 | 0x886 | 00007FFF7A5F3C20 | 0x7A3C20 |
| `BecomeTired` | 0 | 0x86 | 00007FFF7A5EE420 | 0x79E420 |
| `Eat` | 0 | 0x86 | 00007FFF7A5EE920 | 0x79E920 |
| `EatingDone` | 0 | 0x86 | 00007FFF7A5EEC10 | 0x79EC10 |
| `Update` | 0 | 0x81 | **00007FFF7A5F3630** | **0x7A3630** |
| `ForceGallopAnimation` | 0 | 0x86 | 00007FFF7A5EEDE0 | 0x79EDE0 |
| `SetMode` | 2 (SteedMode,Player) | 0x86 | 00007FFF7A5F20A0 | 0x7A20A0 |
| `SetGroundType` | 1 | 0x86 | 00007FFF7A5F1FC0 | 0x7A1FC0 |
| `SetImmobile` | 1 | 0x86 | 00007FFF7A5F1FF0 | 0x7A1FF0 |
| `SetStand` | 0 | 0x86 | 00007FFF7A5F3100 | 0x7A3100 |
| `SetAnimation` | 1 | 0x1E6 | 00007FFF7A5F1C30 | 0x7A1C30 |
| `Stable` | 1 | 0x86 | 00007FFF7A5F3210 | 0x7A3210 |
| `get_CurrentMode` | 0 | 0x886 | 00007FFF7A5F3B50 | 0x7A3B50 |
| `OnAnimGallopStart` | 0 | 0x86 | 00007FFF7A5EFC60 | 0x79FC60 |
| `Pay` | 0 | 0xC6 | 00007FFF7A5F0FA0 | 0x7A0FA0 |
| `CanPay` | 1 | 0xC6 | 00007FFF7A5EE5D0 | 0x79E5D0 |
| `.cctor` | 0 | 0x1891 | 00007FFF7A5F36D0 | 0x7A36D0 |

### 3.4 `SteedAbility`（技能组件）字段

```
[CLASS] SteedAbility  :  UnityEngine.MonoBehaviour   size=0x60 ...
    F S off=0x0000 Int32   APSpit / off=0x0004 APManualAttack / off=0x0008 APManualAttackDone
    F i off=0x0020 Single  _staminaCost        <-- 技能耐力消耗
    F i off=0x0024 Single  _duration
    F i off=0x0028 Boolean _controlMovement
    F i off=0x0029 Boolean _isAutomaticAbility
    F i off=0x002A Boolean _blocksPlayerPaymentWhenActive
    F i off=0x002C Single  _cooldown
    F i off=0x0038 Steed   _steed
    F i off=0x0040 Player  _rider
    F i off=0x0048 Animator _animator
    F i off=0x0050 Boolean _isAbilityActive
    F i off=0x0054 Single  _nextActivationTime
    F i off=0x0058 Single  _abilityActiveTime
    M Activate p0=00007FFF7A5EA260 / Deactivate p0=00007FFF7A134F80 / ManualAttack p0=00007FFF7A5EA630
```

速度相关子类字段：

| 类 | 偏移 | 类型 | 字段 | 说明 |
|---|---|---|---|---|
| `SpeedBoostSteedAbility` | +0x0060 | float | `abilityActiveTime` | 加速持续 |
| | +0x0068 | AnimationCurve | `speedMultiplier` | **加速曲线（改它=改冲刺倍率）** |
| | +0x0070 | enum | `availability` | 可用条件 |
| | +0x00C0 | Player.ActionState | `actionState` | 触发状态 |
| `DayNightSpeedSteedAbility` | +0x0060/64/68/6C/70 | float | `_dawnSpeed` / `_daySpeed` / `_eveningSpeed` / `_nightSpeed` / `_redMoonSpeed` | **昼夜速度（Chariot）** |
| `GlideMovementSteedAbility` | +0x0060 | float | `_speedBoostProportion` | 滑翔加速比例 |
| | +0x0064 | float | `_speedFalloffMultiplier` | 衰减倍率 |
| | +0x0068 | float | `_staminaGlideRegen` | 滑翔耐力回复 |
| | +0x006C | float | `_maxSpeed` | **最大速度** |
| | +0x0070 | float | `_minSpeed` | 最小速度 |
| | +0x0074 | float | `_pushOffSpeed` | 起飞速度 |
| | +0x0098 | float | `_goalSpeed` | 目标速度 |
| `RunningAttackSteedAbility` | +0x0060 | float | `_attackStaminaCost` | 冲撞耐力消耗 |
| | +0x0064 | float | `_attackStaminaGain` | 冲撞耐力回复 |

### 3.5 `SteedData` / `SteedSpawnData` / `SteedStatus`

```
[CLASS] SteedData  :  System.Object   size=0x28 flags=0x102001
    F i off=0x0010 SteedType  steedType ; off=0x0014 SteedMode mode ; off=0x0018 Single homePosition
    F i off=0x001C Boolean    hasBeenRidden ; off=0x0020 Int32 price
[CLASS] SteedSpawnData : System.Object size=0x20
    F i off=0x0010 Int32 price ; off=0x0014 Boolean needsGems ; off=0x0018 CurrencyType currency
[CLASS] SteedStatus : System.ValueType size=0x20
    F i off=0x0010 SteedPosition position ; off=0x0014 Int32 player ; off=0x0018 Int32 land ; off=0x001C Boolean hasBeenRidden
```

`Kingdom` 里的坐骑注册表：`spawnedSteeds` List\<Steed\> @ +0x0160，`steedSpawns` List\<SteedSpawn\> @ +0x0168。

---

## 4. `Rider` / `RiderData` / `Mover`

### 4.1 `Rider`（纯视觉，**没有速度字段**）

```
[CLASS] Rider  :  UnityEngine.MonoBehaviour   size=0x78 (120) F=14 M=12
    F S off=0x0000 System.String                          SPECIAL_ANIMATION   (const)
    F S off=0x0000 System.Int32                           APSpeed             (Animator hash)
    F i off=0x0020 RiderCrown                             activeCrown
    F i off=0x0028 MonarchType                            riderModel
    F i off=0x002C RulerGender                            rulerGender
    F i off=0x0030 UnityEngine.GameObject                 torch
    F i off=0x0038 System.Collections.Generic.List<RiderCrown> allCrowns
    F i off=0x0040 BiomeHolder.Biomes[]                   supportedBiomes
    F i off=0x0048 System.Boolean                         useReins
    F i off=0x0050 UnityEngine.RuntimeAnimatorController  ghostController
    F i off=0x0058 List<Rider.SkinFallback>               _perBiomeModelFallbacks
    F i off=0x0060 UnityEngine.Animator                   <Animator>k__BackingField
    F i off=0x0068 UnityEngine.AnimatorOverrideController _animatorOverrideController
    F i off=0x0070 Rider.AnimationClipOverrides           _clipOverrides
    M SetAPSpeed(float) p0=00007FFF7A5698F0   <-- 只设置 Animator 的 "Speed" 参数，不改逻辑速度
```

> `Rider.SetAPSpeed(float)` 名字看着像速度，实际是给 Animator 喂 Speed 参数（驱动奔跑动画）。**逻辑速度在 `Mover`。**

`Player._activeRider` @ +0x00D0。

### 4.2 `RiderData`（纯资源表，无速度）

```
[CLASS] RiderData  :  UnityEngine.ScriptableObject   size=0x38
    F S off=0x0000 String PATH / FILENAME / PREFAB_FOLDER   (const)
    F S off=0x0000 RiderData _inst
    F S off=0x0008 Dictionary<MonarchType,DlcType> _ridersLockedAsDLC
    F i off=0x0018 List<RiderData.RiderDataPrivate> _riders
    F i off=0x0020 Dictionary<MonarchType,String> _ridersDict
    F i off=0x0028 Dictionary<Biomes,List<MonarchType>> _perBiomeRiders
    F i off=0x0030 Dictionary<Biomes,Dictionary<MonarchType,MonarchType>> _riderSkinFallbacks
    M GetRiderPrefab(MonarchType) p0=00007FFF7A582200 ; get_Inst p0=00007FFF7A582F70
```

### 4.3 `Mover`（**速度真身**）

```
[CLASS] Mover  :  UnityEngine.MonoBehaviour   size=0x90
    F S off=0x0000 System.Boolean                         DebugDisableMovement
    F S off=0x0004 System.Int32                           APSpeed                (Animator hash)
    F S off=0x0008 UnityEngine.Vector3                    LEFT
    F S off=0x0014 UnityEngine.Vector3                    RIGHT
    F S off=0x0000 System.Single                          PositionEpsilon        (const)
    F i off=0x0020 System.Single                          maxDistanceFromEscort
    F i off=0x0024 System.Single                          maxAccelerationPerFrame
    F i off=0x0028 System.Boolean                         shouldAvoidAreas
    F i off=0x002C System.Single                          _rubberBandDist
    F i off=0x0030 System.Single                          _moveSpeed          <-- 基础速度
    F i off=0x0034 System.Single                          _multiplier         <-- 速度倍率
    F i off=0x0038 System.Single                          _multiplierTimeout
    F i off=0x003C System.Single                          _pauseTimeout
    F i off=0x0040 System.Single                          _goalPosition
    F i off=0x0048 UnityEngine.GameObject                 _goalObject
    F i off=0x0050 System.Single                          _goalSpeed
    F i off=0x0054 Mover.GoalMode                         <goalMode>k__BackingField
    F i off=0x0058 System.Single                          _goalOffset
    F i off=0x005C Mover.OffsetMode                       _goalOffsetMode
    F i off=0x0060 UnityEngine.Rigidbody2D                <rigidbody>k__BackingField
    F i off=0x0068 List<UnityEngine.Animator>             _animators
    F i off=0x0070 Coatsink.Common.HagletValue<Boolean>   _movingToGoal
    F i off=0x0078 Mover.FacingMode                       facingMode
    F i off=0x0080 UnityEngine.GameObject                 facingTarget
    F i off=0x0088 Func<Single,Single,Boolean>            HasSpeedChanged
```

**速度相关方法**：

| 方法 | argc | p0 | RVA | 说明 |
|---|---|---|---|---|
| `get_Speed` | 0 | 00007FFF7A467C70 | 0x617C70 | 返回 `_moveSpeed`（可能已乘 `_multiplier`） |
| `get_ActualSpeed` | 0 | 00007FFF7A467C60 | 0x617C60 | 实际刚体速度 |
| `get_Multiplier` | 0 | 00007FFF7A31DBC0 | 0x4CDBC0 | 返回 `_multiplier` |
| `get_goalSpeed` | 0 | 00007FFF7A467C90 | 0x617C90 | 目标速度 |
| `SetSpeed(float)` | 1 | 00007FFF7A467280 | 0x617280 | 设置基础速度 |
| `SetSpeed(float,int)` | 2 | 00007FFF7A4671F0 | 0x6171F0 | 带方向 |
| `SetSpeedToGoal(float)` | 1 | 00007FFF7A4671E0 | 0x6171E0 | |
| `ForceSetSpeed(float,int)` | 2 | 00007FFF7A466710 | 0x616710 | 强制（绕过平滑） |
| **`SetSpeedMultiplier(float,float)`** | 2 | **00007FFF7A4671D0** | **0x6171D0** | **速度倍率 + 超时（改速度最优雅的入口）** |
| `ResetSpeedMultiplier` | 0 | 00007FFF7A466CF0 | 0x616CF0 | |
| `SetGoal(float,float)` | 2 | 00007FFF7A466F20 | 0x616F20 | |
| `SetGoalSpeed(float)` | 1 | 00007FFF7A466F10 | 0x616F10 | |
| `SetGoal(GameObject,float,float,OffsetMode)` | 4 | 00007FFF7A467070 | 0x617070 | |
| `Stop` / `ForceStop` | 0 | 00007FFF7A467300 / 00007FFF7A4667F0 | 0x617300 / 0x6167F0 | |
| `Pause(float)` / `UnPause` | 1 / 0 | 00007FFF7A466C70 / 00007FFF7A467340 | 0x616C70 / 0x617340 | |
| `Update` | 0 | 00007FFF7A467350 | 0x617350 | |
| `SetDirection(int)` | 1 | 00007FFF7A466D00 | 0x616D00 | |

**`Mover` 实例的获取路径**：
- `Player.<mover>k__BackingField` @ **+0x00F0**（`M get_mover p0=00007FFF7A4EF260`, RVA 0x69F260）
- `Steed._mover` @ **+0x0348**
- 各单位自身：`Character._mover` @ +0x00D0、`Worker._mover` @ +0x0078、`Archer._mover` @ +0x0130、`Knight._mover` @ +0x00D0、`Peasant._mover` @ +0x0048、`Beggar._mover` @ +0x0080、`Berserker.mover` @ +0x00E0、`Farmer._mover` @ +0x0070、`Fisher._mover` @ +0x0040

---

## 5. `Damageable` 全解

```
[CLASS] Damageable  :  UnityEngine.MonoBehaviour   size=0x110 flags=0x100001 [MonoBehaviour]
```

### 5.1 字段表

| 偏移 | 类型 | 字段 | 用途 |
|---|---|---|---|
| S+0x0000 | WaitForSeconds | `DESPAWN_DELAY` | 静态（真字段） |
| S+0x0008 | int | `APDie` | 静态 Animator hash（**勿写**） |
| +0x0020 | `Damageable.PreDamageEvent` | `OnPreReceiveDamage` | **伤害前事件（可改 ref int 伤害）** |
| +0x0028 | `Damageable.DamageEvent` | `OnReceiveDamage` | 受伤后事件 |
| +0x0030 | `System.Action` | `OnReceiveRemoteDamage` | 远端伤害 |
| +0x0038 | `Damageable.DamageEvent` | `OnReceiveDelayedDamage` | 延迟伤害 |
| +0x0040 | `System.Action` | `OnDelayDamageEnd` | 延迟伤害结束 |
| +0x0048 | `Damageable.DeathEvent` | `OnDeath` | 死亡事件 |
| +0x0050 | DamageSource | `damagedBy` | 可受哪些伤害源（位标志） |
| +0x0054 | DamageSurface | `surface` | 受击表面 |
| +0x0058 | bool | `useHitPoints` | 是否用 HP（false 时一击必杀） |
| +0x005C | int | `initialHitPoints` | 初始/最大 HP |
| +0x0060 | bool | `destroyOnDeath` | 死亡后销毁 |
| +0x0068/0x0070 | AudioEmitter / [] | `deathSound` / `biomeAlternateDeathSounds` | 音效 |
| +0x0078 | Color | `flashColor` | 受击闪色 |
| +0x0088/0x0090 | AudioEmitter | `hitSound` / `hitSoundArrow` | 音效 |
| +0x0098 | bool | `isLowPriority` | 低优先级 |
| +0x0099 | bool | `alwaysAttackable` | 总可被攻击 |
| +0x009A | bool | `persistOnDeath` | 死后保留 |
| +0x009B | bool | `ignoredWhenInvulnerable` | 无敌时被忽略 |
| +0x009C | bool | `isInvulnerableInitially` | 初始无敌 |
| +0x00A0 | SpriteRendererFX[] | `<_spriteFXs>k__BackingField` | |
| +0x00A8 | AnimationSync | `_animSync` | |
| +0x00B0 | IAnimReceiver | `_otherAnimRecv` | |
| +0x00B8 | IUnitControllable | `_unitControllable` | **→ 可拿到 PlayerId** |
| +0x00C0 | Dictionary\<GameObject,Coroutine\> | `activeContinualDamageRoutines` | 持续伤害协程 |
| +0x00C8 | bool | `syncHP` | 是否网络同步 HP |
| +0x00D0 | CRPCHeader | `parentHeaderRef` | RPC |
| +0x00D8..0x00E4 | int | `_hitFlashIndex` / `_hpIndex` / `_invulnerableIndex` / `_hitSoundIndex` | RPC 索引 |
| +0x00E8 | Rigidbody2D | `_rb` | |
| **+0x00F0** | **Player** | **`_playerRef`** | **阵营线索（语义见 §6.3，推断）** |
| +0x00F8 | IEnumerator | `_delayDamageRoutine` | |
| **+0x0100** | **bool** | **`_isInvulnerable`** | **无敌标志（写 1）** |
| **+0x0104** | **int** | **`_hitPoints`** | **当前 HP（写大值）** |
| +0x0108 | bool | `<IsInBlockingFormation>k__BackingField` | |
| +0x0109 | bool | `<isDead>k__BackingField` | 死亡标志 |

### 5.2 `Damageable` 全部含 Damage / Hit / Die / Kill 的方法

| 方法 | argc | flags | p0 | RVA | 备注 |
|---|---|---|---|---|---|
| **`ReceiveDamage`** | **3** (int, GameObject, DamageSource) | 0x86 | **00007FFF7A811780** | **0x9C1780** | **主入口，非虚，最佳 hook 点** |
| **`ReceiveDamage`** | **4** (+Vector2) | 0x1C6 | **00007FFF7A811820** | **0x9C1820** | 虚（接口实现），带击退向量 |
| **`ReceiveFatalDamage`** | **2** (GameObject, DamageSource) | 0x86 | **00007FFF7A811F80** | **0x9C1F80** | **直接处死，绕过 HP，必须一起 hook** |
| `ApplyDelayedDamage` | 7 | 0x86 | 00007FFF7A810620 | 0x9C0620 | 延迟伤害（火/骑士持续） |
| `DelayDamageApplication` | 6 | 0x81 | 00007FFF7A810BE0 | 0x9C0BE0 | 协程 |
| `EndDelayedDamage` | 0 | 0x86 | 00007FFF7A810E70 | 0x9C0E70 | |
| `EndContinualDamage` | 1 (GameObject) | 0x86 | 00007FFF7A810DF0 | 0x9C0DF0 | |
| `DestroyNow` | 0 | 0x86 | 00007FFF7A810D60 | 0x9C0D60 | **立即销毁** |
| `HitFlash` | 0 | 0x86 | 00007FFF7A811130 | 0x9C1130 | |
| `SendHitFlash` / `RecvHitFlash` | 0 | 0x81 | 00007FFF7A8122D0 / 00007FFF7A812040 | 0x9C22D0 / 0x9C2040 | |
| `SendHitSound` / `RecvHitSound` | 2 / 0 | 0x81 | 00007FFF7A812430 / 00007FFF7A812050 | 0x9C2430 / 0x9C2050 | |
| `SendHP` / `RecvHP` | 0 | 0x81 | 00007FFF7A812200 / 00007FFF7A811FB0 | 0x9C2200 / 0x9C1FB0 | |
| `SendInvulnerableState` / `RecvInvulnerableState` | 0 | 0x81 | 00007FFF7A812520 / 00007FFF7A8121B0 | 0x9C2520 / 0x9C21B0 | |
| `GetActualHp` | 0 | 0x86 | 00007FFF7A810FC0 | 0x9C0FC0 | |
| `get_hitPoints` / `set_hitPoints` | 0 / 1 | 0x886 | 00007FFF7A57A500 / 00007FFF7A812F00 | 0x72A500 / 0x9C2F00 | |
| `get_invulnerable` / `set_invulnerable` | 0 / 1 | 0x886 | 00007FFF7A812B20 / 00007FFF7A812FD0 | 0x9C2B20 / 0x9C2FD0 | |
| `get_isDead` / `set_isDead` | 0 / 1 | 0x886 / 0x881 | 00007FFF7A812B30 / 00007FFF7A8130A0 | 0x9C2B30 / 0x9C30A0 | |
| `get_HealthPercentage` | 0 | 0x886 | 00007FFF7A812B00 | 0x9C2B00 | |
| `get_DamagePercentage` | 0 | 0x886 | 00007FFF7A812AC0 | 0x9C2AC0 | |
| `IsDamagedBy` | 1 (DamageSource) | 0x86 | 00007FFF7A811370 | 0x9C1370 | |
| `EnableDamagedBy` / `DisableDamagedBy` | 1 | 0x86 | 00007FFF7A810DE0 / 00007FFF7A810DD0 | 0x9C0DE0 / 0x9C0DD0 | |
| `ShouldPlayDeathSound` | 0 | 0x81 | 00007FFF7A8125E0 | 0x9C25E0 | |
| `FadeAndDestroy` | 0 | 0x81 | 00007FFF7A810E90 | 0x9C0E90 | 协程 |
| `FadeRecvDeath` | 0 | 0x86 | 00007FFF7A810F50 | 0x9C0F50 | |
| `FadeNoDespawn` | 0 | 0x81 | 00007FFF7A810EF0 | 0x9C0EF0 | |
| `Awake` / `OnEnable` / `OnDisable` / `Reset` | 0 | 0x86/0x81 | 00007FFF7A8107B0 / 00007FFF7A811530 / 00007FFF7A8114F0 / 00007FFF7A8114F0 | 0x9C07B0 / 0x9C1530 / 0x9C14F0 | |
| `Persistent.IDecayable.ApplyDecay` | 1 | 0x1E1 | 00007FFF7A8116E0 | 0x9C16E0 | |

> ⚠️ **`Damageable` 里不存在 `TakeDamage`。** 你需求里写的名字要改成 `ReceiveDamage`。

**`DamageSource` 枚举**（`damagedBy` 用它做位标志）：
`Troll, Arrow, BoulderFriendly, BoulderEnemy, Knight, Ogre, Bolt, PlayerSteed, Pike, Fire, Stealer, Boar, Trap, Crusher, Fleet, GreedProjectile, SerpentAttack`

---

## 6. 阵营判定（**最关键的问题**）

### 6.1 结论：`IUnitControllable.PlayerId` 是权威阵营字段

```
[CLASS] IUnitControllable  :  ?   size=0x0 flags=0xA1
    M get_PlayerId              ret=System.Int32  argc=0 flags=0xDC6 p0=00007FFF7A128B70(GA)
    M set_PlayerId              ret=System.Void   argc=1 flags=0xDC6 p0=00007FFF7A121D00(GA)
    M GetCurrentController      ret=IUnitController argc=0 flags=0x5C6 p0=00007FFF7A123580(GA)
    M AssumeControl             ret=System.Void   argc=2 flags=0x5C6 p0=00007FFF7A1235B0(GA)
```

各单位通过 `<PlayerId>k__BackingField` 实现它（**偏移因类而异，必须按类读**）：

| 单位类 | `PlayerId` 偏移 | `get_PlayerId` p0 | `set_PlayerId` p0 |
|---|---|---|---|
| `Archer` | **+0x0028** | 00007FFF7A30B9B0 | 00007FFF7A30BC50 |
| `Worker` | **+0x0028** | 00007FFF7A30B9B0 | 00007FFF7A30BC50 |
| `Farmer` | **+0x0028** | 00007FFF7A30B9B0 | 00007FFF7A30BC50 |
| `Berserker` | **+0x0028** | 00007FFF7A30B9B0 | 00007FFF7A30BC50 |
| `Knight` | **+0x0020** | 00007FFF7A300680 | 00007FFF7A300750 |
| `Beggar` | **+0x0040** | 00007FFF7A38FAB0 | 00007FFF7A38FAE0 |
| `WarriorPeasant` | **+0x011C** | 00007FFF7A3318E0 | 00007FFF7A3318F0 |
| `Boar` | +0x0178 | 00007FFF7A52F770 | 00007FFF7A52F7A0 |
| `Enemy` 系（`Boss`/`CrownStealer`/`Crusher`/`Squid`） | +0x0088 | 00007FFF7A5C8D60 | 00007FFF7A5C8D70 |
| `Troll` | +0x0268 | 00007FFF7A610960 | 00007FFF7A610970 |

**注意两个反例**：
- **`Peasant`（未招募村民）没有 `PlayerId`**。它的字段表里根本没有 `<PlayerId>k__BackingField`。`Peasant` 是「无主」状态，直到 `Character.Promote` 后变成 Worker/Archer 才有归属。
- **`Ninja` / `Pikeman` 也没有 `PlayerId`**（它们继承 `Fisher`，只有 `Fisher.side` @ +0x0028，类型 `Side{Left,Right}`，是**左右半场**不是阵营）。

`Player.playerId` @ **+0x026C**（`F i off=0x026C System.Int32 playerId`）。`Kingdom.SetupPlayerIDs p0=00007FFF7A3FB320` / `UpdatePlayerIDs p0=00007FFF7A3FD4B0` 负责分配。

### 6.2 `Side` 枚举 ≠ 阵营

```
[CLASS] Side  :  System.Enum   size=0x14
    F S off=0x0000 Side  Left
    F S off=0x0000 Side  Right
```
只表示「篝火左边 / 右边」。

### 6.3 `Damageable._playerRef` 是不是阵营字段？（**推断，未证实**）

**事实**：`Damageable._playerRef`（+0x00F0，类型 `Player`）在 dump 中存在，但**没有 `get_/set_playerRef` 属性访问器**，全工程只有 3 处 `_playerRef` 字段：

```
Damageable   || F i off=0x00F0 Player  _playerRef
PositionSync || F i off=0x0080 Player  _playerRef
Wallet       || F i off=0x0078 Player  _playerRef
```

**推断依据**：
- `Wallet._playerRef` 挂在 Archer/Worker/Farmer/Peasant/Beggar/Knight/Berserker 身上，语义几乎必然是「这些金币属于哪个玩家」；
- `PositionSync._playerRef` 是「这个物体的位置以哪个玩家为权威」；
- `Damageable._playerRef` 与 `Damageable.syncHP`（+0xC8）、`_hpIndex`（+0xDC）相邻，符合同一套「按玩家做网络权威」的模式。

**但**：无法排除它只在**君主本人的 Damageable** 上被赋值（那样它就只能识别「玩家本体」，识别不了子民）。

**⚠️ 因此：不要把它当唯一判据。** 必须运行时验证：

```
对 3 个已知对象分别读 *(void**)(dmg + 0xF0)：
  (a) 自己的 Archer 的 Damageable   -> 期望非 0
  (b) 君主 Player.damageable(+0x100) 的 Damageable -> 期望非 0
  (c) 一只 Greed 的 Damageable      -> 期望 0
若 (a) 为 0，则 _playerRef 不能用来判子民阵营，改用 §6.4 的方案。
```

### 6.4 推荐的阵营判定方案（不依赖 `_playerRef`）

**方案 A（首选，最稳）—— 用 `Kingdom` 的注册表建「友方集合」**

`Kingdom` 把每个友方单位都注册进 `Character` 组件集合：

```
[CLASS] Kingdom  :  Manager   size=0x3A8 F=129 M=296
    F i off=0x0080 Player playerOne
    F i off=0x0088 Player playerTwo
    F i off=0x00D0 Player[] _activePlayers
    F i off=0x01F8 HashSet<Character> _characters      <-- 所有村民类单位
    F i off=0x0200 HashSet<Archer>    _archers
    F i off=0x0208 HashSet<Worker>    _workers
    F i off=0x0210 HashSet<Knight>    _knights
    F i off=0x0220 HashSet<Farmer>    _farmers
    F i off=0x0228 HashSet<Beggar>    _beggars
    F i off=0x02B8 HashSet<BeggarCamp> <BeggarCamps>k__BackingField
    F i off=0x02C0 HashSet<Pikeman>   _pikemen
    F i off=0x02C8 List<Berserker>    _berserkers
    M AddCharacter(Character)  p0=00007FFF7A3EC690  RVA 0x59C690
    M RemoveCharacter(Character) p0=00007FFF7A3F8FD0
    M get_characters  ret=IEnumerable<Character> p0=00007FFF7A3FF0B0  RVA 0x5AF0B0
    M get_BeggarCamps ret=HashSet<BeggarCamp>    p0=00007FFF7A3FE5E0  RVA 0x5AE5E0
    M GetPlayer(int)  p0=00007FFF7A3F3110  RVA 0x5A3110
```

每个村民类单位都持有 `Character` 组件引用，而 `Character` 持有 `Damageable`：

```
[CLASS] Character  :  UnityEngine.MonoBehaviour   size=0x138
    F i off=0x0098 Wallet      <wallet>k__BackingField
    F i off=0x00C8 Damageable  _damageable      <-- 统一入口
    F i off=0x00D0 Mover       _mover
    F i off=0x00D8 Rigidbody2D _rigidbody
    F i off=0x00E8 Single      _inspiredUntil
    M GetCharacterExtraHp()  p0=00007FFF7A7D38D0  RVA 0x9838D0   <-- Character 给 Damageable 加额外 HP
    M HandleOnReceiveDamage(int,GameObject,DamageSource) p0=00007FFF7A7D3BE0
    M OnPreDamageInspired(int&,GameObject,DamageSource)  p0=00007FFF7A984BB0  <-- 注册在 Damageable.OnPreReceiveDamage
```

**持有 `Character` 组件的类（= 玩家阵营单位全集）**：

| 类 | `Character` 字段偏移 |
|---|---|
| `Peasant` | +0x0058 `_character` |
| `Worker` | +0x0080 `_character` |
| `Archer` | +0x0160 `_character` |
| `Knight` | +0x00E8 `_character` |
| `Farmer` | +0x0090 `_character` |
| `Beggar` | +0x0068 `_character` |
| `Berserker` | +0x0110 `character` |
| `Ninja` | +0x0108 `_character` |
| `Pikeman` | +0x00D0 `_character` |
| `NpcShieldUser` | +0x0028 `character` |
| `WarriorGhost` / `WarriorGhostLeader` | +0x00F0 / +0x00A8 |

**方案 B（最省事，无内部结构依赖）—— `UnityEngine.Object.FindObjectsOfType`**

```
[CLASS] UnityEngine.Object
    M FindObjectsOfType   ret=UnityEngine.Object[] argc=1 flags=0x96 p0=00007FFF7CADCE00  RVA=0x2C8CE00  args=(System.Type)
    M FindObjectsOfType   ret=UnityEngine.Object[] argc=2 flags=0x96 p0=00007FFF7CADCE70  args=(System.Type,System.Boolean)
    M FindObjectsByType   ret=UnityEngine.Object[] argc=3 flags=0x96 p0=00007FFF7CADCDB0  RVA=0x2C8CDB0 args=(Type,FindObjectsInactive,FindObjectsSortMode)
```

调用方式：`Il2CppObject* t = il2cpp_type_get_object(il2cpp_class_get_type(Character_klass));` → 直接 `((fn)addr)(t, nullptr)`（末参数是 MethodInfo*，可传 nullptr，IL2CPP 仅在需要泛型/静态构造时使用）。

**方案 C（最终判据，兜底）**——**白名单式**：把 `Kingdom._characters` 里所有 `Character._damageable` 收集成一个**排序数组**，hook 里二分查找。这样既不依赖 `_playerRef` 语义，也不依赖 `PlayerId` 偏移差异。这是本报告推荐的**生产实现**。

**不推荐**：靠 `DamageSource` 判敌我。`DamageSource` 是「伤害来源类型」，不是「受击者阵营」；`BoulderFriendly/BoulderEnemy` 这种命名说明敌我区分在**伤害发起方**，而你要保护的是**受击方**。

---

## 7. 单位类体系（继承树）

### 7.1 完整继承树

> **重要**：KTC 2.4.2 **没有** `UnitBase` / `Subject` / `Soldier` / `Spearman` / `Swordsman` / `Squire` / `Vagrant` 这些类。`Unit`、`UnitType`、`UnitTypes` 只是枚举。已用 `index.txt` 全量确认（`Select-String '^(Squire\w*|Soldier\w*|Spearman\w*|Swordsman\w*|Vagrant\w*)\s'` 返回空）。

```
UnityEngine.MonoBehaviour
├── Character        size=0x138 (312) F=41 M=66    [通用组件：所有村民类单位都挂它]
│      (无子类；靠字段被引用)
├── Peasant          size=0x0C8 F=35 M=45    [闲人/流浪村民，无 PlayerId]
│     └── WarriorPeasant   size=0x138 F=33 M=40  [PlayerId@+0x011C]
├── Actor`1          size=0x000 F=0  M=4     [抽象泛型基类，无实例字段]
│     ├── Worker     size=0x190 F=87 M=104   [建造者/工人] PlayerId@+0x0028
│     ├── Farmer     size=0x168 F=77 M=83    [农夫]        PlayerId@+0x0028
│     └── Fisher     size=0x068 F=13 M=13    [渔夫基类，只有 side]
│           ├── Pikeman  size=0x198 F=72 M=86  [长矛兵]
│           └── Ninja    size=0x1B8 F=75 M=45  [忍者]
├── Archer           size=0x2B0 F=142 M=156  [弓箭手]      PlayerId@+0x0028
├── Knight           size=0x1D8 F=89  M=145  [骑士]        PlayerId@+0x0020
├── Berserker        size=0x178 F=63  M=66   [狂战士]      PlayerId@+0x0028
├── Beggar           size=0x0D0 F=39  M=53   [流浪者]      PlayerId@+0x0040
├── StableKeeper     (马厩管理员)
├── Enemy            size=0x080 (128) F=20 M=54   [敌人抽象基类]
│     ├── Boss          size=0x270 F=85 M=70
│     ├── CrownStealer  size=0x1D8 F=77 M=59
│     ├── Crusher       size=0x158 F=58 M=51
│     ├── GreedArcher   size=0x138 F=31 M=46
│     ├── GreedKnight   size=0x198 F=48 M=48
│     ├── Squid         size=0x1A8 F=45 M=47
│     └── Troll         size=0x270 F=98 M=79
├── GreedGolem / Catapult / Ballista / Wall / ... (Workable 系，建筑/器械)
└── ...
```

### 7.2 **哪一层持有 `Damageable` 组件引用**

**没有任何公共基类持有它。** `Damageable` 是**每个单位类各自声明的序列化字段**，偏移各不相同：

| 类 | `Damageable` 字段名 | 偏移 |
|---|---|---|
| **`Character`（通用组件，推荐）** | `_damageable` | **+0x00C8** |
| `Peasant` | `_damageable` | +0x0070 |
| `Worker` | `_damageable` | +0x0088 |
| `Archer` | `_damageable` | +0x0168 |
| `Knight` | `_damageable` | +0x0108 |
| `Farmer` | `_damageable` | +0x00C0 |
| `Beggar` | (无直接字段，走 `_character`) | — |
| `Berserker` | `damageable` | +0x00F8 |
| `Ninja` | `_damageable` | +0x0110 |
| `Pikeman` | `_damageable` | +0x00D8 |
| `Enemy` | `damageable` | +0x0040 |
| `Player` | `<damageable>k__BackingField` | +0x0100 |
| `Steed` | (无；走 Payable) | — |

> **关键设计决策**：因为 `Beggar` 没有自己的 `_damageable`，且各类偏移不同，**统一走 `Character._damageable`（+0xC8）是最干净的做法**。`Kingdom._characters` 里的每个 `Character` 都有 `_damageable`。

### 7.3 单位枚举入口汇总

| 入口 | 类型 | 偏移 | 说明 |
|---|---|---|---|
| `Kingdom._characters` | `HashSet<Character>` | +0x01F8 | **最全的友方单位集合** |
| `Kingdom._archers` | `HashSet<Archer>` | +0x0200 | |
| `Kingdom._workers` | `HashSet<Worker>` | +0x0208 | |
| `Kingdom._knights` | `HashSet<Knight>` | +0x0210 | |
| `Kingdom._farmers` | `HashSet<Farmer>` | +0x0220 | |
| `Kingdom._beggars` | `HashSet<Beggar>` | +0x0228 | |
| `Kingdom._pikemen` | `HashSet<Pikeman>` | +0x02C0 | |
| `Kingdom._berserkers` | `List<Berserker>` | +0x02C8 | |
| `Kingdom.<BeggarCamps>` | `HashSet<BeggarCamp>` | +0x02B8 | 刷人点 |
| `Kingdom.spawnedSteeds` | `List<Steed>` | +0x0160 | 所有坐骑 |
| `Kingdom._farmHouses` | `HashSet<Farmhouse>` | +0x0288 | |
| `Kingdom._towers` / `_walls` / `_portals` | HashSet | +0x0290 / +0x0270 / +0x0298 | |
| `Kingdom.GetPlayer(int)` | 方法 | p0=00007FFF7A3F3110 | |

> ⚠️ **HashSet 内部偏移未在 dump 中可用**：`System.Collections.Generic.HashSet`1` 是**开放泛型**，IL2CPP 对未膨胀泛型不给字段偏移（dump 里全是 `off=0x0000`）：
> ```
> [CLASS] System.Collections.Generic.HashSet`1  :  System.Object   size=0x0
>     F i off=0x0000 System.Int32[]                              _buckets
>     F i off=0x0000 System.Collections.Generic.HashSet.Slot<T>[] _slots
>     F i off=0x0000 System.Int32                                _count
>     F i off=0x0000 System.Int32                                _lastIndex
>     F i off=0x0000 System.Int32                                _freeList
>     F i off=0x0000 System.Collections.Generic.IEqualityComparer<T> _comparer
>     F i off=0x0000 System.Int32                                _version
>     F i off=0x0000 System.Runtime.Serialization.SerializationInfo _siInfo
> ```
> **正确做法**：在 DLL 里用 `il2cpp_class_get_parent(HashSetBeggarCamp_klass)` 之类拿到膨胀类，或直接 `il2cpp_class_get_field_from_name(klass, "_slots")` + `il2cpp_field_get_offset()` **运行时求偏移**，不要硬编码。（字段声明顺序按 Mono BCL 为 `_buckets,_slots,_count,_lastIndex,_freeList,_comparer,_version,_siInfo`，8/8/4/4/4/(pad)/8/4/(pad)/8 → 推测 0x10/0x18/0x20/0x24/0x28/0x30/0x38/0x40，但**必须验证**。）
>
> **更省事的替代**：用 `UnityEngine.Object.FindObjectsOfType(Character_klass)`（§6.4 方案 B），完全不碰集合内部。

---

## 8. 刷人点（野外刷新点）

### 8.1 结论：**`BeggarCamp`**

`index.txt` 全量搜索 `Vagrant|Wanderer|Settler|Immigrant` → **0 个类**。「流浪者」在代码里叫 **`Beggar`**，野外刷出他们的营地叫 **`BeggarCamp`**。

其他 Spawner 全是**敌人/动物**刷点，已逐一排除：

| 类 | 刷什么 | 关键字段 | 排除理由 |
|---|---|---|---|
| `CaveEnemySpawner` | 洞穴敌人 | `normalEnemyBlueprint`@0x20, `timeBetweenSpawns`@0x2C, `clusterSize`@0x4C, `spawningEnabled`@0x30 | 敌人 |
| `EnemyWaveSpawner` | 夜袭波次 | `_wave`@0x20, `_spawnCooldown`@0x34, `_spawnArea`@0x3C | 敌人 |
| `SingleUseEnemyWaveSpawner` | 一次性敌波 | — | 敌人 |
| `AreaGreedRockSpawner` | 火山落石 | `_rockPrefab`@0x20, `_numRocks`@0x38, `_cooldown`@0x3C, `_trollSpawnChance`@0x40 | 敌人 |
| `BoarSpawnGroup` | 野猪 | `boarPrefab`@0x38, `spawnBoarTime`@0x40, `playerRefs`@0x78 | 动物 |
| `MerchantSpawner` | 商人 | `_hasSpawned`@0x20 | 商人 |
| `CatSpawner` / `DogSpawn` / `HindSpawner` / `StagSpawnerHelper` | 猫/狗/鹿 | — | 动物 |
| `SimpleSpawner` | 通用敌波 | `spawnAmount`@0x20, `blueprint`@0x30 | 敌人 |
| **`BeggarCamp`** | **流浪者（Beggar）** | 见下 | ✅ **就是它** |

### 8.2 `BeggarCamp` 完整字段 / 方法

```
[CLASS] BeggarCamp  :  UnityEngine.MonoBehaviour   size=0x58 (88) flags=0x100001 [MonoBehaviour]
    F i off=0x0020 System.Int32                           maxBeggars          <-- 同时存在的流浪者上限
    F i off=0x0024 System.Single                          spawnInterval       <-- 刷出间隔（秒）【核心】
    F i off=0x0028 System.Single                          hungerInterval      <-- 饥饿/离开间隔
    F i off=0x0030 System.Collections.Generic.List<Beggar> _beggars           <-- 已刷出的流浪者
    F i off=0x0038 UnityEngine.GameObject                 spawnWhenDestroyed
    F i off=0x0040 CRPCHeader                             parentHeaderRef
    F i off=0x0048 System.Int32                           _despawnRPCIndex
    F i off=0x004C System.Int32                           _spawnCitizenHouseRPCIndex
    F i off=0x0050 Coatsink.Common.IHaglet                slowUpdateRoutine
```

| 方法 | argc | flags | p0 | RVA | 说明 |
|---|---|---|---|---|---|
| `get_GetGO` | 0 | 0x9E6 | 00007FFF7A2FDC00 | 0x4ADCC00 | |
| `Awake` | 0 | 0x81 | 00007FFF7A41A240 | 0x5CA240 | |
| `Start` | 0 | 0x81 | 00007FFF7A41B130 | 0x5CB130 | |
| **`SlowUpdate`** | 0 | 0x81 | **00007FFF7A41ABC0** | **0x5CABC0** | **主循环协程（计时 → SpawnBeggar）** |
| `SpawnCitizenHouse` | 0 | 0x81 | 00007FFF7A41AEC0 | 0x5CAEC0 | 升级成民房 |
| **`SpawnBeggar`** | 0 | 0x81 | **00007FFF7A41AC20** | **0x5CAC20** | **刷出一个流浪者** |
| `AddBeggar` | 1 (Beggar) | 0x86 | 00007FFF7A41A1A0 | 0x5CA1A0 | |
| `RemoveBeggar` | 1 (Beggar) | 0x86 | 00007FFF7A41AB00 | 0x5CAB00 | |
| `OnDestroy` | 0 | 0x81 | 00007FFF7A41A680 | 0x5CA680 | |
| `SendDespawn` / `RecvDespawn` | 0 | 0x81 | 00007FFF7A41AB60 / 00007FFF7A41A840 | 0x5CAB60 / 0x5CA840 | |
| `RecvSpawnCitizenHouse` | 0 | 0x81 | 00007FFF7A41A8A0 | 0x5CA8A0 | |
| `.ctor` | 0 | 0x1886 | 00007FFF7A41B220 | 0x5CB220 | |

**控制「刷出流浪者的时间间隔 / 冷却 / 上限」的字段就是这三个**：
- `spawnInterval` @ **+0x0024** → 写成 `0.1f` 就几乎瞬间刷
- `maxBeggars` @ **+0x0020** → 写成 `20` 提高同时在场上限
- `hungerInterval` @ **+0x0028** → 流浪者停留多久后离开

**获取所有 BeggarCamp**：
- `Kingdom.<BeggarCamps>k__BackingField` @ **+0x02B8**（`HashSet<BeggarCamp>`）
- `Kingdom.get_BeggarCamps` p0=00007FFF7A3FE5E0 (RVA 0x5AE5E0)
- `Kingdom.GetClosestBeggarCamp(float)` p0=00007FFF7A3F1750
- 或 `Object.FindObjectsOfType(BeggarCamp_klass)`（最稳）

### 8.3 `Beggar`（流浪者）完整字段

```
[CLASS] Beggar  :  UnityEngine.MonoBehaviour   size=0xD0 (208) F=39 M=53
    F S off=0x0000 Int32   APStunned / off=0x0004 APEat / off=0x0008 CurrencyMask / off=0x000C EnemyMask
    F i off=0x0020 Single  walkSpeed
    F i off=0x0024 Single  runSpeed
    F i off=0x0028 Single  coinSpotRange
    F i off=0x002C Single  timeToCamp          <-- 走回营地所需时间
    F i off=0x0030 Single  enemyScanInterval
    F i off=0x0034 Boolean settler             <-- 是否已定居
    F i off=0x0038 BeggarCamp camp             <-- 所属营地
    F i off=0x0040 Int32   <PlayerId>k__BackingField
    F i off=0x0044 Boolean <DespawnOnLoad>k__BackingField
    F i off=0x0048 CRPCHeader parentHeaderRef
    F i off=0x0058 Wallet _wallet ; off=0x0060 Wallet _originalWallet
    F i off=0x0068 Character _character
    F i off=0x0070 Animator _animator ; off=0x0078 SpriteRenderer _spriteRenderer
    F i off=0x0080 Mover _mover
    F i off=0x0088 Petrifiable _petrifiable
    F i off=0x0090 Scanner _coinScanner ; off=0x0098 Scanner _enemyScanner
    F i off=0x00A0 Baker _baker
    F i off=0x00A8 AnimationSync _animSync
    F i off=0x00B0 Single _idleStarted ; off=0x00B4 Boolean _isEating
    F i off=0x00B5 Boolean _controllableSprinting
    F i off=0x00B8 IUnitController _unitController
    F i off=0x00C0 IHaglet behaviour
    F i off=0x00C8 DroppableCurrency coin
    M get_PlayerId p0=00007FFF7A38FAB0 / set_PlayerId p0=00007FFF7A38FAE0
    M GetCurrentController p0=00007FFF7A38EC60 / AssumeControl p0=00007FFF7A41B2B0
```

`BeggarData`（存档用，不是运行时）：`settler`@0x10, `despawnOnLoad`@0x11, `baker`@0x18。

### 8.4 `CitizenHousePayable`（**城内**民房刷村民 —— 别和野外刷人点搞混）

```
[CLASS] CitizenHousePayable  :  Payable   size=0x178
    F S off=0x0000 WaitForSeconds                    ENEMY_CHECK_WAIT
    F i off=0x0140 System.Single                     _cooldownOfSpawning             <-- 刷人冷却
    F i off=0x0144 System.Single                     _enemyCloseDistance
    F i off=0x0148 System.Single                     _cooldownPenaltyPercent
    F i off=0x0150 UnityEngine.GameObject            _spawnUnit
    F i off=0x0158 List<UnityEngine.GameObject>      citizens
    F i off=0x0160 System.Single                     <_timePassedSinceLastSpawn>k__BackingField  <-- 已过时间
    F i off=0x0164 System.Int32                      <_numberOfAvailableCitizens>k__BackingField <-- 可刷数量
    F i off=0x0168 System.Int32                      _spawnPeasantRPCIndex
    F i off=0x016C System.Int32                      _despawnPeasantRPCIndex
    F i off=0x0170 System.Int32                      _enemiesLayer
    M SpawnPeasant          p0=00007FFF7A7DB020  RVA 0x98B020   <-- 直接调用可立刻刷一个
    M DeSpawnPeasants       p0=00007FFF7A7DA8A0
    M RecvSpawnPeasant      p0=00007FFF7A7DB020   (与 SpawnPeasant 同一地址)
    M RecvDeSpawnPeasant    p0=00007FFF7A7DB010
    M get/set__timePassedSinceLastSpawn  p0=00007FFF7A7DB360 / 00007FFF7A7DB380
    M get/set__numberOfAvailableCitizens p0=00007FFF7A7DB350 / 00007FFF7A7DB370
    M CanPay(Player)        p0=00007FFF7A7DA810
```

> 如果用户想要的是「**城堡/民房里更快出村民**」，改 `CitizenHousePayable._cooldownOfSpawning`（+0x140）。若要「**野外流浪者营地更快出流浪者**」，改 `BeggarCamp.spawnInterval`（+0x24）。两者是不同机制。

---

## 9. Hook 点选型与风险

### 9.1 无敌功能的 hook 点（按优先级）

| 优先级 | 目标 | p0 | RVA | 理由 |
|---|---|---|---|---|
| 1 | `Damageable.ReceiveDamage` (argc=3) | 00007FFF7A811780 | 0x9C1780 | 非虚（flags=0x86），主入口，`this`=rcx / damage=edx / source=r8 / DamageSource=r9d |
| 2 | `Damageable.ReceiveFatalDamage` (argc=2) | 00007FFF7A811F80 | 0x9C1F80 | **绕过 HP 的直接处死路径，必须一起 hook** |
| 3 | `Damageable.ReceiveDamage` (argc=4) | 00007FFF7A811820 | 0x9C1820 | 虚（flags=0x1C6），带击退向量；接口调用可能走这里 |
| 4 | `Damageable.ApplyDelayedDamage` (argc=7) | 00007FFF7A810620 | 0x9C0620 | 火焰/骑士持续伤害 |
| 5 | `Damageable.DestroyNow` | 00007FFF7A810D60 | 0x9C0D60 | 硬销毁 |

**hook 注意事项（逐条）**：

1. **必须同时 hook `ReceiveDamage`(3) + `ReceiveFatalDamage`(2)**。只挡 `ReceiveDamage` 会漏掉直接处死路径（例如君主被夺冠、`CrownStealer` 抓人）。
2. **注意虚方法分派**：argc=4 版本 `flags=0x1C6` 含 `0x40`(Virtual)，若被接口调用（`IDamageable` 之类）会走 vtable。建议把 3 和 4 都 hook，或只在 3 上 hook 并确认 4 会转调 3（需反汇编确认）。
3. **`this` 可能为空/已销毁**：Unity 对象销毁后指针仍非 null 但 `klass` 可能已失效。校验：
   ```
   if (!d) return orig(...);
   void* k = *(void**)d;                       // klass
   if (!InGameAssembly(k)) return orig(...);   // 越界 -> 不碰
   ```
4. **不要在 hook 里调用托管代码**（`il2cpp_runtime_invoke`）——会有 GC/线程附加风险，且可能死锁。hook 内只做「查表 + 直接写内存 + return」。
5. **线程安全**：Unity 主线程占绝大多数，但 `RecvHP`/`RecvInvulnerableState` 可能来自网络线程。友方集合用**双缓冲**（写指针原子交换）或读写锁，或干脆只做无锁的排序数组 + 二分查找。
6. **只读不写更安全**：如果只是「无敌」，直接 `return;`（跳过原函数）比修改 `_hitPoints` 更干净——不会触发 `OnReceiveDamage` 事件链、不闪红、不出伤害数字。
7. **返回值**：`ReceiveDamage` 返回 `System.Void`，hook 里 `return;` 即可，无需返回值处理。
8. **`flags=0x86` 的 `ReceiveDamage`(3) 是非虚**，可直接 inline hook 且不会有 vtable 旁路——这是最推荐的单点。
9. **备选（无 hook）方案**：后台线程每 100~250 ms 遍历友方集合，直接写 `_isInvulnerable=1`（+0x100）、`_hitPoints = initialHitPoints`（+0x104 ← +0x5C）。这个方案**零 hook 风险**，且对 `ReceiveFatalDamage` 也有效（因为 `_isInvulnerable` 是统一检查点）。**强烈建议先实现这个，再考虑加 hook 做即时响应。**

### 9.2 速度 / 耐力功能的 hook 点

| 目标 | p0 | RVA | 做法 |
|---|---|---|---|
| 无限耐力（兜底，最稳） | — | — | 每帧写 `*(float*)(steed+0x44C)=1.0f`、`*(float*)(steed+0x3F8)=0.0f` |
| 无限耐力（字段法） | — | — | 写 `Steed.runStaminaRate`(+0x180)=0、`walkStaminaRate`(+0x17C)=0、`glideStaminaRate`(+0x184)=0 |
| 无限耐力（开关法） | — | — | 写 `Player` 静态块 +0x00 = 1（见 §2.3） |
| 速度（字段法） | — | — | 写 `Steed.walkSpeed`(+0x170)、`runSpeed`(+0x174)，或 `Mover._moveSpeed`(+0x30)、`_multiplier`(+0x34) |
| 速度（调用法） | `Mover.SetSpeedMultiplier` 00007FFF7A4671D0 | 0x6171D0 | 调用一次，第二参数给超时（秒），比写字段更符合游戏逻辑 |
| 速度（hook 法） | `Mover.Update` 00007FFF7A467350 | 0x617350 | 每帧后把 `_moveSpeed`/`_multiplier` 改回你的值（防游戏覆盖） |
| 强制奔跑动画 | `Steed.ForceGallopAnimation` 00007FFF7A5EEDE0 | 0x79EDE0 | 视觉同步 |

> `Mover.Update`（RVA 0x617350）很适合做「每帧维持」hook：它在每个移动对象上都会跑。但注意它会被**所有**单位（村民/敌人）调用，必须按对象过滤，或改成只对 `Player.<mover>`（+0xF0）与 `Player._steed._mover`（+0x190 → +0x348）做维持。

### 9.3 刷人点

| 目标 | 做法 |
|---|---|
| 间隔 | 写 `BeggarCamp.spawnInterval`(+0x24) = `0.1f` |
| 上限 | 写 `BeggarCamp.maxBeggars`(+0x20) = `20` |
| 立刻刷一个 | 调用 `BeggarCamp.SpawnBeggar`（RVA 0x5CAC20），`this`=rcx |
| 维持 | 后台线程每 500 ms 遍历所有 `BeggarCamp` 重新写（防场景重载后重置） |

---

## 10. 实现方案（伪 C++）

### 10.1 引导：解析类与静态数据

```cpp
// ---- 0) 基础
i2c::Init();                                   // 已在 il2cpp_api.h 中
Il2CppDomain* dom = i2c_domain_get();
i2c_thread_attach(dom);
Il2CppImage* game = nullptr;
{ size_t n=0; auto as = i2c_domain_get_assemblies(dom,&n);
  for (size_t i=0;i<n;i++){ auto im=i2c_assembly_get_image(as[i]);
    if (!strcmp(i2c_image_get_name(im),"Assembly-CSharp.dll")) game=im; } }
Il2CppImage* core = /* UnityEngine.CoreModule.dll 同理 */;

// ---- 1) 拿 klass
Il2CppClass* kPlayer     = i2c_class_from_name(game, "", "Player");
Il2CppClass* kSteed      = i2c_class_from_name(game, "", "Steed");
Il2CppClass* kMover      = i2c_class_from_name(game, "", "Mover");
Il2CppClass* kDamageable = i2c_class_from_name(game, "", "Damageable");
Il2CppClass* kCharacter  = i2c_class_from_name(game, "", "Character");
Il2CppClass* kKingdom    = i2c_class_from_name(game, "", "Kingdom");
Il2CppClass* kBeggarCamp = i2c_class_from_name(game, "", "BeggarCamp");
Il2CppClass* kManagers   = i2c_class_from_name(game, "", "Managers");
Il2CppClass* kObj        = i2c_class_from_name(core, "UnityEngine", "Object");

// ---- 2) 静态字段块
uint8_t* playerStatics = (uint8_t*)i2c_class_get_static_field_data(kPlayer);
// Player.DebugInfiniteStamina @ +0x00, DebugReplaceSteed @ +0x01, APSpeed @ +0x04(勿写)

// ---- 3) Managers 单例 -> Kingdom
Il2CppClass* kSingletonBase = i2c_class_get_parent(kManagers);   // SingletonMonoBehaviour`1<Managers>
FieldInfo* fInst = i2c_class_get_field_from_name(kSingletonBase, "_Inst");
void* managers = nullptr;
i2c_field_static_get_value(fInst, &managers);                    // 注意：静态字段用 static_get_value
Kingdom* kingdom = managers ? *(Kingdom**)((uint8_t*)managers + 0x58) : nullptr;
```

### 10.2 功能 1：马匹快速奔跑 + 无限耐力

```cpp
// 一次性（或每 2 秒重刷，防场景切换）
void ApplyMount(Kingdom* kingdom, bool infStamina, float speedScale, bool useDebugFlag)
{
    // (a) 调试开关法（可选，先用 (b)(c) 兜底）
    if (useDebugFlag) *(uint8_t*)(playerStatics + 0x00) = 1;   // Player.DebugInfiniteStamina

    // (b) 遍历所有坐骑：Kingdom.spawnedSteeds (List<Steed>) @ +0x160
    void* list = *(void**)((uint8_t*)kingdom + 0x160);
    if (list) {
        void*  items = *(void**)((uint8_t*)list + 0x10);      // List<T>._items
        int32_t size = *(int32_t*)((uint8_t*)list + 0x18);    // List<T>._size
        for (int i = 0; i < size; ++i) {
            Steed* st = *(Steed**)((uint8_t*)items + 0x20 + 8*i);
            if (!st) continue;

            // 耐力
            if (infStamina) {
                *(float*)((uint8_t*)st + 0x44C) = 1.0f;        // <Stamina>
                *(float*)((uint8_t*)st + 0x3F8) = 0.0f;        // _tiredTimer
                *(float*)((uint8_t*)st + 0x17C) = 0.0f;        // walkStaminaRate
                *(float*)((uint8_t*)st + 0x180) = 0.0f;        // runStaminaRate
                *(float*)((uint8_t*)st + 0x184) = 0.0f;        // glideStaminaRate
                *(float*)((uint8_t*)st + 0x1C0) = 0.0f;        // tiredDuration
            }
            // 速度：只改"玩家正在骑的那匹"，避免野马乱窜
            Player* rider = *(Player**)((uint8_t*)st + 0x440); // <Rider>
            if (rider) {
                *(float*)((uint8_t*)st + 0x170) = 6.0f * speedScale;  // walkSpeed (原值 ~3)
                *(float*)((uint8_t*)st + 0x174) = 12.0f * speedScale; // runSpeed  (原值 ~5)
                // 同步 Mover，立即生效
                Mover* mv = *(Mover**)((uint8_t*)st + 0x348);
                if (mv) {
                    *(float*)((uint8_t*)mv + 0x30) = 12.0f * speedScale; // _moveSpeed
                    *(float*)((uint8_t*)mv + 0x34) = 1.0f;               // _multiplier
                }
                // 或者更"游戏内"的做法：调用 SetSpeedMultiplier(mult, timeoutSec)
                // typedef void (*fnSetSpeedMul)(Mover*, float, float, void*);
                // ((fnSetSpeedMul)GA(0x6171D0))(mv, speedScale, 0.2f, nullptr);
            }
        }
    }
    // (c) 玩家自己的 Mover：Player.<mover> @ +0xF0（走路时也生效）
    Player* p1 = kingdom ? *(Player**)((uint8_t*)kingdom + 0x80) : nullptr;
    if (p1) {
        Mover* mv = *(Mover**)((uint8_t*)p1 + 0xF0);
        if (mv) { *(float*)((uint8_t*)mv + 0x30) = 12.0f * speedScale;
                  *(float*)((uint8_t*)mv + 0x34) = 1.0f; }
    }
}
```

> **更稳的耐力方案**：hook `Steed.Update`（RVA 0x7A3630），在调用原函数**之后**写 `*(float*)(steed+0x44C)=1.0f`。这样游戏每帧扣完你立刻补满，不依赖任何字段语义推断。

### 10.3 功能 2：己方子民无敌

**Step 1 — 建立友方 Damageable 白名单（后台线程，每 250 ms）**

```cpp
static std::atomic<uintptr_t*> g_friendly;   // 排序去重的 Damageable* 数组
static std::atomic<size_t>    g_friendlyN;

void RebuildFriendlyList(Kingdom* kingdom)
{
    std::vector<uintptr_t> tmp;
    // 方案 A1：走 Kingdom._characters (HashSet<Character>) @ +0x1F8
    //           —— 但 HashSet 内部偏移需运行时求，见 §7.3 警告
    // 方案 A2（推荐，无内部依赖）：
    typedef Il2CppArray* (*fnFind)(void* type, void* methodInfo);
    void* typeChar = i2c_type_get_object(i2c_class_get_type(kCharacter));
    Il2CppArray* arr = ((fnFind)GA(0x2C8CE00))(typeChar, nullptr);   // Object.FindObjectsOfType

    size_t n = *(uintptr_t*)((uint8_t*)arr + 0x18);
    for (size_t i = 0; i < n; ++i) {
        Character* ch = *(Character**)((uint8_t*)arr + 0x20 + 8*i);
        if (!ch) continue;
        Damageable* d = *(Damageable**)((uint8_t*)ch + 0xC8);        // Character._damageable
        if (!d) continue;
        tmp.push_back((uintptr_t)d);
    }
    // 补漏：Player 本体（君主）也保护
    for (int i = 0; i < 2; ++i) {
        Player* p = *(Player**)((uint8_t*)kingdom + 0x80 + 8*i);
        if (p) { Damageable* d = *(Damageable**)((uint8_t*)p + 0x100);
                 if (d) tmp.push_back((uintptr_t)d); }
    }
    std::sort(tmp.begin(), tmp.end());
    tmp.erase(std::unique(tmp.begin(), tmp.end()), tmp.end());
    // 双缓冲原子交换...
}
```

**Step 2a — 无 hook 版（推荐先做）**

```cpp
void TickInvulnerable()
{
    auto* arr = g_friendly.load(); size_t n = g_friendlyN.load();
    for (size_t i = 0; i < n; ++i) {
        uint8_t* d = (uint8_t*)arr[i];
        if (!d) continue;
        *(uint8_t*)(d + 0x100) = 1;                                  // _isInvulnerable = true
        int32_t maxHp = *(int32_t*)(d + 0x5C);                       // initialHitPoints
        if (*(int32_t*)(d + 0x104) < maxHp) *(int32_t*)(d + 0x104) = maxHp;  // _hitPoints
        *(uint8_t*)(d + 0x109) = 0;                                  // <isDead> = false
    }
}
// 每 100ms 跑一次。对 ReceiveFatalDamage 也有效，因为 _isInvulnerable 是统一检查点。
```

**Step 2b — hook 版（即时拦截）**

```cpp
typedef void (*fnRecvDmg3)(Damageable*, int32_t, void* srcGO, int32_t dmgSource, void* mi);
typedef void (*fnRecvFatal)(Damageable*, void* srcGO, int32_t dmgSource, void* mi);
static fnRecvDmg3  origRecvDmg3  = nullptr;
static fnRecvFatal origRecvFatal = nullptr;

bool IsFriendly(Damageable* d) {
    auto* arr = g_friendly.load(); size_t n = g_friendlyN.load();
    return n && std::binary_search(arr, arr + n, (uintptr_t)d);
}

// ReceiveDamage(int amount, GameObject source, DamageSource ds)  RVA 0x9C1780
void Hook_ReceiveDamage(Damageable* self, int32_t amount, void* srcGO,
                        int32_t ds, void* mi)
{
    if (self && IsFriendly(self)) {
        *(uint8_t*)((uint8_t*)self + 0x100) = 1;   // 顺手打上无敌，防止别的路径
        return;                                    // 直接吞掉：不扣血、不闪红、不出伤害
    }
    origRecvDmg3(self, amount, srcGO, ds, mi);
}

// ReceiveFatalDamage(GameObject source, DamageSource ds)  RVA 0x9C1F80
void Hook_ReceiveFatalDamage(Damageable* self, void* srcGO, int32_t ds, void* mi)
{
    if (self && IsFriendly(self)) { *(uint8_t*)((uint8_t*)self + 0x100) = 1; return; }
    origRecvFatal(self, srcGO, ds, mi);
}

// 安装（MinHook / SafetyHook 风格）
MH_CreateHook(GA(0x9C1780), &Hook_ReceiveDamage,      (void**)&origRecvDmg3);
MH_CreateHook(GA(0x9C1F80), &Hook_ReceiveFatalDamage, (void**)&origRecvFatal);
```

> 如果担心漏掉，再补 hook `ReceiveDamage`(4) @ RVA 0x9C1820 与 `ApplyDelayedDamage` @ RVA 0x9C0620。

### 10.4 功能 3：野外刷新点快速刷人

```cpp
void BoostBeggarCamps()
{
    typedef Il2CppArray* (*fnFind)(void* type, void* mi);
    void* t = i2c_type_get_object(i2c_class_get_type(kBeggarCamp));
    Il2CppArray* arr = ((fnFind)GA(0x2C8CE00))(t, nullptr);
    size_t n = *(uintptr_t*)((uint8_t*)arr + 0x18);
    for (size_t i = 0; i < n; ++i) {
        uint8_t* camp = *(uint8_t**)((uint8_t*)arr + 0x20 + 8*i);
        if (!camp) continue;
        *(int32_t*)(camp + 0x20) = 20;      // maxBeggars
        *(float*  )(camp + 0x24) = 0.1f;    // spawnInterval  <-- 核心
        *(float*  )(camp + 0x28) = 9999.f;  // hungerInterval（几乎不离开）
    }
    // 想立刻刷：对每个 camp 调 SpawnBeggar(RVA 0x5CAC20)，this = rcx
    // typedef void (*fnSpawn)(void* self, void* mi);
    // ((fnSpawn)GA(0x5CAC20))(camp, nullptr);
}
// 每 500 ms 重跑一次（场景重载会重建 BeggarCamp）
```

若要「城内民房更快出村民」，另加：

```cpp
// CitizenHousePayable._cooldownOfSpawning @ +0x140
void BoostCitizenHouses() {
    void* t = i2c_type_get_object(i2c_class_get_type(
                 i2c_class_from_name(game, "", "CitizenHousePayable")));
    Il2CppArray* arr = ((fnFind)GA(0x2C8CE00))(t, nullptr);
    for (size_t i = 0; i < *(uintptr_t*)((uint8_t*)arr+0x18); ++i) {
        uint8_t* h = *(uint8_t**)((uint8_t*)arr + 0x20 + 8*i);
        if (h) { *(float*)(h + 0x140) = 0.2f;                       // 冷却
                 *(int32_t*)(h + 0x164) = 8; }                      // _numberOfAvailableCitizens
    }
}
```

### 10.5 每帧维持循环骨架

```cpp
DWORD WINAPI Worker(LPVOID) {
    i2c::Init(); i2c_thread_attach(i2c_domain_get());
    ResolveClasses();                       // §10.1
    InstallHooks();                         // §10.3 Step 2b（可选）
    uint32_t t = 0;
    for (;;) {
        if (g_enableInvuln && (t % 5 == 0))  { RebuildFriendlyList(GetKingdom()); TickInvulnerable(); }
        if (g_enableMount)                   { ApplyMount(GetKingdom(), g_infStamina, g_speedScale, g_useDebugFlag); }
        if (g_enableSpawn && (t % 10 == 0))  { BoostBeggarCamps(); BoostCitizenHouses(); }
        Sleep(20); ++t;
    }
}
```

> **线程注意**：`FindObjectsOfType` 必须在 Unity 主线程调用（内部访问场景图）。若你的后台线程调用它，请改为 **hook 一个主线程高频方法**（例如 `Player.Update` RVA 0x69E5A0 或 `Mover.Update` RVA 0x617350），在里面做「每 N 帧执行一次」的调度。

---

## 11. 地址速查表（RVA，基址 = `GetModuleHandleA("GameAssembly.dll")`）

| 符号 | p0（导出时绝对地址） | RVA |
|---|---|---|
| `Player.TryToGallop` | 00007FFF7A4EC440 | 0x69C440 |
| `Player.Ride` | 00007FFF7A4E8F40 | 0x698F40 |
| `Player.get_steed` | 00007FFF7A497B40 | 0x647B40 |
| `Player.get_mover` | 00007FFF7A4EF260 | 0x69F260 |
| `Player.get_damageable` | 00007FFF7A345BB0 | 0x4F5BB0 |
| `Player.get_currentSpeed` | 00007FFF7A4EF460 | 0x69F460 |
| `Player.get_isRunning` | 00007FFF7A4EF670 | 0x69F670 |
| `Player.get_IsTired` | 00007FFF7A4EF3E0 | 0x69F3E0 |
| `Player.SendStaminaState` | 00007FFF7A4E9EA0 | 0x699EA0 |
| `Player.ReceiveStaminaState` | 00007FFF7A4E8530 | 0x698530 |
| `Player.Update` | 00007FFF7A4EE5A0 | 0x69E5A0 |
| `Player..cctor` | 00007FFF7A4EEEB0 | 0x69EEB0 |
| `Steed.get_Stamina` | 00007FFF7A5F3C10 | 0x7A3C10 |
| `Steed.set_Stamina` | 00007FFF7A5F4050 | 0x7A4050 |
| `Steed.BecomeTired` | 00007FFF7A5EE420 | 0x79E420 |
| `Steed.Eat` | 00007FFF7A5EE920 | 0x79E920 |
| `Steed.Update` | 00007FFF7A5F3630 | **0x7A3630** |
| `Steed.ForceGallopAnimation` | 00007FFF7A5EEDE0 | 0x79EDE0 |
| `Steed.SetMode` | 00007FFF7A5F20A0 | 0x7A20A0 |
| `Steed.SetGroundType` | 00007FFF7A5F1FC0 | 0x7A1FC0 |
| `Steed.SetImmobile` | 00007FFF7A5F1FF0 | 0x7A1FF0 |
| `Steed.SetStand` | 00007FFF7A5F3100 | 0x7A3100 |
| `Steed..cctor` | 00007FFF7A5F36D0 | 0x7A36D0 |
| `Mover.get_Speed` | 00007FFF7A467C70 | 0x617C70 |
| `Mover.get_ActualSpeed` | 00007FFF7A467C60 | 0x617C60 |
| `Mover.get_Multiplier` | 00007FFF7A31DBC0 | 0x4CDBC0 |
| `Mover.SetSpeed(float)` | 00007FFF7A467280 | 0x617280 |
| `Mover.SetSpeed(float,int)` | 00007FFF7A4671F0 | 0x6171F0 |
| `Mover.ForceSetSpeed` | 00007FFF7A466710 | 0x616710 |
| `Mover.SetSpeedMultiplier` | 00007FFF7A4671D0 | **0x6171D0** |
| `Mover.ResetSpeedMultiplier` | 00007FFF7A466CF0 | 0x616CF0 |
| `Mover.Update` | 00007FFF7A467350 | 0x617350 |
| **`Damageable.ReceiveDamage` (3)** | 00007FFF7A811780 | **0x9C1780** |
| `Damageable.ReceiveDamage` (4) | 00007FFF7A811820 | 0x9C1820 |
| **`Damageable.ReceiveFatalDamage` (2)** | 00007FFF7A811F80 | **0x9C1F80** |
| `Damageable.ApplyDelayedDamage` (7) | 00007FFF7A810620 | 0x9C0620 |
| `Damageable.DelayDamageApplication` (6) | 00007FFF7A810BE0 | 0x9C0BE0 |
| `Damageable.DestroyNow` | 00007FFF7A810D60 | 0x9C0D60 |
| `Damageable.get_invulnerable` | 00007FFF7A812B20 | 0x9C2B20 |
| `Damageable.set_invulnerable` | 00007FFF7A812FD0 | 0x9C2FD0 |
| `Damageable.get_hitPoints` | 00007FFF7A57A500 | 0x72A500 |
| `Damageable.set_hitPoints` | 00007FFF7A812F00 | 0x9C2F00 |
| `Damageable.IsDamagedBy` | 00007FFF7A811370 | 0x9C1370 |
| `Damageable.GetActualHp` | 00007FFF7A810FC0 | 0x9C0FC0 |
| `Damageable.SendHP` | 00007FFF7A812200 | 0x9C2200 |
| `Damageable.RecvHP` | 00007FFF7A811FB0 | 0x9C1FB0 |
| `Damageable.HitFlash` | 00007FFF7A811130 | 0x9C1130 |
| `Damageable.FadeAndDestroy` | 00007FFF7A810E90 | 0x9C0E90 |
| `Damageable.FadeRecvDeath` | 00007FFF7A810F50 | 0x9C0F50 |
| `Damageable.EndContinualDamage` | 00007FFF7A810DF0 | 0x9C0DF0 |
| `Damageable.EndDelayedDamage` | 00007FFF7A810E70 | 0x9C0E70 |
| `Damageable.get_isDead` | 00007FFF7A812B30 | 0x9C2B30 |
| `Damageable.set_isDead` | 00007FFF7A8130A0 | 0x9C30A0 |
| `Damageable.SendInvulnerableState` | 00007FFF7A812520 | 0x9C2520 |
| `BeggarCamp.SpawnBeggar` | 00007FFF7A41AC20 | **0x5CAC20** |
| `BeggarCamp.SlowUpdate` | 00007FFF7A41ABC0 | 0x5CABC0 |
| `BeggarCamp.Awake` | 00007FFF7A41A240 | 0x5CA240 |
| `BeggarCamp.Start` | 00007FFF7A41B130 | 0x5CB130 |
| `BeggarCamp.SpawnCitizenHouse` | 00007FFF7A41AEC0 | 0x5CAEC0 |
| `BeggarCamp.AddBeggar` | 00007FFF7A41A1A0 | 0x5CA1A0 |
| `CitizenHousePayable.SpawnPeasant` | 00007FFF7A7DB020 | 0x98B020 |
| `Character.Promote(DroppableTool,IUnitController)` | 00007FFF7A7D5680 | 0x985680 |
| `Character.OnPreDamageInspired` | 00007FFF7A7D4BB0 | 0x984BB0 |
| `Character.HandleOnReceiveDamage` | 00007FFF7A7D3BE0 | 0x983BE0 |
| `Character.GetCharacterExtraHp` | 00007FFF7A7D38D0 | 0x9838D0 |
| `Kingdom.AddCharacter` | 00007FFF7A3EC690 | 0x59C690 |
| `Kingdom.get_characters` | 00007FFF7A3FF0B0 | 0x5AF0B0 |
| `Kingdom.get_BeggarCamps` | 00007FFF7A3FE5E0 | 0x5AE5E0 |
| `Kingdom.GetPlayer` | 00007FFF7A3F3110 | 0x5A3110 |
| `Kingdom.SpawnStartingBeggars` | 00007FFF7A3FB680 | 0x5AB680 |
| `Kingdom.SpawnStartingPeasants` | 00007FFF7A3FBC60 | 0x5ABC60 |
| `Kingdom.GetClosestBeggarCamp` | 00007FFF7A3F1750 | 0x5A1750 |
| `Kingdom.AddBeggarCamp` | 00007FFF7A3EC280 | 0x59C280 |
| `DebugTools.OnButtonInfiniteStamina` | 00007FFF7A8236A0 | 0x9D36A0 |
| `DebugTools.OnButtonInvulnerableRulers` | 00007FFF7A823700 | 0x9D3700 |
| `DebugTools.GetPlayersInvunerable` | 00007FFF7A81E820 | 0x9CE820 |
| `UnityEngine.Object.FindObjectsOfType(Type)` | 00007FFF7CADCE00 | 0x2C8CE00 |
| `UnityEngine.Object.FindObjectsByType(Type,int,int)` | 00007FFF7CADCDB0 | 0x2C8CDB0 |
| `UnityEngine.Animator.StringToHash` | 00007FFF7CA843D0 | 0x2C343D0 |

**类 klass 指针 / 实例大小（摘自 `index.txt`；klass 仅该次运行有效，仅供对照；运行时请用 `il2cpp_class_from_name` 重新取）**：

| 类 | 父类 | size | F | M | klass |
|---|---|---|---|---|---|
| `Player` | UnityEngine.MonoBehaviour | 656 (0x290) | 100 | 160 | 000001CFB17C1D80 |
| `Steed` | Payable | 1168 (0x490) | 133 | 87 | 000001CFB1B73FF0 |
| `Mover` | UnityEngine.MonoBehaviour | 144 (0x90) | 25 | 46 | 000001CFB1852490 |
| `Damageable` | UnityEngine.MonoBehaviour | 272 (0x110) | 41 | 65 | 000001CFB1A309B0 |
| `Character` | UnityEngine.MonoBehaviour | 312 (0x138) | 41 | 66 | 000001CFB1B3CFE0 |
| `Kingdom` | Manager | 936 (0x3A8) | 129 | 296 | 000001CFB1850B80 |
| `Managers` | Coatsink.Common.SingletonMonoBehaviour`1 | 304 (0x130) | 37 | 11 | 000001CFB19155A0 |
| `BeggarCamp` | UnityEngine.MonoBehaviour | 88 (0x58) | 9 | 19 | 000001CFB18A5950 |
| `Beggar` | UnityEngine.MonoBehaviour | 208 (0xD0) | 39 | 53 | 000001CFB19E1380 |
| `CitizenHousePayable` | Payable | 376 (0x178) | 11 | 24 | 000001CFB1A8A8A0 |
| `Rider` | UnityEngine.MonoBehaviour | 120 (0x78) | 14 | 12 | 000001CFB1B43200 |
| `SteedAbility` | UnityEngine.MonoBehaviour | 96 (0x60) | 17 | 18 | 000001CFB17C18E0 |
| `Archer` | UnityEngine.MonoBehaviour | 688 (0x2B0) | 142 | 156 | 000001CFB190D1C0 |
| `Worker` | Actor`1 | 400 (0x190) | 87 | 104 | 000001CFB1A2A9C0 |
| `Knight` | UnityEngine.MonoBehaviour | 472 (0x1D8) | 89 | 145 | 000001CFB17F0390 |
| `Farmer` | Actor`1 | 360 (0x168) | 77 | 83 | 000001CFB18915A0 |
| `Peasant` | UnityEngine.MonoBehaviour | 200 (0xC8) | 35 | 45 | 000001CFB185FAB0 |
| `Berserker` | UnityEngine.MonoBehaviour | 376 (0x178) | 63 | 66 | 000001CFB19779D0 |
| `Pikeman` | Fisher | 408 (0x198) | 72 | 86 | 000001CFB19D9A00 |
| `Ninja` | Fisher | 440 (0x1B8) | 75 | 45 | 000001CFB1B39910 |
| `Enemy` | UnityEngine.MonoBehaviour | 128 (0x80) | 20 | 54 | 000001CFB18933C0 |
| `DebugTools` | Coatsink.Common.SingletonMonoBehaviour`2 | 1344 (0x540) | 179 | 237 | 000001CFB1B4A180 |

---

## 12. 未证实项清单（编码时请自行运行时验证）

| 项 | 状态 | 验证方法 |
|---|---|---|
| `Player.DebugInfiniteStamina = true` 真的跳过耐力消耗 | **推断（高置信）** | 写 `static+0x00=1`，观察 `Steed+0x44C` 是否停止下降 |
| `DebugTools.OnButtonInfiniteStamina` 内部写的就是这个字段 | **推断（高置信）** | 在游戏里按调试按钮，前后读该字节 |
| `Damageable._playerRef != null` ⇒ 玩家阵营 | **推断（中置信）** | 对比 Archer / 君主 / Greed 三者的 `+0xF0` |
| `Damageable._isInvulnerable` 在 `ReceiveDamage` 开头被检查 | **推断（高置信）** | 写 1 后让敌人打，看是否掉血 |
| `HashSet<T>` 内部字段偏移 | **未知** | 运行时用 `il2cpp_class_get_field_from_name` + `il2cpp_field_get_offset` 求；或改用 `FindObjectsOfType` |
| `Steed.walkSpeed/runSpeed` 的原始量级 | **未知（示例值 3/5 是估计）** | 进游戏读一次真值，再按倍数改 |
| `ReceiveDamage`(4) 是否会转调 `ReceiveDamage`(3) | **未知** | 反汇编 RVA 0x9C1820 看是否 `jmp/call 0x9C1780` |

---

*报告生成：基于 `E:\AI\KTC-Trainer\dump\` 下 dump.txt（38,409,993 字节）、index.txt（21,220 行类索引）、images.txt、gameassembly_exports.txt。所有 `[CLASS]`/`F`/`M` 引用行均逐字摘自 dump.txt。*
