# Kingdom Two Crowns 2.4.2 — 建筑 / 建造 / 王冠 逆向分析报告

- **目标**：Unity 6000.0.66f2, IL2CPP, x64 Windows
- **数据来源**：`E:\AI\KTC-Trainer\dump\dump.txt`（38 MB）、`index.txt`
- **用途**：编写注入式原生 DLL（直接读写内存 + inline hook 原生函数指针）
- **分析方法**：只读解析 dump，未修改任何文件，未触碰游戏目录

> **地址说明**：dump 中 `p0=00007FFF7A7EF070(GA)` 里的 `(GA)` 表示该值是 **GameAssembly.dll 的 RVA + 基址**（运行时基址会因 ASLR 变化）。注入后应使用
> `真实地址 = GameAssembly.dll 模块基址 + (p0 - dump基址)`，或直接用
> `GetProcAddress` 风格的 `moduleBase + rva` 计算。dump 中 `p0=0000000000000000` 表示该槽位未被解析（泛型/接口未实例化）。

---

## 0. 关键前提：泛型基类 `Doable\`1` 的偏移未解析

这是本报告最重要的一条数据可靠性说明。

```
[CLASS] Workable  :  Doable`1   size=0x40 flags=0x100081 [MonoBehaviour]
    F i off=0x0038 System.Single                          _speed
```

`Workable` 自身只有一个字段 `_speed`（off=0x0038），但 `size=0x40`，说明 0x00–0x37 全部来自基类 `Doable\`1`。而 dump 里 `Doable\`1` 的定义是：

```
[CLASS] Doable`1  :  UnityEngine.MonoBehaviour   size=0x0 flags=0x100081 [MonoBehaviour]
    F i off=0x0000 AudioEmitter                           jobSoundOverride
    F i off=0x0000 System.Boolean                         jobIsOffGround
    F i off=0x0000 System.Boolean                         jobIsBelowGround
    F i off=0x0000 System.Collections.Generic.HashSet<T>  _currentActors
```

**`size=0x0` 且所有字段 `off=0x0000`** —— 该 dump 工具没有解析开放泛型 `Doable\`1<T>` 的实例字段偏移。

**结论 / 编码影响**：
- ✅ 所有**非泛型**具体类的偏移是可靠的（`Wall`、`ConstructionBuildingComponent`、`Damageable`、`Crown`、`Player`、`Enemy` 等），可以放心硬编码。
- ❌ **`Doable\`1` 的字段偏移未知**：`jobSoundOverride`、`jobIsOffGround`、`jobIsBelowGround`、`_currentActors` 这 4 个字段**没有可信偏移**。任何依赖 `_currentActors`（"当前有几个工人在敲"）的方案都**不能**用硬编码偏移实现，必须改用 IL2CPP API（`il2cpp_class_get_field_from_name`）或干脆绕开。
- ⚠️ `Workable` 的 `size=0x40` 可作为"基类 + 自身"总大小；但**不要**从 `Doable\`1` 反推单个字段偏移。

好消息是：**本报告的三个功能都不需要 `Doable\`1` 的字段**，全部依赖已解析的具体类偏移。

---

## 1. 建筑类体系（继承树）

### 1.1 继承树

```
UnityEngine.MonoBehaviour
├── Doable`1<T>                        size=0x0   ← 泛型，字段偏移未解析（见 §0）
│   └── Workable                       size=0x40  F=1  M=3
│       ├── Wall                       size=0xD0  F=24 M=35   ← 城墙，有 Damageable
│       ├── WorkableBuilding           size=0x58  F=4  M=16   ← 建筑建造载体
│       ├── WorkableTree               size=0xC8  F=24 M=26   ← 树（用 strength，非 HP）
│       ├── WorkableWharf              size=0x58  F=3  M=12
│       ├── WorkPosition               size=0x58  F=3  M=14
│       ├── FireTower                  size=0x150 F=41 M=40
│       ├── Catapult                   size=0x190 F=59 M=72   ← 有 Damageable
│       ├── Ballista                   size=0x110 F=31 M=28   ← 只有 _target
│       ├── OilFireArcherTower         size=0x118 F=39 M=53
│       ├── AssembledBuildProject.AssembledBuildProject  size=0xE8 F=28 M=36
│       ├── GreedGolem / EggMound / TrojanHorse / BoatPushPosition
│       ├── RollableOilBarrel
│       └── ...
├── Tower                              size=0x38  F=3  M=13   ← 不是 Workable！
├── ConstructionBuildingComponent      size=0x90  F=19 M=30   ← 建造进度核心
├── EraUpgradeBuilding                 size=0x28  F=1  M=2
├── Scaffolding                        size=0x38  F=4  M=7
├── ManualAttackObject                 size=0x40  F=4  M=6
│   ├── Barrier                        size=0x80  F=10 M=16   ← 有 Damageable
│   └── Trap                           size=0x80  F=12 M=15
├── Damageable                         size=0x110 F=41 M=65   ← 血量/无敌
├── Crown                              size=0xF0  F=31 M=48
└── ...

Payable (size=0x140)
├── PayableUpgrade                     size=0x190 F=16 M=23   ← 城墙升级
└── Statue / PayableShop / PayableForge / ...
```

### 1.2 重要纠正：`Wall` **不是** `WorkableBuilding` 的子类

任务描述里把 `Wall`、`WorkableBuilding` 并列列出，实际 dump 证据是**两者都直接继承 `Workable`，是兄弟关系**：

```
[CLASS] Wall  :  Workable   size=0xD0 flags=0x100001 [MonoBehaviour]        (dump.txt:40887)
[CLASS] WorkableBuilding  :  Workable   size=0x58 flags=0x100001 [MonoBehaviour]   (dump.txt:41830)
```

`Wall` 通过**字段引用**持有 `WorkableBuilding`：

```
    F i off=0x0098 WorkableBuilding                       _workableBuilding       (dump.txt:40907)
```

这是一个**委托/组合**关系，不是继承。编码时不要做 `is-a WorkableBuilding` 的判断，要走 `Wall._workableBuilding`（off=0x98）。

### 1.3 关键字段表（全部为已验证偏移）

#### `Workable`（`dump.txt:41819`，size=0x40）
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| 0x38 | `System.Single` | `_speed` | 工作速度 |
| 0x00–0x37 | — | （继承自 `Doable\`1`） | **偏移未知**，见 §0 |

#### `WorkableBuilding`（`dump.txt:41830`，size=0x58）
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| 0x40 | `System.Action` | `OnConstructionComplete` | 完工回调 |
| 0x48 | `System.Single` | `_workPositionOffset` | 工人站位偏移 |
| 0x4C | `AdjustZ` | `_adjustZ` | Z 排序 |
| **0x50** | **`ConstructionBuildingComponent`** | **`_constructionBuilding`** | **★ 建造进度组件引用** |

方法：
| 方法 | argc | p0 | 说明 |
|---|---|---|---|
| `get_UnderConstruction` | 0 | `00007FFF7A675D60` | 返回 bool，是否在建 |
| `CompleteConstruction` | 0 | `00007FFF7A675B10` | **★ 完成建造** |
| `ConstructionComplete` | 0 | `00007FFF7A675B30` | 完工处理 |
| `RegisterWork` | 0 | `00007FFF7A675E50` | 注册工作 |
| `OnJobFinish` | 1 | `00007FFF7A675E00` | 工人敲完一次 |
| `JobAvailable` | 0 | `00007FFF7A675D60` | 是否可接活 |

#### `Wall`（`dump.txt:40887`，size=0xD0）
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| 0x40 | `System.Int32` | `repairRate` | 修理速率 |
| 0x44 | `System.Int32` | `repairRateAboveMax` | 超上限修理 |
| 0x48 | `System.Single` | `bonusHitPointsMultiplier` | HP 加成 |
| 0x4C | `System.Single` | `minimumIntactHitPoints` | 最低完好 HP |
| 0x50 | `System.Int32` | `level` | 城墙等级 |
| 0x54 | `TechnologyAge` | `wallEraLevel` | 时代等级 |
| 0x58 | `DebrisGroup` | `debrisPrefab` | 残骸 |
| 0x60 | `UnityEngine.GameObject` | `wreckPrefab` | 废墟 |
| 0x68 | `UnityEngine.ParticleSystem` | `sparklesPrefab` | 粒子 |
| 0x70 | `UnityEngine.SpriteRenderer` | `_spriteRenderer` | 渲染 |
| **0x78** | **`Damageable`** | **`_damageable`** | **★ 城墙血量组件** |
| 0x80 | `DebrisGroup` | `_debrisGroup` | |
| 0x88 | `UnityEngine.ParticleSystem` | `_sparkles` | |
| 0x90 | `PayableUpgrade` | `_payableUpgrade` | 升级付费 |
| **0x98** | **`WorkableBuilding`** | **`_workableBuilding`** | **★ 建造载体** |
| 0xA0 | `System.Boolean` | `destructionHandled` | 已处理摧毁 |
| 0xA4 | `System.Int32` | `pikemanCount` | 长矛兵数 |
| 0xA8 | `System.Int32` | `archerCount` | 弓箭手数 |
| 0xAC | `System.Boolean` | `isHornUpgrade` | 号角升级 |
| 0xB0 | `CRPCHeader` | `parentHeaderRef` | 网络 RPC |
| 0xB8 | `System.Int32` | `_destroyedRpcIndex` | RPC 索引 |
| 0xC0 | `UnityEngine.Coroutine` | `invunerableWallsRoutine` | 无敌墙协程 |
| 0xC8 | `System.Boolean` | `fullyRepaired` | 已完全修复 |
| **S 0x00** | **`System.Boolean`** | **`<InvunerableWalls>k__BackingField`** | **★ 静态：城墙无敌总开关** |

> ⚠️ 注意拼写：游戏里是 **`Invunerable`**（少一个 `l`），不是 `Invulnerable`。写代码时别拼错。

方法：
| 方法 | argc | p0 | 说明 |
|---|---|---|---|
| `get_InvunerableWalls` | 0 | `00007FFF7A65FCB0` | 静态属性读 |
| `set_InvunerableWalls` | 1 | `00007FFF7A65FD50` | **★ 静态属性写（一改全图城墙无敌）** |
| `get_isIntact` | 0 | `00007FFF7A65FCF0` | 是否完好 |
| `UpdateInvunerable` | 0 | `00007FFF7A65FC10` | 协程 |
| `UpdateInvunerableState` | 0 | `00007FFF7A65FB60` | 刷新无敌状态 |
| `GetTotalMaxHitPoints` | 0 | `00007FFF7A65E600` | 总最大 HP |
| `HandleOnReceiveDamage` | 3 | `00007FFF7A65EAC0` | 受伤回调 |
| `HandleOnDeath` | 1 | `00007FFF7A65E7D0` | 死亡回调 |
| `DestroyWall` | 1 | `00007FFF7A65E0B0` | 摧毁 |
| `IDestructiblePlayerStructure.get_IsImmune` | 0 | `00007FFF7A2F3CC0` | **★ 免疫判定（返回常量 false）** |
| `IDestructiblePlayerStructure.DestroyPlayerStructure` | 0 | `00007FFF7A65EC90` | 被玩家结构摧毁 |

> 💡 `Wall.get_IsImmune` 的 p0 = `00007FFF7A2F3CC0`，与 `Damageable.get_IsInBlockingFormation`、`WorkableBuilding`… 多个方法**共用同一地址**——这是 IL2CPP 把"返回常量 false"的方法合并到同一个 stub 了。也就是说 `Wall.IsImmune` 目前**恒返回 false**，这是一个天然的 hook 点。

#### `Tower`（size=0x38）
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| 0x20 | `System.Int32` | `level` | 塔等级 |
| 0x28 | `CRPCHeader` | `parentHeaderRef` | 网络 |
| 0x30 | `System.Int32` | `_destroyedRpcIndex` | RPC |

方法：`DestroyTower`(argc=1, p0=`00007FFF7A608860`)、`IDestructiblePlayerStructure.get_IsImmune`(argc=0, p0=`00007FFF7A608D00`)、`SendDestroyed`/`RecvDestroyed`。
**`Tower` 没有 `Damageable` 字段**，也没有 HP 字段——它靠独立的 `IDestructiblePlayerStructure` 接口走摧毁流程。

#### `ConstructionBuildingComponent`（`dump.txt:7501`，size=0x90）★ 建造核心
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| **S 0x00** | **`System.Boolean`** | **`<AllAutoBuild>k__BackingField`** | **★ 静态：全局自动建造开关** |
| 0x20 | `System.Action` | `OnManualConstructionStarted` | 手动开工回调 |
| 0x28 | `System.Action` | `OnConstructionComplete` | 完工回调 |
| **0x30** | **`System.Int32`** | **`_buildPoints`** | **★ 建造所需总点数（目标值）** |
| 0x34 | `System.Single` | `_autoBuildRate` | 自动建造速率 |
| 0x38 | `AudioEmitter` | `_buildingStartedSound` | 音效 |
| 0x40 | `AudioEmitter` | `_buildingSound` | 敲击音效 |
| 0x48 | `AudioEmitter` | `_buildingCompleteSound` | 完工音效 |
| 0x50 | `AudioEmitter` | `_buildingCompleteAddSound` | |
| 0x58 | `Scaffolding` | `_scaffolding` | 脚手架 |
| 0x60 | `Stat` | `_statToIncrementOnBuilt` | 统计 |
| **0x64** | **`System.Single`** | **`_currentBuildPoints`** | **★ 当前已累计建造点数（进度）** |
| 0x68 | `System.Single` | `_buildSoundLastPlayed` | |
| 0x70 | `AudioEmitter` | `_buildSoundPlaying` | |
| **0x78** | **`System.Boolean`** | **`_hasStarted`** | **★ 是否已开工** |
| 0x80 | `CRPCHeader` | `_parentHeaderRef` | 网络 |
| 0x88 | `System.Int32` | `_progressRPCIndex` | RPC |
| 0x8C | `System.Int32` | `_completionRPCIndex` | RPC |
| S 0x00 | `System.Int32` | `RECV_INCREMENT_BUILD_RPC_BYTE_LEN` | 常量 |

方法：
| 方法 | argc | p0 | 说明 |
|---|---|---|---|
| `get_AllAutoBuild` | 0 | `00007FFF7A7F03A0` | 静态读 |
| `set_AllAutoBuild` | 1 | `00007FFF7A7F0530` | **静态写：开全自动建造** |
| `Awake` | 0 | `00007FFF7A7EE830` | |
| `AutoBuild` | 0 | `00007FFF7A7EE7D0` | 协程 |
| **`ConstructionComplete`** | **1** | **`00007FFF7A7EEA70`** | **★ 完工（bool 参数）** |
| **`ForceComplete`** | **0** | **`00007FFF7A7EF070`** | **★★ 强制完工——最佳调用目标** |
| `get_NeedsMoreWork` | 0 | `00007FFF7A7F03E0` | **★ 是否还需工人敲（= `_currentBuildPoints < _buildPoints`）** |
| `get_ShouldCancelBuild` | 0 | `00007FFF7A2F3CC0` | 返回常量 false |
| `InitializeBuild` | 0 | `00007FFF7A7EF150` | 初始化 |
| `IncrementBuild` | 1 | `00007FFF7A7EF130` | **加进度（int）** |
| `UpdateBuildPoints` | 1 | `00007FFF7A7EFF80` | **设置进度（float）** |
| `OverrideCurrentBuildPoints` | 1 | `00007FFF7A7EF710` | **★ 直接覆盖当前进度（float）** |
| `RetrieveData` | 0 | `00007FFF7A7EFD00` | 存档 |
| `ApplyData` | 1 | `00007FFF7A7EE750` | 读档 |
| `SendConstructionCompleteRPC` | 0 | `00007FFF7A7EFD70` | 网络 |
| `SendIncrementBuildRPC` | 0 | `00007FFF7A7EFEB0` | 网络 |
| `RecvConstructionCompleteRPC` | 0 | `00007FFF7A7EFA10` | 网络 |
| `RecvIncrementBuildRPC` | 0 | `00007FFF7A7EFA20` | 网络 |

#### `AssembledBuildProject.AssembledBuildProject`（size=0xE8）
这是**大型工程**（投石机/大型建筑的分部件组装），与普通建造是**两套独立机制**。
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| 0x40 | `AssembledBuildPart` | `_realPartTemplate` | 真部件模板 |
| 0x48 | `SpriteRenderer` | `_fakePartTemplate` | 假部件模板 |
| 0x50 | `GameObject` | `_spawnsObjectWhenComplete` | 完工生成物 |
| 0x58 | `GameObject` | `_launchParticles` | 发射粒子 |
| 0x60/0x68/0x70 | `AudioEmitter` | `_launchSound`/`_partFloatSound`/`_partAttachSound` | 音效 |
| 0x78 | `System.Boolean` | `_copyTransformValuesFromProject` | |
| 0x7C | `System.Single` | `_partFlyingDuration` | 部件飞行时长 |
| **0x80** | **`System.Int32`** | **`_workPerPart`** | **★ 每部件所需工作量** |
| 0x84 | `System.Int32` | `_maxWorkers` | 最大工人数 |
| 0x88 | `System.Boolean` | `_workersCanFlee` | |
| 0x8C | `System.Int32` | `_partsSpawnedPerPay` | 每次付款生成部件数 |
| 0x90 | `System.Boolean` | `_useCustomWorkArea` | |
| 0x94 | `FloatRange` | `_workArea` | 工作区域 |
| **0x9C** | **`System.Boolean`** | **`_autoCompleteWhenAssembled`** | **★ 组装完成即自动完工** |
| 0x9D | `System.Boolean` | `_overrideDefaultWorkablePriority` | |
| 0xA0 | `System.Int32` | `_workablePriority` | |
| 0xA8 | `List<Component>` | `_spawnerComponents` | |
| 0xB0 | `List<IAssembledBuildProjectSpawner>` | `_spawners` | |
| 0xB8 | `Dictionary<...>` | `_spawnerPartMap` | |
| **0xC0** | **`List<AssembledBuildPart>`** | **`_spawnedParts`** | **★ 已生成部件列表（进度）** |
| 0xC8 | `List<SpriteRenderer>` | `_fakePartPool` | |
| S 0x00 | `System.Single` | `MAX_ROTATE_ANGLE` | |
| 0xD0 | `System.Action` | `OnAssemblyComplete` | 完工回调 |
| 0xD8 | `CRPCHeader` | `_parentHeaderRef` | |
| 0xE0 | `System.Int32` | `_assemblePartRpcIndex` | |
| 0xE4 | `System.Int32` | `_spawnFinishedObjectRpcIndex` | |

关键方法：
| 方法 | argc | p0 | 说明 |
|---|---|---|---|
| `SpawnPart` | 2 | `00007FFF7A6FEA40` | 生成部件 |
| `PartAssemblyRoutine` | 1 | `00007FFF7A6FE200` | 组装协程 |
| `AllSpawnersComplete` | 0 | `00007FFF7A6FCEF0` | 全部生成完 |
| **`SpawnFinishedObject`** | **0** | **`00007FFF7A6FE880`** | **★ 生成完工对象** |
| `InstantiateFinishedObject` | 0 | `00007FFF7A6FD7D0` | 实例化 |
| `ShowProjectFinishedVisuals` | 0 | `00007FFF7A6FE760` | 完工视觉 |
| `DebugSpawnFinishedObject` | 0 | `00007FFF7A6FD650` | **调试用完工** |
| `get_OnPay` | 0 | `00007FFF7A6FF280` | |

#### `EraUpgradeBuilding`（size=0x28）
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| 0x20 | `TechnologyAge` | `techAge` | 时代等级 |

方法：`Start`(argc=0, p0=`00007FFF7A35AAB0`)。**与建造进度无关**，只是时代升级标记。

#### `WorkableTree`（size=0xC8）— 树用 `strength` 而非 HP
| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| **0x40** | **`System.Int32`** | **`strength`** | **★ 树的耐久（不是 Damageable）** |
| 0x44 | `IntRange` | `coinYield` | 掉落金币 |
| 0x4C | `CurrencyType` | `yieldCurrencyType` | |
| 0x50 | `Vector2` | `coinDropForce` | |
| 0x58 | `PoolSpawner` | `leavesSpawner` | |
| 0x60 | `SpriteRenderer` | `trunk` | |
| 0x68 | `SpriteRendererFX` | `moss` | |
| 0x70 | `SpriteRenderer[]` | `chopMarks` | 砍痕 |
| 0x78 | `AudioEmitter` | `chopSound` | |
| 0x80 | `System.Action` | `OnTreeChopped` | |
| 0x88 | `CRPCHeader` | `parentHeaderRef` | |
| **0x90** | **`System.Boolean`** | **`_marked`** | **★ 被标记要砍** |
| 0x91 | `System.Boolean` | `_chopped` | 已砍 |
| 0x92 | `System.Boolean` | `_fellRight` | |
| 0x98 | `Animator` | `_animator` | |
| 0xA0 | `SpriteRendererFX[]` | `_childSpriteFX` | |
| 0xA8 | `ParticleSystem` | `_particleSystem` | |
| 0xB0 | `SpriteRenderer` | `_spriteRenderer` | |
| 0xB8 | `RandomizeSprite` | `trunkRandRef` | |
| 0xC0 | `System.Int32` | `_collapseRPCIndex` | |
| **S 0x08** | **`System.Boolean`** | **`AutoChopTrees`** | **★ 静态：自动砍树** |
| 0xC4 | `System.Boolean` | `hasDecrementedStats` | |

#### `WorkableWharf`（size=0x58）
| 偏移 | 类型 | 名称 |
|---|---|---|
| 0x40 | `Wharf` | `_wharf` |
| 0x48 | `CRPCHeader` | `parentHeaderRef` |
| 0x50 | `System.Int32` | `_workDoneIndex` |

#### `FireTower`（size=0x150）/ `OilFireArcherTower`（size=0x118）/ `Catapult`（size=0x190）/ `Ballista`（size=0x110）
这些是**战斗装置**，字段以发射/瞄准参数为主（`_releaseTime`、`_scanRange`、`_reloadWorkTime`、`_fireJarsActiveNum` …）。
- `Catapult` **有** `Damageable _damageable` @ **0x100**
- `Ballista` 的 `_target` @ 0xD8 是**目标**，不是自己的血量
- `FireTower` / `OilFireArcherTower` **没有** `Damageable` 字段

---

## 2. 建造进度：状态机与关键字段

### 2.1 「未完工 → 完工」状态机

```
玩家付款放置建筑
      ↓
WorkableBuilding.Start / RegisterWork          (p0=00007FFF7A675F70 / 00007FFF7A675E50)
      ↓
ConstructionBuildingComponent.InitializeBuild   (p0=00007FFF7A7EF150)
      ├─ _hasStarted (0x78) = true
      ├─ _currentBuildPoints (0x64) = 0
      └─ _buildPoints (0x30) = 所需总点数
      ↓
工人来敲 ──► OnJobFinish → IncrementBuild(int)   (p0=00007FFF7A7EF130)
                        → UpdateBuildPoints(float) (p0=00007FFF7A7EFF80)
                        → _currentBuildPoints (0x64) += 增量
      ↓
get_NeedsMoreWork (p0=00007FFF7A7F03E0)  判断  _currentBuildPoints < _buildPoints
      ├─ true  → 继续敲（还在施工中）
      └─ false → ConstructionComplete(bool)  (p0=00007FFF7A7EEA70)
                        ↓
                 WorkableBuilding.ConstructionComplete (p0=00007FFF7A675B30)
                        ↓
                 WorkableBuilding.CompleteConstruction (p0=00007FFF7A675B10)
                        ↓
                 OnConstructionComplete 回调 (0x28 / WorkableBuilding 0x40)
                        ↓
                 拆脚手架 Scaffolding，播放完工音效，Stat 统计 +1
```

**强制完工的快捷入口**：`ConstructionBuildingComponent.ForceComplete()`（p0=`00007FFF7A7EF070`）——它就是"跳过所有敲击、直接完工"的官方函数，绕过了 `_buildPoints` 累加过程。

### 2.2 「进度」到底存在哪个字段

**两个字段配合**：

| 字段 | 偏移 | 类型 | 含义 |
|---|---|---|---|
| `_buildPoints` | **0x30** | `System.Int32` | 目标总量（需要多少点） |
| `_currentBuildPoints` | **0x64** | `System.Single` | **当前进度值** ← 真正要改的 |
| `_hasStarted` | **0x78** | `System.Boolean` | 是否已开工 |
| `<AllAutoBuild>k__BackingField` | **S 0x00** | `System.Boolean` | **静态**：全局自动建造 |

> **证据**：`WorkableBuildingData`（存档结构）里也只有这两个语义：
> ```
> [CLASS] WorkableBuildingData  :  System.Object   size=0x18 flags=0x102000
>     F i off=0x0010 System.Single                          currentBuildPoints
>     F i off=0x0014 System.Boolean                         usesConstructionBuildingComponent
> ```
> `ConstructionBuildingComponentSaveData` 同样只存一个 `CurrentBuildPoints`（off=0x10）。
> **存档只序列化 `currentBuildPoints` 一个进度值** —— 这是 `_currentBuildPoints`(0x64) 就是唯一进度字段的强证据。

### 2.3 完工判定

`get_NeedsMoreWork`（p0=`00007FFF7A7F03E0`）返回 bool，逻辑等价于：

```c
bool NeedsMoreWork() { return _currentBuildPoints(0x64) < (float)_buildPoints(0x30); }
```

所以把 `_currentBuildPoints` 写成 `>= _buildPoints` 就能让所有"还需敲"判定失效。

---

## 3. 建筑血量与无敌

### 3.1 建筑是否挂 `Damageable`？

**部分建筑有，部分没有。** dump 全量扫描所有含 `Damageable` 实例字段的类，建筑相关的只有：

| 类 | 偏移 | 字段名 |
|---|---|---|
| `Wall` | 0x78 | `_damageable` |
| `Catapult` | 0x100 | `_damageable` |
| `Barrier` | 0x60 | `damageable` |

**没有** `Damageable` 字段的建筑类：`Tower`、`WorkableBuilding`、`FireTower`、`OilFireArcherTower`、`WorkableTree`、`WorkableWharf`、`Farmhouse`、`Farmland`、`Scaffolding`。

这意味着：
- **城墙**：直接 `Wall._damageable`（0x78）拿 `Damageable`，改 `_isInvulnerable` 即可。
- **塔 / 农田 / 其他建筑**：没有固定偏移字段可依赖 → **必须运行时用 Unity 的 `GetComponentInChildren<Damageable>()`**（通过 IL2CPP API 调用），或 hook 通用的 `Damageable.ReceiveDamage` 做"己方建筑"判定。
- **树**：根本没有 `Damageable`，用的是 `WorkableTree.strength`（0x40）。

### 3.2 `Damageable` 完整关键字段（`dump.txt:8738`，size=0x110）

| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| 0x20 | `Damageable.PreDamageEvent` | `OnPreReceiveDamage` | **★ 伤害前置事件（可取消）** |
| 0x28 | `Damageable.DamageEvent` | `OnReceiveDamage` | 受伤事件 |
| 0x30 | `System.Action` | `OnReceiveRemoteDamage` | 远程伤害 |
| 0x38 | `Damageable.DamageEvent` | `OnReceiveDelayedDamage` | 延迟伤害 |
| 0x40 | `System.Action` | `OnDelayDamageEnd` | |
| 0x48 | `Damageable.DeathEvent` | `OnDeath` | **★ 死亡事件** |
| 0x50 | `DamageSource` | `damagedBy` | 伤害来源 |
| 0x54 | `DamageSurface` | `surface` | |
| **0x58** | **`System.Boolean`** | **`useHitPoints`** | **★ 是否使用 HP 系统** |
| 0x5C | `System.Int32` | `initialHitPoints` | 初始 HP |
| 0x60 | `System.Boolean` | `destroyOnDeath` | 死亡即销毁 |
| 0x68 | `AudioEmitter` | `deathSound` | |
| 0x70 | `AudioEmitter[]` | `biomeAlternateDeathSounds` | |
| 0x78 | `UnityEngine.Color` | `flashColor` | 受击闪色 |
| 0x88 | `AudioEmitter` | `hitSound` | |
| 0x90 | `AudioEmitter` | `hitSoundArrow` | |
| 0x98 | `System.Boolean` | `isLowPriority` | 低优先级目标 |
| 0x99 | `System.Boolean` | `alwaysAttackable` | 总是可被攻击 |
| 0x9A | `System.Boolean` | `persistOnDeath` | 死亡后保留 |
| **0x9B** | **`System.Boolean`** | **`ignoredWhenInvulnerable`** | **★ 无敌时被忽略** |
| **0x9C** | **`System.Boolean`** | **`isInvulnerableInitially`** | **★ 初始即无敌** |
| 0xA0 | `SpriteRendererFX[]` | `<_spriteFXs>k__BackingField` | |
| 0xA8 | `AnimationSync` | `_animSync` | |
| 0xB0 | `IAnimReceiver` | `_otherAnimRecv` | |
| 0xB8 | `IUnitControllable` | `_unitControllable` | |
| 0xC0 | `Dictionary<GameObject,Coroutine>` | `activeContinualDamageRoutines` | |
| 0xC8 | `System.Boolean` | `syncHP` | 网络同步 HP |
| 0xD0 | `CRPCHeader` | `parentHeaderRef` | |
| 0xD8 | `System.Int32` | `_hitFlashIndex` | RPC |
| 0xDC | `System.Int32` | `_hpIndex` | RPC |
| 0xE0 | `System.Int32` | `_invulnerableIndex` | RPC |
| 0xE4 | `System.Int32` | `_hitSoundIndex` | RPC |
| 0xE8 | `UnityEngine.Rigidbody2D` | `_rb` | |
| **0xF0** | **`Player`** | **`_playerRef`** | **★ 归属玩家（区分敌我！）** |
| 0xF8 | `IEnumerator` | `_delayDamageRoutine` | |
| **0x100** | **`System.Boolean`** | **`_isInvulnerable`** | **★★ 无敌标志位** |
| **0x104** | **`System.Int32`** | **`_hitPoints`** | **★★ 当前血量** |
| 0x108 | `System.Boolean` | `<IsInBlockingFormation>k__BackingField` | |
| **0x109** | **`System.Boolean`** | **`<isDead>k__BackingField`** | **★ 已死标志** |

方法：
| 方法 | argc | p0 | 说明 |
|---|---|---|---|
| `get_invulnerable` | 0 | `00007FFF7A812B20` | 读无敌 |
| **`set_invulnerable`** | **1** | **`00007FFF7A812FD0`** | **★ 写无敌（可能带 RPC 同步）** |
| `get_hitPoints` | 0 | `00007FFF7A57A500` | 读 HP |
| `set_hitPoints` | 1 | `00007FFF7A812F00` | 写 HP |
| `get_HealthPercentage` | 0 | `00007FFF7A812B00` | 血量百分比 |
| `get_DamagePercentage` | 0 | `00007FFF7A812AC0` | 伤害百分比 |
| `get_isDead` | 0 | `00007FFF7A812B30` | 是否已死 |
| `GetActualHp` | 0 | `00007FFF7A810FC0` | 实际 HP |
| `IsDamagedBy` | 1 | `00007FFF7A811370` | 是否被某来源伤害 |
| `EnableDamagedBy` | 1 | `00007FFF7A810DE0` | 启用某来源 |
| `DisableDamagedBy` | 1 | `00007FFF7A810DD0` | 禁用某来源 |
| **`ReceiveFatalDamage`** | **2** | **`00007FFF7A811F80`** | **★ 致命伤（绕过普通减伤）** |
| **`ReceiveDamage`** | **3** | **`00007FFF7A811780`** | **★★ 主伤害入口（int, GameObject, DamageSource）** |
| **`ReceiveDamage`** | **4** | **`00007FFF7A811820`** | 带击退向量版本 |
| `DestroyNow` | 0 | `00007FFF7A810D60` | 立即销毁 |
| `ApplyDelayedDamage` | 7 | `00007FFF7A810620` | 延迟伤害 |
| `SendInvulnerableState` | 0 | `00007FFF7A812520` | 网络同步无敌 |
| `RecvInvulnerableState` | 0 | `00007FFF7A8121B0` | 网络接收 |
| `SendHP` / `RecvHP` | 0 | `00007FFF7A812200` / `00007FFF7A811FB0` | 网络同步 HP |

> 💡 **`_playerRef`(0xF0) 是区分敌我的关键**：己方建筑的 `Damageable._playerRef` 指向所属 `Player`，敌方单位一般为 null。用它做"只保护己方建筑"的判定，比按类名白名单更稳。

### 3.3 静态开关：`Wall.InvunerableWalls`

```
    F S off=0x0000 System.Boolean                         <InvunerableWalls>k__BackingField   (dump.txt:40916)
```

`Wall` 有一个**静态属性** `InvunerableWalls`（注意拼写），配合实例字段 `invunerableWallsRoutine`(0xC0) 和 `UpdateInvunerableState()`（p0=`00007FFF7A65FB60`）。
这看起来是**开发者调试用的全局城墙无敌开关**。设为 `true` 后所有 `Wall` 应该都会进入无敌状态。

> ⚠️ 静态字段地址 = `Il2CppClass->static_fields` 基址 + 偏移。**每个类的静态块基址不同**，需要先通过 IL2CPP API（`il2cpp_class_get_static_field_data`）或从 `Il2CppClass` 结构读取，不能全局硬编码。这是所有 `F S off=...` 字段的通用限制。

---

## 4. 王冠（Crown）

### 4.1 `Crown` 完整字段表（`dump.txt:7784`，size=0xF0，F=31）

| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| S 0x00 | `System.Single` | `DISTANCE_TO_INDICATOR_THRESHOLD` | 常量 |
| S 0x00 | `System.Single` | `MOVE_TO_HEAD_TIME` | 常量 |
| 0x20 | `System.Single` | `finalTimescale` | 时间缩放 |
| 0x24 | `System.Single` | `transitionDuration` | 过渡时长 |
| 0x28 | `UnityEngine.AnimationCurve` | `transition` | 过渡曲线 |
| 0x30 | `AudioEmitter` | `crownBounceAudio` | 弹跳音 |
| 0x38 | `FloatRange` | `hoverHeight` | 悬浮高度 |
| 0x40 | `SpriteRendererFX` | `glow` | 光效 |
| 0x48 | `UnityEngine.SpriteRenderer` | `crownRend` | 渲染器 |
| **0x50** | **`CrownType`** | **`crownType`** | **★ 王冠类型（枚举）** |
| **0x54** | **`EventCrownType`** | **`eventCrownType`** | **★ 活动王冠类型** |
| 0x58 | `AudioEmitter` | `_launchSound` | |
| 0x60 | `System.Single` | `_paymentMoveSpeed` | |
| 0x68 | `UnityEngine.AnimationCurve` | `_paymentMoveCurve` | |
| **0x70** | **`Player`** | **`<owningPlayer>k__BackingField`** | **★ 归属玩家** |
| **0x78** | **`System.Boolean`** | **`_heldByFoe`** | **★★ 被敌人持有（唯一的状态布尔）** |
| 0x80 | `UnityEngine.Collider2D` | `<collider>k__BackingField` | |
| 0x88 | `Droppable` | `<droppable>k__BackingField` | 掉落物组件 |
| 0x90 | `Persistent` | `<persistent>k__BackingField` | 持久化 |
| 0x98 | `UnityEngine.Rigidbody2D` | `_rigidbody` | 物理 |
| 0xA0 | `SpriteRendererFX` | `_spriteFX` | |
| **0xA8** | **`System.Boolean`** | **`_hovering`** | **★ 是否悬浮中（= 已掉落在地）** |
| 0xB0 | `UnityEngine.Coroutine` | `_hoverCoroutine` | |
| 0xB8 | `IEnumerator` | `_tweenTimescaleCoroutine` | |
| 0xC0 | `DG.Tweening.Tweener` | `_tweenerMove` | |
| 0xC8 | `DG.Tweening.Tweener` | `_tweenerRotate` | |
| 0xD0 | `UnityEngine.Coroutine` | `_moveToPayRoutine` | |
| 0xD8 | `CRPCHeader` | `parentHeaderRef` | 网络 |
| 0xE0 | `System.Int32` | `_heldSetIndex` | RPC |
| 0xE4 | `System.Int32` | `_spriteUpdateIndex` | RPC |
| 0xE8 | `System.Single` | `<MoveTimeUntilNearTarget>k__BackingField` | |

> **重要结论**：`Crown` **没有** `isDropped` / `isStolen` / `attached` 这类直白布尔。
> 表达"王冠状态"的字段是这三个：
> - **`_heldByFoe`(0x78)** — 被敌人持有
> - **`_hovering`(0xA8)** — 悬浮在地（已掉落）
> - **`crownType`(0x50) / `eventCrownType`(0x54)** — 类型枚举
>
> "王冠在君主头上"这个状态**不存在于 `Crown` 组件里**，而是由 `Player.hasCrown` 属性 + `Rider.activeCrown`（`Rider` 0x20）表达。

### 4.2 `Crown` 方法表（部分，M=48）

| 方法 | argc | p0 | 说明 |
|---|---|---|---|
| `get_owningPlayer` | 0 | `00007FFF7A30C230` | |
| `set_owningPlayer` | 1 | `00007FFF7A38F5A0` | |
| **`get_heldByFoe`** | 0 | `00007FFF7A7F8360` | |
| **`set_heldByFoe`** | 1 | `00007FFF7A7F8380` | **★ 设置被敌人持有** |
| `Awake` | 0 | `00007FFF7A7F6EB0` | |
| **`OnLose`** | 0 | `00007FFF7A7F7BE0` | **★ 王冠丢失（核心）** |
| `SetCrownSprite` | 0 | `00007FFF7A7F8060` | |
| **`AssignDroppedCrown`** | 1 | `00007FFF7A7F6BB0` | **★ 指派为掉落的王冠（int）** |
| `TweenTimescale` | 2 | `00007FFF7A7F8220` | |
| `Hover` | 0 | `00007FFF7A7F7350` | 悬浮协程 |
| `MoveToHead` | 3 | `00007FFF7A7F73B0` | **回到君主头上** |
| `OnCollisionEnter2D` | 1 | `00007FFF7A7F7670` | |
| `OnCollisionStay2D` | 1 | `00007FFF7A7F7720` | |
| `SendHeldByFoe` | 0 | `00007FFF7A7F7FE0` | 网络 |
| `RecvHeldByFoe` | 0 | `00007FFF7A7F7F00` | 网络 |
| `SendAssignDroppedCrown` | 1 | `00007FFF7A7F7F60` | 网络 |
| `RecvAssignDroppedCrown` | 0 | `00007FFF7A7F7EA0` | 网络 |
| `MoveToPaymentSlot` | 3 | `00007FFF7A7F7500` | |
| `MoveToPaymentIndicatorRoutine` | 4 | `00007FFF7A7F7450` | |
| `CancelMoveToHead` | 0 | `00007FFF7A7F7140` | |
| `DropFromIndicator` | 0 | `00007FFF7A7F7200` | |
| `PayableTransactionComplete` | 0 | `00007FFF7A7F7C10` | |
| `get_CurrencyType` | 0 | `00007FFF7A3C2710` | |

### 4.3 `CrownType` / `CrownTarget` / `CrownPair`

```
[CLASS] CrownType  :  System.Enum   size=0x14 flags=0x101
    F i off=0x0010 System.Int32    value__
    F S off=0x0000 CrownType       Regular
    F S off=0x0000 CrownType       Cursed
    F S off=0x0000 CrownType       Event

[CLASS] CrownTarget  :  System.Enum   size=0x14 flags=0x103
    F i off=0x0010 System.Int32    value__
    F S off=0x0000 CinematicSequence_CrownLoss.CrownTarget Troll
    F S off=0x0000 CinematicSequence_CrownLoss.CrownTarget Crownstealer
    F S off=0x0000 CinematicSequence_CrownLoss.CrownTarget Ghost
```

`CrownTarget` 是**嵌套在 `CinematicSequence_CrownLoss` 里的枚举**，取值 `Troll / Crownstealer / Ghost` —— 对应三种丢冠过场动画（谁把王冠拿走）。

```
[CLASS] CrownPair  :  System.ValueType   size=0x18 flags=0x102109
    F i off=0x0010 CrownType        CrownType
    F i off=0x0014 EventCrownType   EventCrownType
    F S off=0x0000 CrownPair        Regular
    F S off=0x0008 CrownPair        Cursed
```

`CrownPair` 是 **struct**（不是类），大小 0x18，含两个枚举字段，偏移 0x10 / 0x14。

> ⚠️ **编码陷阱**：`Player._crownPair` 在 **0x0264**，类型是 `CrownPair`（值类型，16 字节有效数据 + 8 字节对象头）。
> 所以 `CrownType` 实际落在 `Player + 0x0264 + 0x10 = Player + 0x0274`，
> `EventCrownType` 落在 `Player + 0x0264 + 0x14 = Player + 0x0278`。
> 同理 `CrownPair.Regular` 静态实例在类静态块 offset 0x00，`Cursed` 在 0x08。

### 4.4 `CrownSaveData` / `RiderCrown` / `CinematicSequence_CrownLoss`

```
[CLASS] CrownSaveData  :  System.Object   size=0x18 flags=0x102001
    F i off=0x0010 IslandSaveData.PersistentLink          owningPlayer
```

存档只记录 `owningPlayer` —— **王冠的持久化状态极简**，说明"王冠是否掉落"主要是运行时状态。

```
[CLASS] RiderCrown  :  UnityEngine.MonoBehaviour   size=0x48
    F i off=0x0020 CrownType        _type
    F i off=0x0028 List<SpriteLibrary>                     _spriteLibraries
    F i off=0x0030 List<EventCrownSpriteLibraryPair>       _eventSpritesLibraryPairs
    F i off=0x0038 List<SpriteLibraryAsset>                _defaultCrownSpriteLibraryAssets
    F i off=0x0040 EventCrownType   _eventType
```

`RiderCrown` 是**君主头上王冠的纯视觉组件**（只有精灵库），不含任何逻辑状态。它挂在 `Rider` 上：

```
[CLASS] Rider  :  UnityEngine.MonoBehaviour   size=0x78
    F i off=0x0020 RiderCrown                             activeCrown
    F i off=0x0038 List<RiderCrown>                       allCrowns
    M SetCrown      argc=4 p0=00007FFF7A569970  args=(CrownPair, Material, Sprite, Boolean)
    M GetCrownComponent / TryGetCrownComponent
```

`CinematicSequence_CrownLoss`（size=0xB8）是**丢冠过场动画**：
| 偏移 | 类型 | 名称 |
|---|---|---|
| 0x50 | `GameObject` | `caveSceneRoot` |
| 0x58 | `BlenderState` | `caveBlendState` |
| 0x60 | `Transform` | `ghostCrownHolder` |
| 0x68 | `Transform` | `trollCrownHolder` |
| 0x70 | `Transform` | `crownstealerCrownHolder` |
| 0x78 | `Transform` | `crown` |
| 0x80 | `System.Single` | `crownSpringStrength` |
| 0x84 | `System.Single` | `crownSpringDampening` |
| 0x88 | `Vector2` | `crownVelocity` |
| 0x90 | `Transform` | `crownTarget` |
| 0x98 | `System.Boolean` | `crownLockedToTarget` |
| 0x9C/0xA0/0xA4 | `System.Int32` | `AP_CaveScene` / `AP_Vanish` / `AP_EnemyType` |
| 0xA8/0xB0 | `BlenderStateFader` | `p1CaveFader` / `p2CaveFader` |

方法：`get_sequenceID`(p0=`00007FFF7A2F8250`)、`Init`(p0=`00007FFF7A7D99F0`)、`PlayRoutine`(p0=`00007FFF7A7D9EB0`)、`LateUpdate`(p0=`00007FFF7A7D9B30`)、`Anim_SetTarget`(argc=1, p0=`00007FFF7A7D9910`)。

### 4.5 ★ 王冠掉落的触发路径（全部方法，带 argc 与 p0）

dump 全量搜索 `Crown|Lose` 得到以下**游戏逻辑**方法（已剔除 System/Unity 噪音）：

| 类 | 方法 | argc | p0 | 说明 |
|---|---|---|---|---|
| **`Player`** | **`LoseCrown`** | **3** | **`00007FFF7A4E5B10`** | **★★ 君主丢冠总入口** `args=(Vector2, Boolean, Boolean)` |
| `Player` | `LoseCrownDebug` | 0 | `00007FFF7A4E5A50` | 调试丢冠 |
| `Player` | `OnLoseHandler` | 0 | `00007FFF7A4E3B60` | 订阅 `Game.OnLose` |
| `Player` | `OnReceiveDamageHandler` | 3 | `00007FFF7A4E66C0` | 订阅 `Damageable.OnReceiveDamage` |
| `Player` | `PickupCrown` | 2 | `00007FFF7A4E8220` | 捡回王冠协程 |
| `Player` | `AttemptCrownCollect` | 1 | `00007FFF7A4E2BA0` | 尝试收冠 |
| `Player` | `CleanUpPartialCrownPickup` | 0 | `00007FFF7A4E34F0` | 清理未完成的捡冠 |
| `Player` | `ForceCrownState` | 2 | `00007FFF7A4E4A90` | **★ 强制王冠状态** `(Boolean, Boolean)` |
| `Player` | `SetCrownVisibility` | 1 | `00007FFF7A4EA5A0` | 王冠显示 |
| `Player` | `ResetCrownStatus` | 0 | `00007FFF7A4E8E60` | 重置王冠状态 |
| `Player` | `SwapForCrownlessAnim` | 2 | `00007FFF7A4EBE30` | 换成无冠动画 |
| `Player` | `ToggleCrownOverridePlayer` | 1 | `00007FFF7A4EBFC0` | |
| `Player` | `get_hasCrown` / `set_hasCrown` | 0 / 1 | `00007FFF7A4EF5B0` / `00007FFF7A4EF9F0` | **★ 是否有冠** |
| `Player` | `SetCrownTypes` | 1 | `00007FFF7A4EA4C0` | 设置冠类型 |
| `Player` | `UpdateCrownType` | 0 | `00007FFF7A4E5210` | |
| **`Enemy`** | **`HandleCrownLoss`** | **3** | **`00007FFF7A357F60`** | **★★ 敌人夺冠** `args=(Damageable, Crown, IUnitController)` |
| **`GreedKnight`** | **`GrabCrownRoutine`** | **1** | **`00007FFF7A3C66E0`** | **★★ 贪魔骑士抓冠协程** `args=(Crown)` |
| **`CrownStealer`** | **`PickupCrown`** | **1** | **`00007FFF7A7F4ED0`** | **★★ 偷冠者捡冠** `args=(Crown)` |
| **`Troll`** | **`PickupCrown`** | **1** | **`00007FFF7A60E750`** | **★★ 巨魔捡冠** `args=(Crown)` |
| `Crown` | `OnLose` | 0 | `00007FFF7A7F7BE0` | **★ 王冠丢失** |
| `Crown` | `AssignDroppedCrown` | 1 | `00007FFF7A7F6BB0` | 指派掉落冠 |
| `Game` | `Lose` | 1 | `00007FFF7A389480` | **★ 游戏失败** |
| `Game` | `add_OnLose` / `remove_OnLose` | 1 | `00007FFF7A38E280` / `00007FFF7A38F320` | 失败事件 |
| `Game` | `SendLoseToClient` | 0 | `00007FFF7A38BA10` | 网络 |
| `Game` | `Recv_ClientsideLose` | 0 | `00007FFF7A38A950` | 网络 |
| `Stats` | `HandleOnLose` | 0 | `00007FFF7A5DB0B0` | 统计 |
| `CaveHelper` | `LoseInCave` | 0 | `00007FFF7A742960` | 洞穴内失败 |
| `Kingdom` | `GetNearestDroppedCrown` | 1 | `00007FFF7A3F25F0` | |
| `Kingdom` | `GetDroppedCrown` | 1 | `00007FFF7A3F1EB0` | |
| `Kingdom` | `GetCrownsLeft` | 0 | `00007FFF7A3F1D20` | |
| `Kingdom` | `CheckCrownsAtLaunch` | 0 | `00007FFF7A3EF030` | |
| `Kingdom` | `CheckCrownsAfterCoop` | 0 | `00007FFF7A3EED20` | |
| `DebugTools` | `OnButtonLoseCrown` | 0 | `00007FFF7A823D00` | 调试按钮 |
| `DebugTools` | `LoseCrown` | 1 | `00007FFF7A81F580` | 调试 `(Player)` |
| `WorldEatingSerpent` | `IsDroppedCrownNearby` | 0 | `00007FFF7A680450` | |

**掉落状态载体**：
```
[CLASS] Kingdom  :  Manager   size=0x3A8 flags=0x100001 [MonoBehaviour]
    F S off=0x0000 System.Single                          crownLostGraceTimer
    F S off=0x0000 System.Single                          crownLostGraceDistance
    F i off=0x00E0 Crown                                  droppedP1Crown
    F i off=0x00E8 Crown                                  droppedP2Crown
```
`Kingdom.droppedP1Crown`(0xE0) / `droppedP2Crown`(0xE8) 就是**地上掉落的王冠**（P1/P2 各一）。静态 `crownLostGraceTimer` / `crownLostGraceDistance` 是丢冠后的宽限计时/距离。

### 4.6 ★★ 核心判定：让君主 `Damageable` 无敌能否阻止掉冠？

**答案：不能。必须单独处理掉冠逻辑。**

**证据链（5 条独立证据）**：

**证据 1 —— 掉冠有独立的、不经过 `Damageable` 的入口。**
`Player.LoseCrown`（argc=3, p0=`00007FFF7A4E5B10`）是 `Player` 自己的方法，参数是 `(Vector2, Boolean, Boolean)`，**不接收 `DamageSource`，也不接收伤害值**。它不是伤害结算的一部分，而是一个独立的"把王冠脱手"动作。
（`dump.txt:26581`）

**证据 2 —— 敌人可以在君主活着时直接把王冠抓走。**
`GreedKnight.GrabCrownRoutine`（argc=1, args=`(Crown)`, p0=`00007FFF7A3C66E0`）接收一个 `Crown` 对象作为参数 —— 这是**抓取动作**，作用于王冠实体，与君主的 `Damageable` 完全无关。
（`dump.txt:15758`）
`GreedKnight` 甚至有专门的字段 `_grabCrownRoutine`（0x188）缓存这个协程。

**证据 3 —— `Crown` 组件自带独立的状态位。**
`Crown._heldByFoe`(0x78) + `Crown.get/set_heldByFoe`（p0=`00007FFF7A7F8360`/`00007FFF7A7F8380`）+ `SendHeldByFoe`/`RecvHeldByFoe`。
如果掉冠只是"伤害的副作用"，`Crown` 不需要一个专门的"被敌人持有"布尔和配套网络 RPC。

**证据 4 —— 三种独立的夺冠者实现。**
`CrownStealer.PickupCrown`(p0=`00007FFF7A7F4ED0`)、`Troll.PickupCrown`(p0=`00007FFF7A60E750`)、`GreedKnight.GrabCrownRoutine`(p0=`00007FFF7A3C66E0`) —— 三个不同的敌人类各自实现夺冠。
更关键的是 `Enemy.HandleCrownLoss(Damageable, Crown, IUnitController)`（p0=`00007FFF7A357F60`）作为基类方法，被三个子类共用。这构成一条**与伤害系统并列的完整子系统**。

**证据 5 —— `CrownTarget` 枚举证明掉冠是"演出事件"而非"死亡后果"。**
`CrownTarget { Troll, Crownstealer, Ghost }` 用于 `CinematicSequence_CrownLoss`（丢冠过场动画），并有 `Anim_SetTarget`(p0=`00007FFF7A7D9910`) 切换动画。
如果只是"君主死了所以掉冠"，不需要按**谁拿走的**来分三种动画。

**补充说明 —— 无敌仍然有价值，但只解决一半问题：**
把君主 `Damageable._isInvulnerable`(0x100) 设为 `true`，可以：
- ✅ 阻止君主被伤害致死（从而阻止"死亡→掉冠"这条路径）
- ❌ **不能**阻止 `GreedKnight` 在战斗中直接把王冠从头上抓走
- ❌ **不能**阻止 `CrownStealer` / `Troll` 捡起已掉落的王冠
- ❌ **不能**阻止 `Enemy.HandleCrownLoss` 被调用

**所以「王冠不会掉落」必须 hook `Player.LoseCrown`。**

### 4.7 可设置的"王冠状态"字段汇总

| 目标 | 字段 | 偏移 | 类型 | 备注 |
|---|---|---|---|---|
| 君主是否有冠 | `Player.hasCrown` 属性 | — | bool | 属性，用 `set_hasCrown`(p0=`00007FFF7A4EF9F0`) |
| 君主冠类型 | `Player._crownPair` | 0x0264 | `CrownPair`(struct) | `CrownType` 在 +0x10，`EventCrownType` 在 +0x14 |
| 王冠被敌人持有 | `Crown._heldByFoe` | 0x78 | bool | 设 false 可让游戏认为未被抢 |
| 王冠悬浮在地 | `Crown._hovering` | 0xA8 | bool | 掉落状态 |
| 王冠类型 | `Crown.crownType` | 0x50 | `CrownType` 枚举 | |
| 王冠归属 | `Crown.<owningPlayer>` | 0x70 | `Player*` | |
| 地上掉落冠 | `Kingdom.droppedP1Crown` | 0xE0 | `Crown*` | |
| 地上掉落冠 P2 | `Kingdom.droppedP2Crown` | 0xE8 | `Crown*` | |
| 君主正在捡冠 | `Player._isPickingUpCrown` | 0x01E0 | bool | |
| 捡冠中的对象 | `Player._crownBeingPickedUp` | 0x01E8 | `Crown*` | |
| 君主 Damageable | `Player.<damageable>` | 0x0100 | `Damageable*` | |

> ⚠️ **没有** `isDropped` / `isStolen` / `attached` 这三个字段。dump 全量字段名搜索确认不存在。表达掉落状态用 `_hovering`(0xA8)，表达被抢用 `_heldByFoe`(0x78)，表达"君主有冠"用 `Player.hasCrown` 属性。

---

## 5. Hook 点选择与返回值写法

### 5.1 三个功能的最优 hook 点

| 功能 | 首选方案 | 目标 | p0 | 类型 |
|---|---|---|---|---|
| **建造秒完成** | **A. 每帧写进度** | `ConstructionBuildingComponent._currentBuildPoints` | 0x64 | 写内存 |
| | **B. 调用完工函数** | `ConstructionBuildingComponent.ForceComplete` | `00007FFF7A7EF070` | 调用 |
| | C. 全局自动建造 | `set_AllAutoBuild` | `00007FFF7A7F0530` | 调用 |
| | D. 大型工程 | `AssembledBuildProject.SpawnFinishedObject` | `00007FFF7A6FE880` | 调用 |
| **建筑无敌** | **A. 写无敌位** | `Damageable._isInvulnerable` | 0x100 | 写内存 |
| | **B. 拦伤害** | `Damageable.ReceiveDamage`(argc=3) | `00007FFF7A811780` | **hook** |
| | C. 拦致命伤 | `Damageable.ReceiveFatalDamage`(argc=2) | `00007FFF7A811F80` | **hook** |
| | D. 城墙全局开关 | `Wall.<InvunerableWalls>` | S 0x00 | 写静态 |
| **王冠不掉** | **A. 拦丢冠** | `Player.LoseCrown`(argc=3) | `00007FFF7A4E5B10` | **hook** |
| | B. 拦敌人夺冠 | `Enemy.HandleCrownLoss`(argc=3) | `00007FFF7A357F60` | **hook** |
| | C. 拦抓冠协程 | `GreedKnight.GrabCrownRoutine`(argc=1) | `00007FFF7A3C66E0` | **hook** |
| | D. 拦偷冠者 | `CrownStealer.PickupCrown`(argc=1) | `00007FFF7A7F4ED0` | **hook** |
| | E. 君主无敌 | `Player.<damageable>` + 0x100 | 0x100 | 写内存 |

### 5.2 「跳过逻辑」的写法

#### 情况 1：返回 `System.Void` 的方法 —— 直接 `ret`

`Player.LoseCrown`、`Enemy.HandleCrownLoss`、`Damageable.ReceiveDamage`、`Damageable.ReceiveFatalDamage`、`ConstructionBuildingComponent.ForceComplete`、`Crown.OnLose` 全都是 `ret=System.Void`。

x64 Windows 下 `void` 方法**没有返回值寄存器**，所以"跳过原逻辑"就是**什么都不做直接返回**：

```c
// 以 Player.LoseCrown 为例（argc=3, void）
// 原函数签名（IL2CPP 方法指针）：void (*)(Player* __this, Vector2 pos, bool a, bool b)
// 参数：RCX=this, XMM1=Vector2(8字节, 占 XMM1 低64位), R8=bool a, R9=bool b
static void __fastcall Hook_LoseCrown(void* __this, Vec2 pos, bool a, bool b)
{
    if (g_bCrownNeverDrops) return;   // ★ 什么都不做 = 王冠不掉
    Orig_LoseCrown(__this, pos, a, b);
}
```

#### 情况 2：返回 `System.Boolean` —— `AL = 1`

`Damageable.get_invulnerable`、`WorkableBuilding.get_UnderConstruction`、`ConstructionBuildingComponent.get_NeedsMoreWork`、`Wall.IDestructiblePlayerStructure.get_IsImmune` 返回 bool（在 `AL` 中）。

```c
static bool __fastcall Hook_NeedsMoreWork(void* __this)
{
    if (g_bInstantBuild) return false;   // 编译期等价于 xor eax,eax; ret
    return Orig_NeedsMoreWork(__this);
}
```

若用**裸 inline hook**（不写完整 hook 函数，只补 shellcode），返回 false 的补丁是：
```asm
xor eax, eax        ; 33 C0
ret                 ; C3
```
返回 true：
```asm
mov al, 1           ; B0 01
ret                 ; C3
```

#### 情况 3：返回 `System.Int32` —— `EAX`

`Damageable.get_hitPoints`、`Wall.GetTotalMaxHitPoints` 等。
```asm
mov eax, 7FFFFFFFh  ; B8 FF FF FF 7F
ret                 ; C3
```

#### 情况 4：返回浮点 —— `XMM0`

`Damageable.get_HealthPercentage`、`WorkableBuilding.GetJobOffset` 等。
```asm
movss xmm0, [rip+const_1f]   ; F3 0F 10 05 xx xx xx xx
ret                          ; C3
```

### 5.3 标准 trampoline hook 骨架（x64，14 字节 `jmp [rip+0]`）

```c
// 14 字节绝对跳转：FF 25 00000000 <8字节目标地址>
static void* InstallHook(void* target, void* hookFn, uint8_t* trampolineOut, size_t stealLen)
{
    // 1) 反汇编 target 处至少 stealLen 字节的完整指令（通常 14~16 字节）
    // 2) 拷贝到 trampolineOut（RWX 内存）
    // 3) 在 trampoline 末尾写 14 字节 jmp 回 target+stealLen
    // 4) 在 target 处写 14 字节 jmp hookFn（先 VirtualProtect 成 RWX）
    DWORD old;
    VirtualProtect(target, stealLen, PAGE_EXECUTE_READWRITE, &old);
    uint8_t patch[14] = { 0xFF, 0x25, 0x00,0x00,0x00,0x00 };
    *(void**)(patch + 6) = hookFn;
    memcpy(target, patch, 14);
    VirtualProtect(target, stealLen, old, &old);
    FlushInstructionCache(GetCurrentProcess(), target, stealLen);
    return trampolineOut;   // 作为 Orig_XXX 调用
}
```

> **IL2CPP 特有注意事项**：
> 1. IL2CPP 方法体可能被 **IL2CPP 的 metadata 初始化**或 **Unity Burst/IL2CPP 内联**影响，尽量选 `flags` 中**非内联**的方法（`ForceComplete`、`LoseCrown` 这类 `argc>0` 或 `flags=0x86` 的通常安全）。
> 2. `p0=0000000000000000` 的方法**不可 hook**（未解析地址）。
> 3. 多个方法可能**共用同一 p0**（如 `00007FFF7A2F3CC0` 被 `get_ShouldCancelBuild`、`Wall.get_IsImmune`、`Damageable.get_IsInBlockingFormation` 共用）。**hook 这种共享 stub 会影响所有共用者**，务必确认副作用可接受，或改用"写字段"方案。
> 4. 游戏是 IL2CPP + 可能启用 **BepInEx/MelonLoader** 时，注意 hook 顺序。

---

## 6. 实现方案（伪 C++）

### 6.0 公共基础设施

```cpp
// ============ 全局配置 ============
struct TrainerConfig {
    bool instantBuild      = false;   // 建造秒完成
    bool invincibleBuildings = false; // 建筑无敌
    bool crownNeverDrops   = false;   // 王冠不掉
    bool monarchInvincible = false;   // 君主无敌（辅助，非必需）
};
TrainerConfig g_cfg;

// ============ 偏移常量（全部来自 dump，已验证） ============
namespace Off {
    // ConstructionBuildingComponent  (dump.txt:7501)
    constexpr size_t CBC_buildPoints        = 0x30;  // int   目标点数
    constexpr size_t CBC_currentBuildPoints = 0x64;  // float 当前进度
    constexpr size_t CBC_hasStarted         = 0x78;  // bool  已开工

    // WorkableBuilding  (dump.txt:41830)
    constexpr size_t WB_constructionBuilding = 0x50; // ConstructionBuildingComponent*

    // Wall  (dump.txt:40887)
    constexpr size_t Wall_damageable   = 0x78;  // Damageable*
    constexpr size_t Wall_workableBldg = 0x98;  // WorkableBuilding*

    // Damageable  (dump.txt:8738)
    constexpr size_t Dmg_useHitPoints  = 0x58;  // bool
    constexpr size_t Dmg_playerRef     = 0xF0;  // Player*
    constexpr size_t Dmg_isInvulnerable= 0x100; // bool  ★
    constexpr size_t Dmg_hitPoints     = 0x104; // int   ★
    constexpr size_t Dmg_isDead        = 0x109; // bool

    // Player  (dump.txt:26366)
    constexpr size_t Pl_damageable     = 0x100; // Damageable*
    constexpr size_t Pl_isPickingUp    = 0x01E0;// bool
    constexpr size_t Pl_crownBeingPicked=0x01E8;// Crown*
    constexpr size_t Pl_crownPair      = 0x0264;// CrownPair (struct)
    constexpr size_t Pl_playerId       = 0x026C;// int
    // CrownPair 内部
    constexpr size_t CP_crownType      = 0x10;  // CrownType enum
    constexpr size_t CP_eventCrownType = 0x14;  // EventCrownType enum

    // Crown  (dump.txt:7784)
    constexpr size_t Cr_crownType      = 0x50;  // CrownType
    constexpr size_t Cr_eventCrownType = 0x54;  // EventCrownType
    constexpr size_t Cr_owningPlayer   = 0x70;  // Player*
    constexpr size_t Cr_heldByFoe      = 0x78;  // bool  ★
    constexpr size_t Cr_hovering       = 0xA8;  // bool  ★

    // Kingdom
    constexpr size_t Kd_droppedP1Crown = 0xE0;  // Crown*
    constexpr size_t Kd_droppedP2Crown = 0xE8;  // Crown*
}

// ============ 通用内存读写 ============
template <typename T> inline T  R(void* base, size_t off)              { return *(T*)((uint8_t*)base + off); }
template <typename T> inline void W(void* base, size_t off, T v)       { *(T*)((uint8_t*)base + off) = v; }

// ============ Unity/IL2CPP 辅助 ============
// 用 IL2CPP API 找类与遍历对象；也可直接扫描 GameObject 链表
typedef void* (*t_il2cpp_object_new)(void* klass);
typedef void* (*t_il2cpp_class_from_name)(void* image, const char* ns, const char* name);
typedef void* (*t_il2cpp_domain_get)();
typedef void* (*t_il2cpp_domain_assembly_open)(void* domain, const char* name);
typedef void* (*t_il2cpp_assembly_get_image)(void* asm_);
// 静态字段基址
typedef void* (*t_il2cpp_class_get_static_field_data)(void* klass);

// 通过 UnityEngine.Object.FindObjectsOfType 或遍历
// 更简单可靠：用 IL2CPP API 拿到类型对象后，调用 UnityEngine.Object.FindObjectsOfType(Type)
extern "C" void** FindAllObjectsOfType(void* il2cppType);
```

### 6.1 功能一：建造秒完成

```cpp
// ---------- 方案 A（推荐，最稳）：每帧把所有在建组件的进度拉满 ----------
// 不 hook 任何东西，纯内存写，零崩溃风险。
void Tick_InstantBuild()
{
    if (!g_cfg.instantBuild) return;

    void* cbcClass = GetClass("", "ConstructionBuildingComponent");
    if (!cbcClass) return;

    void** objs = FindAllObjectsOfType(cbcClass);   // 或用缓存的 List
    for (int i = 0; objs && objs[i]; ++i) {
        void* cbc = objs[i];
        int   target  = R<int>  (cbc, Off::CBC_buildPoints);         // 0x30
        float current = R<float>(cbc, Off::CBC_currentBuildPoints);  // 0x64

        if (current < (float)target) {
            // 直接把进度写满 → get_NeedsMoreWork() 立即返回 false
            W<float>(cbc, Off::CBC_currentBuildPoints, (float)target);
            W<bool >(cbc, Off::CBC_hasStarted, true);   // 0x78 确保"已开工"状态
        }
    }
}

// ---------- 方案 B：主动调用 ForceComplete（立即触发完工演出/回调） ----------
// p0 = 00007FFF7A7EF070, 签名: void ForceComplete(ConstructionBuildingComponent* __this)
typedef void (__fastcall *t_ForceComplete)(void* __this);
static t_ForceComplete g_ForceComplete = nullptr;   // 解析 p0 后填充

void Tick_InstantBuild_ForceComplete()
{
    if (!g_cfg.instantBuild) return;

    void** objs = FindAllObjectsOfType(GetClass("", "ConstructionBuildingComponent"));
    for (int i = 0; objs && objs[i]; ++i) {
        void* cbc = objs[i];
        // 只对"确实还需要工作"的下手，避免每帧重复触发完工回调/音效
        if (R<float>(cbc, Off::CBC_currentBuildPoints) < (float)R<int>(cbc, Off::CBC_buildPoints)) {
            g_ForceComplete(cbc);   // 一步到位
        }
    }
}

// ---------- 方案 C：从 Wall 侧进入（若你能拿到 Wall 对象） ----------
void ForceCompleteWall(void* wall)
{
    void* wb = R<void*>(wall, Off::Wall_workableBldg);      // 0x98
    if (!wb) return;
    void* cbc = R<void*>(wb, Off::WB_constructionBuilding); // 0x50
    if (!cbc) return;

    W<float>(cbc, Off::CBC_currentBuildPoints,
             (float)R<int>(cbc, Off::CBC_buildPoints));
    if (g_ForceComplete) g_ForceComplete(cbc);
}

// ---------- 方案 D：大型工程（投石机等分部件组装） ----------
// AssembledBuildProject.AssembledBuildProject
//   _autoCompleteWhenAssembled @ 0x9C  ← 设为 true 即可"组装完自动完工"
//   _workPerPart               @ 0x80  ← 设为 0 可让每部件零工作量
void Tick_InstantAssemble()
{
    if (!g_cfg.instantBuild) return;
    void** objs = FindAllObjectsOfType(GetClass("AssembledBuildProject", "AssembledBuildProject"));
    for (int i = 0; objs && objs[i]; ++i) {
        void* p = objs[i];
        W<bool >(p, 0x9C, true);   // _autoCompleteWhenAssembled
        W<int  >(p, 0x80, 0);      // _workPerPart = 0
    }
}

// ---------- 方案 E（可选）：全局自动建造静态开关 ----------
// ConstructionBuildingComponent.<AllAutoBuild>k__BackingField  (静态, off 0x00)
// p0(set_AllAutoBuild) = 00007FFF7A7F0530, argc=1, args=(System.Boolean)
typedef void (__fastcall *t_setAllAutoBuild)(bool);
static t_setAllAutoBuild g_setAllAutoBuild = nullptr;
// 调用: g_setAllAutoBuild(true);   // 影响全局，慎用
```

**主循环选择**：在 `Present`/`Update` 的 hook 里每帧调用 `Tick_InstantBuild()`。建议**方案 A + B 组合**：先写满 `_currentBuildPoints`，再调一次 `ForceComplete` 触发正式完工流程（这样视觉、音效、Stat 统计都正常）。

### 6.2 功能二：建筑无敌

```cpp
// ---------- 层 1：全局拦截伤害（最彻底，推荐主方案） ----------
// Damageable.ReceiveDamage(argc=3)  p0 = 00007FFF7A811780
// 签名: void ReceiveDamage(Damageable* __this, int amount, GameObject* src, DamageSource source)
// 参数: RCX=this, EDX=amount, R8=src, R9=source
typedef void (__fastcall *t_ReceiveDamage)(void* __this, int amount, void* src, int source);
static t_ReceiveDamage Orig_ReceiveDamage = nullptr;

static void __fastcall Hook_ReceiveDamage(void* __this, int amount, void* src, int source)
{
    if (g_cfg.invincibleBuildings && IsFriendlyStructure(__this)) {
        return;   // ★ void 方法直接返回 = 伤害被完全吞掉
    }
    Orig_ReceiveDamage(__this, amount, src, source);
}

// 判定"己方建筑"：用 Damageable._playerRef (0xF0)
static bool IsFriendlyStructure(void* damageable)
{
    void* playerRef = R<void*>(damageable, Off::Dmg_playerRef);  // 0xF0
    if (playerRef) return true;   // 有归属玩家 → 己方

    // 没有 _playerRef 的，回退到按宿主类名判定
    return HostIsBuilding(damageable);   // Wall/Tower/Catapult/... 白名单
}

// ---------- 层 2：拦致命伤（防止绕过普通减伤） ----------
// Damageable.ReceiveFatalDamage(argc=2)  p0 = 00007FFF7A811F80
typedef void (__fastcall *t_ReceiveFatalDamage)(void* __this, void* src, int source);
static t_ReceiveFatalDamage Orig_ReceiveFatalDamage = nullptr;

static void __fastcall Hook_ReceiveFatalDamage(void* __this, void* src, int source)
{
    if (g_cfg.invincibleBuildings && IsFriendlyStructure(__this)) return;
    Orig_ReceiveFatalDamage(__this, src, source);
}

// ---------- 层 3：直接把无敌位写 true（简单、无 hook） ----------
void Tick_BuildingInvincible_FlagWrite()
{
    if (!g_cfg.invincibleBuildings) return;

    void** objs = FindAllObjectsOfType(GetClass("", "Damageable"));
    for (int i = 0; objs && objs[i]; ++i) {
        void* d = objs[i];
        if (!IsFriendlyStructure(d)) continue;
        W<bool>(d, Off::Dmg_isInvulnerable, true);   // 0x100 = true

        // 可选：顺手把血量顶满，避免"无敌之前已经掉血"的视觉残留
        int init = R<int>(d, 0x5C);                  // initialHitPoints
        if (R<int>(d, Off::Dmg_hitPoints) < init)    // _hitPoints 0x104
            W<int>(d, Off::Dmg_hitPoints, init);
    }
}

// ---------- 层 4：城墙专用快捷开关 ----------
// Wall.<InvunerableWalls>k__BackingField  (静态, off 0x00) —— 注意拼写少一个 l
void SetInvunerableWalls(bool on)
{
    void* wallClass = GetClass("", "Wall");
    void* staticData = g_il2cpp_class_get_static_field_data(wallClass);
    if (staticData) W<bool>(staticData, 0x00, on);
}
// 或直接调用 set_InvunerableWalls  p0 = 00007FFF7A65FD50, argc=1, args=(System.Boolean)
typedef void (__fastcall *t_setInvunerableWalls)(bool);
static t_setInvunerableWalls g_setInvunerableWalls = nullptr;
// g_setInvunerableWalls(true);
```

**推荐组合**：层 1 + 层 2（hook 伤害入口）+ 层 3（兜底写标志位）。层 1/2 是精确拦截，层 3 防止其它代码路径直接读 `_isInvulnerable` 时得到错误答案。

**树的特殊处理**：`WorkableTree` 没有 `Damageable`，它用 `strength`(0x40)。如果要保护树：
```cpp
void ProtectTrees()
{
    void** objs = FindAllObjectsOfType(GetClass("", "WorkableTree"));
    for (int i = 0; objs && objs[i]; ++i)
        W<int>(objs[i], 0x40, 0x7FFFFFFF);   // strength 顶满
}
```

### 6.3 功能三：王冠不会掉落

```cpp
// ============ 层 1（核心）：拦 Player.LoseCrown ============
// p0 = 00007FFF7A4E5B10, argc=3, ret=System.Void
// 签名: void LoseCrown(Player* __this, Vector2 pos, bool a, bool b)
// 参数: RCX=this, XMM1=Vector2, R8=b, R9=b  (bool 走 R8/R9)
// ⚠️ Vector2 是 8 字节值类型，按 x64 调用约定占用 XMM1（低 64 位）
typedef void (__fastcall *t_LoseCrown)(void* __this, Vector2 pos, bool a, bool b);
static t_LoseCrown Orig_LoseCrown = nullptr;

static void __fastcall Hook_LoseCrown(void* __this, Vector2 pos, bool a, bool b)
{
    if (g_cfg.crownNeverDrops) {
        // ★ 关键：什么都不做直接返回 → 王冠永不脱手
        //    同时确保王冠视觉仍在头上
        EnsureCrownVisible(__this);
        return;
    }
    Orig_LoseCrown(__this, pos, a, b);
}

// 保险：把 Player.hasCrown 拉回 true，并恢复无冠动画之外的状态
static void EnsureCrownVisible(void* player)
{
    // Player.hasCrown 是属性 → 调用 set_hasCrown  p0 = 00007FFF7A4EF9F0
    // typedef void (__fastcall *t_setHasCrown)(void*, bool);
    if (g_setHasCrown) g_setHasCrown(player, true);

    // 兜底：清掉"正在捡冠"的残留状态
    W<bool >(player, Off::Pl_isPickingUp,     false);  // 0x01E0
    W<void*>(player, Off::Pl_crownBeingPicked, nullptr);// 0x01E8
}

// ============ 层 2：拦敌人夺冠基类方法 ============
// Enemy.HandleCrownLoss(argc=3)  p0 = 00007FFF7A357F60
// 签名: void HandleCrownLoss(Enemy* __this, Damageable* d, Crown* crown, IUnitController* ctrl)
typedef void (__fastcall *t_HandleCrownLoss)(void* __this, void* dmg, void* crown, void* ctrl);
static t_HandleCrownLoss Orig_HandleCrownLoss = nullptr;

static void __fastcall Hook_HandleCrownLoss(void* __this, void* dmg, void* crown, void* ctrl)
{
    if (g_cfg.crownNeverDrops) {
        // 顺手把王冠标记为"未被敌人持有"，防止状态机卡在异常态
        if (crown) W<bool>(crown, Off::Cr_heldByFoe, false);   // 0x78
        return;
    }
    Orig_HandleCrownLoss(__this, dmg, crown, ctrl);
}

// ============ 层 3：拦三种夺冠者的抓取动作 ============
// GreedKnight.GrabCrownRoutine(argc=1)  p0 = 00007FFF7A3C66E0  ret=IEnumerator
// ⚠️ 返回 IEnumerator，不能简单 return（调用方会 null 解引用）
//    → 改为返回一个"空协程"或返回原协程但让内部逻辑无效
// CrownStealer.PickupCrown(argc=1)      p0 = 00007FFF7A7F4ED0  ret=IEnumerator
// Troll.PickupCrown(argc=1)             p0 = 00007FFF7A60E750  ret=IEnumerator

// 更安全的做法：不 hook 这三个协程，而是持续把 _heldByFoe 压回 false
void Tick_CrownStateGuard()
{
    if (!g_cfg.crownNeverDrops) return;

    void** crowns = FindAllObjectsOfType(GetClass("", "Crown"));
    for (int i = 0; crowns && crowns[i]; ++i) {
        void* c = crowns[i];
        W<bool>(c, Off::Cr_heldByFoe, false);   // 0x78 永远"没被敌人持有"
        // 注意：不要盲目把 _hovering(0xA8) 设 false，否则掉在地上的冠会消失
    }
}

// ============ 层 4（辅助）：君主无敌，掐死"死亡→掉冠"路径 ============
void Tick_MonarchInvincible()
{
    if (!g_cfg.monarchInvincible) return;

    void** players = FindAllObjectsOfType(GetClass("", "Player"));
    for (int i = 0; players && players[i]; ++i) {
        void* d = R<void*>(players[i], Off::Pl_damageable);   // 0x100
        if (!d) continue;
        W<bool>(d, Off::Dmg_isInvulnerable, true);            // 0x100
        W<bool>(d, Off::Dmg_isDead,         false);           // 0x109
    }
}
```

**⚠️ 层 3 的陷阱**：`GrabCrownRoutine` / `PickupCrown` 返回 `System.Collections.IEnumerator`。
如果 hook 后直接 `return`（不设 `RAX`），调用方拿到 `nullptr` 再调用 `GetEnumerator()`/`MoveNext()` 会**立即崩溃**。
两个安全替代：
1. **不 hook 它们**，改用层 1（`LoseCrown`）+ 层 2（`HandleCrownLoss`）+ 每帧压 `_heldByFoe`。
2. 若要 hook，必须返回一个**合法的空 IEnumerator**（例如复用游戏内某个已有的空协程对象），复杂度高，不推荐。

**推荐组合**：**层 1（`Player.LoseCrown` 直接 return）为主**，层 2 兜底，层 4 辅助。这是最小、最可靠、最不容易崩的组合。

### 6.4 初始化顺序

```cpp
BOOL WINAPI DllMain(HMODULE hMod, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hMod);
        CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            // 1) 等待 GameAssembly.dll 加载
            HMODULE ga = nullptr;
            while (!(ga = GetModuleHandleA("GameAssembly.dll"))) Sleep(100);
            g_gaBase = (uintptr_t)ga;

            // 2) 等待 IL2CPP 运行时就绪（il2cpp_domain_get 可用）
            while (!(g_il2cpp_domain_get = (t_il2cpp_domain_get)
                     GetProcAddress(ga, "il2cpp_domain_get"))) Sleep(100);
            while (!g_il2cpp_domain_get()) Sleep(100);

            // 3) 解析 IL2CPP 导出 API
            ResolveIl2CppApi(ga);

            // 4) 把 dump 里的 p0 转成运行时绝对地址
            //    ⚠️ dump 的 p0 是"当时的绝对地址"，需换算成 RVA：
            //       RVA = p0 - dumpModuleBase
            //    若不知道 dumpModuleBase，可用"相邻方法地址差"或
            //    用 il2cpp_class_get_method_from_name 直接拿方法指针（更稳！）
            g_ForceComplete   = (t_ForceComplete)   ResolveMethod(
                                    "ConstructionBuildingComponent", "ForceComplete");
            Orig_ReceiveDamage = (t_ReceiveDamage)  InstallHook(
                                    ResolveMethod("Damageable", "ReceiveDamage", 3),
                                    Hook_ReceiveDamage, ...);
            Orig_ReceiveFatalDamage = (t_ReceiveFatalDamage) InstallHook(
                                    ResolveMethod("Damageable", "ReceiveFatalDamage", 2),
                                    Hook_ReceiveFatalDamage, ...);
            Orig_LoseCrown    = (t_LoseCrown)       InstallHook(
                                    ResolveMethod("Player", "LoseCrown", 3),
                                    Hook_LoseCrown, ...);
            Orig_HandleCrownLoss = (t_HandleCrownLoss) InstallHook(
                                    ResolveMethod("Enemy", "HandleCrownLoss", 3),
                                    Hook_HandleCrownLoss, ...);

            // 5) 主循环：hook 一个每帧调用的函数（如 Player.Update 或
            //    UnityEngine.Time.get_deltaTime 的调用点），或起自己的线程轮询
            StartTickLoop();
            return 0;
        }, nullptr, 0, nullptr);
    }
    return TRUE;
}
```

> **强烈建议**：**不要**把 dump 里的 `p0` 当绝对地址硬编码。ASLR 会让每次运行的基址不同。
> 正确做法是用 IL2CPP 导出 API 按名字拿方法指针：
> ```c
> void* klass = il2cpp_class_from_name(image, "", "Player");
> const MethodInfo* mi = il2cpp_class_get_method_from_name(klass, "LoseCrown", 3);
> void* fn = mi->methodPointer;   // ← 这就是运行时真实地址
> ```
> dump 里的 `p0` 用于**校验**（确认拿到的是同一个方法）和**人工定位**，不是最终地址。

---

## 7. 已知限制与风险

| # | 限制 | 影响 | 应对 |
|---|---|---|---|
| 1 | `Doable\`1` 泛型字段偏移未解析（`size=0x0`, 全 `off=0x0000`） | 无法用硬编码偏移访问 `_currentActors` / `jobIsOffGround` 等 | 用 IL2CPP API 按名取字段，或绕开不用 |
| 2 | 静态字段 `F S off=...` 是**类静态块内偏移**，不是绝对地址 | `Wall.InvunerableWalls`、`ConstructionBuildingComponent.AllAutoBuild`、`WorkableTree.AutoChopTrees` 不能硬编码 | 用 `il2cpp_class_get_static_field_data` 拿块基址再加偏移 |
| 3 | dump 的 `p0` 是导出时的绝对地址，ASLR 下失效 | 硬编码地址必崩 | 用 `il2cpp_class_get_method_from_name` 运行时解析；`p0` 仅作校验 |
| 4 | 多方法共用同一 `p0`（如 `00007FFF7A2F3CC0`） | hook 共享 stub 会影响所有共用者 | 避免 hook 共享 stub；改 hook 唯一的、`argc>0` 的方法 |
| 5 | `Tower` / `WorkableBuilding` / `FireTower` / 农田等**没有 `Damageable` 字段** | 无法用固定偏移拿到血量组件 | 运行时 `GetComponentInChildren<Damageable>()`，或统一在 `Damageable.ReceiveDamage` 层拦截 |
| 6 | `WorkableTree` 用 `strength`(0x40) 而非 HP | 树的保护方案与建筑不同 | 单独写 `strength` |
| 7 | `GrabCrownRoutine`/`PickupCrown` 返回 `IEnumerator` | 简单 `return` 会导致调用方空指针崩溃 | 不 hook 它们，改用 `LoseCrown` + `HandleCrownLoss` + 压 `_heldByFoe` |
| 8 | 游戏有网络同步（大量 `Send*RPC`/`Recv*RPC`、`CRPCHeader`） | 联机时本地改内存可能被服务器/主机覆盖或判定不同步 | 训练器定位为**单机**；联机场景需同时拦 `Recv*` 方法 |
| 9 | 每帧 `FindAllObjectsOfType` 开销大 | 帧率下降 | 缓存对象列表，只在场景切换时重建；或 hook 对象的 `Awake`/`OnEnable` 增量登记 |
| 10 | `Wall.get_IsImmune` 当前恒返回 false（与多个方法共用 stub） | 若 hook 该 stub 会误伤其它方法 | 不 hook；如需免疫走 `_isInvulnerable` 或 `InvunerableWalls` |

---

## 8. 快速索引（地址速查）

### 建造
| 用途 | 目标 | 地址/偏移 |
|---|---|---|
| 强制完工 | `ConstructionBuildingComponent.ForceComplete` argc=0 | `00007FFF7A7EF070` |
| 完工（带参） | `ConstructionBuildingComponent.ConstructionComplete` argc=1 | `00007FFF7A7EEA70` |
| 设进度 | `ConstructionBuildingComponent.UpdateBuildPoints` argc=1 | `00007FFF7A7EFF80` |
| 覆盖进度 | `ConstructionBuildingComponent.OverrideCurrentBuildPoints` argc=1 | `00007FFF7A7EF710` |
| 加进度 | `ConstructionBuildingComponent.IncrementBuild` argc=1 | `00007FFF7A7EF130` |
| 还需工作? | `ConstructionBuildingComponent.get_NeedsMoreWork` argc=0 | `00007FFF7A7F03E0` |
| 当前进度 | `_currentBuildPoints` | **0x64** (float) |
| 目标进度 | `_buildPoints` | **0x30** (int) |
| 已开工 | `_hasStarted` | **0x78** (bool) |
| 全局自动建造 | `<AllAutoBuild>` 静态 | S 0x00 |
| 建筑完成 | `WorkableBuilding.CompleteConstruction` argc=0 | `00007FFF7A675B10` |
| 大型工程完工 | `AssembledBuildProject.SpawnFinishedObject` argc=0 | `00007FFF7A6FE880` |

### 无敌
| 用途 | 目标 | 地址/偏移 |
|---|---|---|
| 主伤害入口 | `Damageable.ReceiveDamage` argc=3 | `00007FFF7A811780` |
| 致命伤 | `Damageable.ReceiveFatalDamage` argc=2 | `00007FFF7A811F80` |
| 无敌位 | `Damageable._isInvulnerable` | **0x100** (bool) |
| 血量 | `Damageable._hitPoints` | **0x104** (int) |
| 归属玩家 | `Damageable._playerRef` | **0xF0** (`Player*`) |
| 用 HP 系统 | `Damageable.useHitPoints` | **0x58** (bool) |
| 初始无敌 | `Damageable.isInvulnerableInitially` | **0x9C** (bool) |
| 无敌时忽略 | `Damageable.ignoredWhenInvulnerable` | **0x9B** (bool) |
| 写无敌 | `Damageable.set_invulnerable` argc=1 | `00007FFF7A812FD0` |
| 城墙血组件 | `Wall._damageable` | **0x78** (`Damageable*`) |
| 城墙建造载体 | `Wall._workableBuilding` | **0x98** (`WorkableBuilding*`) |
| 建造组件 | `WorkableBuilding._constructionBuilding` | **0x50** |
| 城墙全局无敌 | `Wall.<InvunerableWalls>` 静态 | S 0x00 |
| 树耐久 | `WorkableTree.strength` | **0x40** (int) |
| 自动砍树 | `WorkableTree.AutoChopTrees` 静态 | S 0x08 |

### 王冠
| 用途 | 目标 | 地址/偏移 |
|---|---|---|
| **丢冠总入口** | `Player.LoseCrown` argc=3 | **`00007FFF7A4E5B10`** |
| 敌人夺冠 | `Enemy.HandleCrownLoss` argc=3 | **`00007FFF7A357F60`** |
| 骑士抓冠 | `GreedKnight.GrabCrownRoutine` argc=1 | `00007FFF7A3C66E0` |
| 偷冠者捡冠 | `CrownStealer.PickupCrown` argc=1 | `00007FFF7A7F4ED0` |
| 巨魔捡冠 | `Troll.PickupCrown` argc=1 | `00007FFF7A60E750` |
| 王冠丢失 | `Crown.OnLose` argc=0 | `00007FFF7A7F7BE0` |
| 指派掉落冠 | `Crown.AssignDroppedCrown` argc=1 | `00007FFF7A7F6BB0` |
| 游戏失败 | `Game.Lose` argc=1 | `00007FFF7A389480` |
| 有冠? | `Player.get_hasCrown` / `set_hasCrown` | `00007FFF7A4EF5B0` / `00007FFF7A4EF9F0` |
| 强制冠状态 | `Player.ForceCrownState` argc=2 | `00007FFF7A4E4A90` |
| 君主 Damageable | `Player.<damageable>` | **0x0100** |
| 君主冠类型 | `Player._crownPair` | **0x0264** (`CrownPair`) |
| ├ CrownType | （`_crownPair + 0x10`） | **0x0274** |
| └ EventCrownType | （`_crownPair + 0x14`） | **0x0278** |
| 王冠被敌持有 | `Crown._heldByFoe` | **0x78** (bool) |
| 王冠悬浮在地 | `Crown._hovering` | **0xA8** (bool) |
| 王冠类型 | `Crown.crownType` | **0x50** (`CrownType`) |
| 王冠归属 | `Crown.<owningPlayer>` | **0x70** (`Player*`) |
| 地上冠 P1 | `Kingdom.droppedP1Crown` | **0xE0** (`Crown*`) |
| 地上冠 P2 | `Kingdom.droppedP2Crown` | **0xE8** (`Crown*`) |

---

*报告生成：基于 `E:\AI\KTC-Trainer\dump\dump.txt` 与 `index.txt` 的只读分析。所有偏移与地址均引自 dump 原文，行号已在文中标注。*
