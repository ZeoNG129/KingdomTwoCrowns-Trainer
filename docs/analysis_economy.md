# Kingdom Two Crowns 2.4.2 (Unity 6000.0.66f2 / IL2CPP / x64) 经济系统逆向分析报告

> 目标：为注入式原生 DLL（直接读写内存 + inline hook 原生函数指针）提供**可直接编码**的金币/宝石结构、偏移、地址与 hook 方案。
>
> 数据来源（只读）：`E:\AI\KTC-Trainer\dump\dump.txt`（38MB 类元数据 dump）、`E:\AI\KTC-Trainer\dump\index.txt`。
> 额外验证：直接反汇编本机 `E:\steam\steamapps\common\Kingdom Two Crowns\GameAssembly.dll`（70,887,936 字节，PE32+ / machine=0x8664）。
> 本报告**没有修改** dump / index / 游戏目录中的任何文件。

---

## 0. 结论速览（TL;DR）

| 需求 | 结论 | 关键地址（RVA，基址见 §1） |
|---|---|---|
| **+10 金币** | 调用 `Wallet.AddCurrency(CurrencyType=0, 10)`；失败/被网络门控时改用 `Wallet.FastSetCurrency(0, 当前值+10)` | `AddCurrency` RVA `0x80FD90`；`FastSetCurrency` RVA `0x810AD0`；`set_Coins` RVA `0x8147B0` |
| **+10 宝石** | 同上，CurrencyType=1 | `set_Gems` RVA `0x814800`；`AddCurrency`/`FastSetCurrency` 同上 |
| **资源不消耗** | **把 `Wallet.InfiniteMoney` 静态字段置 1 即可**（这是游戏自带 debug 开关）。全二进制只有 `Wallet.RemoveCurrencyAfterMoving` 读它 | 静态字段：Wallet 静态块 `+0x00`；`set_InfiniteMoney` RVA `0x814810`；被读函数 RVA `0x812550` |
| 金币/宝石存储 | `Wallet + 0x60` → `CurrencyMap<int>` → `+0x30` → `Dictionary<CurrencyType,int>` → `+0x18` `_entries[]`，Entry 步长 `0x10`，值在 `+0x0C` | 见 §3 |
| 枚举值 | **Coins = 0，Gems = 1（已由机器码证明）**；Crown/Skulls/Shades/Merchandise/Candle/Egg = 2..7 | 见 §4 |
| Wallet 实例 | `Managers.Inst.kingdom.playerOne/playerTwo/_activePlayers[i].wallet`（`Player+0xE8`） | 见 §7 |

**一句话**：把 `Wallet` 的静态字段 `<InfiniteMoney>k__BackingField`（静态数据块 +0x00）写成 `1`，「买东西不掉钱」就成立了，而且这是游戏自己 debug 菜单里 `BuildSettlement` / `AutoBuild` 用的同一机制。

---

## 1. 地址换算：dump 里的 `p0` 是绝对地址，先还原成 RVA

dump 中的 `p0=00007FFF7A662AC0(GA)` 是**当时进程里 GameAssembly.dll 的绝对地址**（ASLR 后），不能跨进程硬编码。

我通过把 dump 里全部 **165,105 个 `p0`** 与 `GameAssembly.dll` 文件内保存的重定位指针表做匹配，唯一确定了 dump 采集时模块基址：

```
dump-time GameAssembly.dll base = 0x00007FFF79E50000
```

匹配结果：`hits 502/502  base=0x7FFF79E50000`（其余候选基址命中率 ≤ 9%）。校验：所有 `p0` 都落在该映像的 `.text` / `il2cpp` 代码节内（PE 节表见下）。

**换算公式：`RVA = p0 - 0x7FFF79E50000`；运行时地址 = `GetModuleHandleA("GameAssembly.dll") + RVA`。**

PE 节表（GameAssembly.dll，ImageBase=0x180000000，SizeOfImage=0x4664000）：

| 节 | RVA | 虚拟大小 | 文件偏移 |
|---|---|---|---|
| `.text` | `0x00001000` | `0x004A1D04` | `0x00000400` |
| `il2cpp` | `0x004A3000` | `0x02C0C6E1` | `0x004A2200` |
| `.rdata` | `0x030B0000` | `0x00B5E010` | `0x030AEA00` |
| `.data` | `0x03C0F000` | `0x00684270` | `0x03C0CC00` |
| `.pdata` | `0x04294000` | `0x0029D684` | `0x03FCCE00` |
| `.reloc` | `0x04533000` | `0x00130100` | `0x0426A800` |

> 全部托管方法代码在自定义节 **`il2cpp`**（Unity 6 的新布局），`.text` 只放 IL2CPP 运行时本体。

**如果不想依赖 RVA**（例如换版本），可以在运行时算“dump 偏移量”：
用 IL2CPP API 解析任意一个 dump 里也有的方法（推荐 `Wallet.SetCurrency`），
`delta = dump_p0 - runtime_methodPointer`，之后所有 dump 地址都可用 `dump_addr - delta` 还原。

---

## 2. `Wallet` 类完整结构（size=0xA8）

dump 原始行（`dump.txt:40987-41011`）：

```
[CLASS] Wallet  :  UnityEngine.MonoBehaviour   size=0xA8 flags=0x100101 [MonoBehaviour]
    F S off=0x0000 System.Single                          MAX_PAY_COINS_FORCE
    F S off=0x0000 System.Single                          PLAYER_COIN_SPEED_MODIFIER
    F i off=0x0020 System.Action<DroppableCurrency>       OnPickupCurrency
    F i off=0x0028 System.Int32                           payTaxesAbove
    F i off=0x002C UnityEngine.Vector3                    position
    F i off=0x0038 CurrencyMap<AudioEmitter>              currencyPickupSound
    F i off=0x0040 CurrencyMap<AudioEmitter>              currencyDropSound
    F i off=0x0048 AudioEmitter                           payTaxesSound
    F i off=0x0050 System.Boolean                         onlyGrabPlayerCoins
    F i off=0x0051 System.Boolean                         showCurrencyBag
    F i off=0x0054 System.Int32                           TotalCapacity
    F i off=0x0058 CurrencyMap<System.Boolean>            _allowedCurrencies
    F i off=0x0060 CurrencyMap<System.Int32>              _currencyAmount
    F i off=0x0068 IPickupAttributeProvider               _pickupAttributesProvider
    F i off=0x0070 CurrencyBag                            _currencyBag
    F i off=0x0078 Player                                 _playerRef
    F i off=0x0080 Mover                                  _mover
    F i off=0x0088 System.Boolean                         _payingTaxes
    F i off=0x0090 CRPCHeader                             parentHeaderRef
    F i off=0x0098 System.Int32                           suckCurrencyRPCIndex
    F i off=0x009C System.Int32                           spawnLossPrefabRPCIndex
    F S off=0x0000 System.Boolean                         <InfiniteMoney>k__BackingField
    F S off=0x0001 System.Boolean                         <DebugDisableTaxes>k__BackingField
    F i off=0x00A0 System.Boolean                         AllowCollect
```

### 2.1 实例字段（可直接读写）

| 偏移 | 类型 | 名称 | 用途 |
|---|---|---|---|
| `0x20` | `Action<DroppableCurrency>` | `OnPickupCurrency` | 捡钱回调 |
| `0x28` | int | `payTaxesAbove` | 超过多少枚触发“交税”协程 |
| `0x2C` | Vector3 | `position` | 钱包世界坐标 |
| `0x38` / `0x40` | `CurrencyMap<AudioEmitter>` | `currencyPickupSound` / `currencyDropSound` | 每种货币的音效 |
| `0x48` | `AudioEmitter` | `payTaxesSound` | 交税音效 |
| `0x50` | bool | `onlyGrabPlayerCoins` | 是否只捡自己的钱 |
| `0x51` | bool | `showCurrencyBag` | 是否显示钱袋 |
| **`0x54`** | **int** | **`TotalCapacity`** | **SetCurrency 的上限钳制值（重要，见 §8）** |
| `0x58` | `CurrencyMap<bool>` | `_allowedCurrencies` | 每种货币能否拾取 |
| **`0x60`** | **`CurrencyMap<int>`** | **`_currencyAmount`** | **★ 金币/宝石真正的存储容器（引用类型，8 字节指针）** |
| `0x68` | `IPickupAttributeProvider` | `_pickupAttributesProvider` | 拾取属性来源 |
| `0x70` | `CurrencyBag` | `_currencyBag` | 钱袋视觉对象（`InfiniteMoney` 分支会检查它是否为 null） |
| `0x78` | `Player` | `_playerRef` | 反向引用玩家 |
| `0x80` | `Mover` | `_mover` | |
| `0x88` | bool | `_payingTaxes` | |
| `0x90` | `CRPCHeader` | `parentHeaderRef` | 联机 RPC |
| `0x98` / `0x9C` | int | `suckCurrencyRPCIndex` / `spawnLossPrefabRPCIndex` | RPC 索引 |
| `0xA0` | bool | `AllowCollect` | |

> 注意：`MAX_PAY_COINS_FORCE`、`PLAYER_COIN_SPEED_MODIFIER` 在 dump 里 off=0x0000，是 **const/编译期常量**（无静态存储）；同类的还有 `Player.directionTimeout`、`Player.directionFlipReqCount`。真正的静态字段只有两个 bool（见下）。

### 2.2 静态字段（★ 关键）

dump 原始行（`dump.txt:41009-41010`）：

```
    F S off=0x0000 System.Boolean                         <InfiniteMoney>k__BackingField
    F S off=0x0001 System.Boolean                         <DebugDisableTaxes>k__BackingField
```

| 静态块偏移 | 名称 | 类型 | 属性访问器 |
|---|---|---|---|
| **`+0x00`** | `<InfiniteMoney>k__BackingField` | bool | `get_InfiniteMoney` RVA `0x814570` / `set_InfiniteMoney` RVA `0x814810`（**static**） |
| `+0x01` | `<DebugDisableTaxes>k__BackingField` | bool | `get_` RVA `0x8143E0` / `set_` RVA `0x8147C0`（**static**） |

两个偏移已由机器码确认（`set_InfiniteMoney`：`mov rdx,[rax+0xB8]` → `mov byte ptr [rdx], bl`；`set_DebugDisableTaxes`：`mov byte ptr [rdx+1], bl`），且 `flags=0x896` 中的 `0x10 = MethodAttributes.Static`，说明它们是**静态属性**。

### 2.3 方法清单（含 RVA）

完整方法表见 **附录 A.1**（68 个方法）。最常用的：

| 方法 | argc | RVA | 说明 |
|---|---|---|---|
| `GetCurrency(CurrencyType)` | 1 | `0x810D60` | 读：`return _currencyAmount[type]` |
| `SetCurrency(CurrencyType,int)` | 2 | `0x812AC0` | 写：带容量钳制 / 网络门控 / 钱袋刷新 |
| `FastSetCurrency(CurrencyType,int)` | 2 | `0x810AD0` | 写：直接写字典 + 更新统计（**无门控、无钳制**） |
| `AddCurrency(CurrencyType,int)` | 2 | `0x80FD90` | `SetCurrency(t, GetCurrency(t)+max(amount,0))` |
| `RemoveCurrency(CurrencyType,int)` | 2 | `0x812640` | `SetCurrency(t, GetCurrency(t)-max(amount,0))` |
| `RemoveCurrencyAfterMoving(CurrencyType)` | 1 | `0x812550` | **★ 花钱扣款的实际落点，`InfiniteMoney` 在这里生效** |
| `MoveCurrencyObject(CurrencyType,Transform,Vector3,bool)` | 4 | `0x811080` | 生成“飞向建筑”的硬币对象 |
| `MoveCurrencyObject(CurrencyType,Vector3,bool)` | 3 | `0x811120` | 同上重载 |
| `PrepareCurrencyObjectBeforeMoving(CurrencyType)` | 1 | `0x8120D0` | |
| `HasCurrency(CurrencyType)` | 1 | `0x8103A0` | `return GetCurrency(t) > 0`（已反汇编确认） |
| `get_Coins()` / `set_Coins(int)` | 0/1 | `0x814390` / `0x8147B0` | 等价 `GetCurrency(0)` / `SetCurrency(0,v)`（已确认） |
| `get_Gems()` / `set_Gems(int)` | 0/1 | `0x814420` / `0x814800` | 等价 `GetCurrency(1)` / `SetCurrency(1,v)`（已确认） |
| `get_TotalCurrency()` | 0 | `0x8145B0` | 所有货币求和 |
| `UpdateCurrencyStats(CurrencyType)` | 1 | `0x813F70` | 写 Stats（成就/统计） |
| `ShowCurrencyBag()` | 0 | `0x812D50` | 刷新钱袋显示 |
| `DropCurrency(...)` | 3/7 | `0x810690` / `0x810720` | 掉钱 |
| `Reset()` | 0 | `0x8126C0` | 清空（调 `CurrencyMap<int>.ResetValues`） |
| `TryToPayTaxes(Collider2D)` | 1 | `0x813DB0` | 自动交税入口 |
| `SuckCurrency(DroppableCurrency,bool)` | 2 | `0x8133D0` | 吸钱入袋 |

### 2.4 `SetCurrency` 的真实行为（反汇编 0x812AC0）

```c
void Wallet::SetCurrency(CurrencyType type, int amount) {
    if (!NetworkBigBoss::HasWorldAuth() && !NetworkBigBoss::HasClientCaughtUp())
        return;                                   // ← 网络门控！单人局一般 HasWorldAuth==true
    if (amount < 0) amount = 0;
    if (amount > this->TotalCapacity /*+0x54*/) amount = this->TotalCapacity;   // ← 容量钳制
    this->_currencyAmount->set_Item(type, amount);          // 直接写字典
    if (this->_playerRef /*+0x78*/ != null && this->_playerRef->hasLocalAuthority /*+0x1F8*/) {
        CurrencyConfig cfg;
        if (CurrencyManager::TryGetData(type, &cfg)) {
            if (cfg.HeldStat /*+0x40*/ != 0) Stats::SetStat(...);
            if (cfg.ShouldShowCurrencyBag /*+0x4D*/) this->ShowCurrencyBag();
        }
    }
    /* 之后是 HasClientCaughtUp 相关的联机同步分支 */
}
```

关键点：
1. `SetCurrency` **会钳制到 `TotalCapacity`（Wallet+0x54）**。想超过上限只能走 `FastSetCurrency` 或直接写字典。
2. `SetCurrency` 受 `NetworkBigBoss.HasWorldAuth/HasClientCaughtUp` 门控；`FastSetCurrency` **没有**这个门控。
3. `AddCurrency` / `RemoveCurrency` / `RemoveCurrencyAfterMoving` / `DropCurrency` 全部是 **尾调用跳进 `SetCurrency`**（`jmp 0x812AC0`），所以 `SetCurrency` 是所有“正常扣款/加钱”的唯一汇聚点。

---

## 3. 金币/宝石的实际存储与完整指针链

### 3.1 `CurrencyMap<T>` 类（dump.txt:8587-8608）

```
[CLASS] CurrencyMap`1  :  System.Object   size=0x0 flags=0x102001
    F i off=0x0000 T                                      Coins
    F i off=0x0000 T                                      Gems
    F i off=0x0000 T                                      Crown
    F i off=0x0000 T                                      Skulls
    F i off=0x0000 T                                      Shades
    F i off=0x0000 T                                      Merchandise
    F i off=0x0000 T                                      Candle
    F i off=0x0000 T                                      Egg
    F i off=0x0000 System.Collections.Generic.Dictionary<CurrencyType,T> _map
    M get_Count / .ctor / ResetValues / Clone / get_Item / set_Item / Contains /
      GetEnumerator x2 / OnBeforeSerialize / OnAfterDeserialize / GetMap
```

⚠️ **注意**：这是**开放泛型定义**（`size=0x0`，字段类型是 `T`），IL2CPP 不会为泛型定义计算实例偏移，所以 dump 里 8 个字段全是 `off=0x0000`。真实布局必须靠**反汇编封闭泛型实例**得到。

`CurrencyMap<int>` 的 `get_Item` / `set_Item` 是本报告从二进制里定位到的具体实现（它们不在 dump 的 `p0` 表里，因为 dump 只登记了共享泛型版本）：

```
CurrencyMap<int>.get_Item   RVA 0x14929C0
  0x14929C4   mov rcx, qword ptr [rcx + 0x30]      ; ★ this->_map 在 CurrencyMap<int> 的 +0x30
  0x14929C8   test rcx, rcx / je -> NullReference
  0x14929E3   jmp Dictionary<CurrencyType,int>.get_Item
CurrencyMap<int>.set_Item   RVA 0x1492B80
  0x1492B84   mov rcx, qword ptr [rcx + 0x30]      ; ★ 同一个 _map 偏移
  0x1492BA0   jmp Dictionary<CurrencyType,int>.set_Item
```

**由此确定 `CurrencyMap<int>` 实例布局**（= 0x10 对象头 + 8 个 `int` 序列化镜像字段 + 字典指针）：

| 偏移 | 字段 | 说明 |
|---|---|---|
| `0x10` | `Coins` (int) | 仅供 Unity 序列化（`ISerializationCallbackReceiver`） |
| `0x14` | `Gems` (int) | 同上 |
| `0x18` / `0x1C` / `0x20` / `0x24` / `0x28` / `0x2C` | `Crown` / `Skulls` / `Shades` / `Merchandise` / `Candle` / `Egg` | 同上 |
| **`0x30`** | **`_map` `Dictionary<CurrencyType,int>*`** | **★ 运行时唯一权威存储（已由 get_Item/set_Item 证明）** |

> 判定依据：`Wallet.GetCurrency` 的实现是 `return _currencyAmount->get_Item(type)`（RVA 0x810D60 尾部 `jmp 0x14929C0`），而 `get_Item` 读的是 `+0x30` 的字典。所以 **读写必须走字典**；`Coins/Gems/...` 那 8 个字段只是给 Unity 序列化用的镜像，运行时游戏自己都不读它们。

### 3.2 `Dictionary<CurrencyType,int>` 布局（dump.txt:78117-78127 + Entry 78037-78041）

```
[CLASS] System.Collections.Generic.Dictionary`2  :  System.Object   size=0x0
    F i off=0x0000 System.Int32[]                         _buckets
    F i off=0x0000 System.Collections.Generic.Dictionary.Entry<TKey,TValue>[] _entries
    F i off=0x0000 System.Int32                           _count
    F i off=0x0000 System.Int32                           _freeList
    F i off=0x0000 System.Int32                           _freeCount
    F i off=0x0000 System.Int32                           _version
    F i off=0x0000 System.Collections.Generic.IEqualityComparer<TKey> _comparer
    F i off=0x0000 System.Collections.Generic.Dictionary.KeyCollection<TKey,TValue> _keys
    F i off=0x0000 System.Collections.Generic.Dictionary.ValueCollection<TKey,TValue> _values
    F i off=0x0000 System.Object                          _syncRoot
[CLASS] Entry  :  System.ValueType   size=0x0
    F i off=0x0000 System.Int32                           hashCode
    F i off=0x0000 System.Int32                           next
    F i off=0x0000 TKey                                   key
    F i off=0x0000 TValue                                 value
```

`Dictionary<CurrencyType,int>.get_Item`（RVA `0x1539890`）的机器码给出了**精确的运行时布局**：

```
0x15398FB   mov rcx, qword ptr [rbx + 0x18]     ; _entries 数组（+0x18）
0x1539904   cmp eax, dword ptr [rcx + 0x18]     ; 数组长度在 +0x18
0x1539915   add rax, rax                        ; index*2
0x1539918   mov eax, dword ptr [rcx + rax*8 + 0x2c]   ; 值 = entries + 0x20 + index*0x10 + 0x0C
```

| 偏移 | 字段 | 类型 |
|---|---|---|
| `0x10` | `_buckets` | `int[]` |
| **`0x18`** | **`_entries`** | `Entry[]` |
| `0x20` | `_count` | int |
| `0x24` | `_freeList` | int |
| `0x28` | `_freeCount` | int |
| `0x2C` | `_version` | int |
| `0x30` | `_comparer` | `IEqualityComparer<CurrencyType>` |
| `0x38` / `0x40` / `0x48` | `_keys` / `_values` / `_syncRoot` | 对象引用 |

托管数组（`Entry[]`，值类型数组 → 元素内联）：

| 偏移 | 含义 |
|---|---|
| `0x10` | bounds |
| `0x18` | `max_length`（元素个数） |
| `0x20` | 第 0 个元素起始 |

`Entry`（步长 **0x10**）：

| Entry 内偏移 | 绝对偏移（第 i 个） | 字段 |
|---|---|---|
| `+0x00` | `0x20 + i*0x10` | `hashCode`（int，**< 0 表示空槽**） |
| `+0x04` | `+0x04` | `next`（int） |
| `+0x08` | `+0x08` | `key` = `CurrencyType`（int） |
| **`+0x0C`** | **`+0x0C`** | **`value` = 数量（int）** |

### 3.3 给定 Wallet 指针读写金币的完整链

```c
// wallet = Wallet* 实例
CurrencyMap_int* map  = *(CurrencyMap_int**)((uint8_t*)wallet + 0x60);  // _currencyAmount
void*            dict = *(void**)((uint8_t*)map + 0x30);                // _map
void*            ent  = *(void**)((uint8_t*)dict + 0x18);               // _entries
int32_t          cnt  = *(int32_t*)((uint8_t*)dict + 0x20);             // _count
for (int i = 0; i < cnt; ++i) {
    uint8_t* e = (uint8_t*)ent + 0x20 + i * 0x10;
    int32_t hash = *(int32_t*)(e + 0x00);
    int32_t key  = *(int32_t*)(e + 0x08);
    if (hash < 0) continue;                    // 空槽
    if (key == 0 /*Coins*/)  coins = *(int32_t*)(e + 0x0C);
    if (key == 1 /*Gems*/)   gems  = *(int32_t*)(e + 0x0C);
}
```

**写入**：直接改 `*(int32_t*)(e + 0x0C) = newValue;` 即可（key 不变，无需动 `_buckets` / `hashCode`）。
代价：不会触发 `UpdateCurrencyStats` / `ShowCurrencyBag` / 联机同步，HUD 数字通常在下一帧由 UI 自行读取刷新。

### 3.4 对比：用游戏自己的方法（推荐，最稳）

| 目的 | 调用 | RVA | 备注 |
|---|---|---|---|
| 读金币 | `wallet->get_Coins()` | `0x814390` | 内部 = `GetCurrency(0)` |
| 读宝石 | `wallet->get_Gems()` | `0x814420` | 内部 = `GetCurrency(1)` |
| 读任意 | `wallet->GetCurrency(type)` | `0x810D60` | 需要一个 `CurrencyType` |
| 写金币 | `wallet->set_Coins(v)` | `0x8147B0` | 内部 = `SetCurrency(0, v)` |
| 写宝石 | `wallet->set_Gems(v)` | `0x814800` | 内部 = `SetCurrency(1, v)` |
| 写任意（推荐） | `wallet->SetCurrency(type, v)` | `0x812AC0` | 有钳制+门控+UI+同步 |
| 写任意（静默） | `wallet->FastSetCurrency(type, v)` | `0x810AD0` | 无门控、无钳制，直接写字典 + 统计 |
| 加钱 | `wallet->AddCurrency(type, n)` | `0x80FD90` | 内部 = `SetCurrency(type, cur+n)` |

> 这些都是**非泛型具体方法**，`p0 == p1`（dump 里两个地址相同），可以按标准 x64 fastcall 直接调用，**不需要额外传 `MethodInfo*`**。
> 反过来，泛型方法（如 dump 里 `CurrencyMap`1.get_Item` 的 `p0=0x7FFF7A235DD0`）是共享泛型桩，调用时必须多传一个 `MethodInfo*` 参数——**不要直接调它们**。

---

## 4. `CurrencyType` 枚举的数值含义

dump 里看不到数值（`dump.txt:8610-8619`，枚举成员全是 `F S off=0x0000`）：

```
[CLASS] CurrencyType  :  System.Enum   size=0x14 flags=0x101
    F i off=0x0010 System.Int32                           value__
    F S off=0x0000 CurrencyType                           Coins
    F S off=0x0000 CurrencyType                           Gems
    F S off=0x0000 CurrencyType                           Crown
    F S off=0x0000 CurrencyType                           Skulls
    F S off=0x0000 CurrencyType                           Shades
    F S off=0x0000 CurrencyType                           Merchandise
    F S off=0x0000 CurrencyType                           Candle
    F S off=0x0000 CurrencyType                           Egg
```

**但我用机器码直接证明前两个值**：

```
Wallet.get_Coins  (RVA 0x814390):  ... xor edx,edx ; jmp CurrencyMap<int>.get_Item   -> Coins = 0
Wallet.set_Coins  (RVA 0x8147B0):  ... xor edx,edx ; jmp Wallet.SetCurrency         -> Coins = 0
Wallet.get_Gems   (RVA 0x814420):  ... mov edx,1   ; jmp CurrencyMap<int>.get_Item   -> Gems  = 1
Wallet.set_Gems   (RVA 0x814800):  ... lea edx,[r9+1] ; jmp Wallet.SetCurrency       -> Gems  = 1
```

| 值 | 名称 | 证据强度 |
|---|---|---|
| **0** | `Coins` | ★★★ 机器码证明 |
| **1** | `Gems` | ★★★ 机器码证明 |
| 2 | `Crown` | ★★ 元数据声明顺序 + `CurrencyMap<T>` 字段顺序一致 |
| 3 | `Skulls` | ★★ 同上 |
| 4 | `Shades` | ★★ 同上 |
| 5 | `Merchandise` | ★★ 同上 |
| 6 | `Candle` | ★★ 同上 |
| 7 | `Egg` | ★★ 同上 |

**运行时校验方法**（若你想 100% 确认 2..7）：枚举常量保存在枚举类的静态数据块里，用 IL2CPP API 遍历即可：

```c
Il2CppClass* e = il2cpp_class_from_name(image, "", "CurrencyType");
void* iter = NULL;
const FieldInfo* f;
while ((f = il2cpp_class_get_fields(e, &iter))) {          // 只取静态字段
    if (!(il2cpp_field_get_flags(f) & 0x10 /*STATIC*/)) continue;
    int v = 0; il2cpp_field_static_get_value(f, &v);        // 枚举常量 = int32
    printf("%s = %d\n", il2cpp_field_get_name(f), v);
}
```
（备用校验：`CurrencyManager.AllCurrencyTypes`（静态 `CurrencyType[]`，dump.txt:8558）的数组下标顺序与枚举顺序一致。）

---

## 5. `InfiniteMoney` / `DebugDisableTaxes` 到底控制什么（★ 核心结论）

### 5.1 结论

| 字段 | 结论 |
|---|---|
| **`InfiniteMoney`** | **= “花钱不扣钱”**。它是**静态全局 debug 开关**，全二进制**只有一处**读取它：`Wallet.RemoveCurrencyAfterMoving`。置 1 后，任何通过“钱飞向建筑/目标”的付款都不会扣钱，**这正是“资源不消耗且仍能正常购买建造”**。 |
| `DebugDisableTaxes` | 关闭“自动交税”（Wallet 的 `TryToPayTaxes`/`PayTaxes` 协程）。**与购买无关**，不是我们要的开关。它由 debug 面板的 `taxesEnabledButton` 通过 `DebugTools.OnButtonToggleTaxes` 切换。 |

### 5.2 判定依据（不是猜的，是扫全二进制得到的）

方法：IL2CPP 里访问某类的静态字段，代码形如 `mov rax,[rip+<该类的 metadata-usage 槽>]` → `mov rax,[rax+0xB8]`（`Il2CppClass.static_fields`）→ 再按静态块偏移读写。metadata-usage 槽是**按类去重**的（`get_InfiniteMoney`、`set_InfiniteMoney`、`get/set_DebugDisableTaxes`、`RemoveCurrencyAfterMoving` 全都引用同一个槽 RVA `0x3D21980`），因此扫描该槽的**所有引用**就能穷尽“谁碰了 Wallet 的静态字段”。

扫描结果（`GameAssembly.dll` 全代码节，22 个引用点 → 去重后 9 个函数）：

| 函数 | RVA | 访问的静态偏移 | 语义 |
|---|---|---|---|
| `Wallet.get_InfiniteMoney` | `0x814570` | `+0x00` 读 | 属性 getter |
| `Wallet.set_InfiniteMoney` | `0x814810` | `+0x00` 写 | 属性 setter |
| **`Wallet.RemoveCurrencyAfterMoving`** | **`0x812550`** | **`+0x00` 读** | **★ 唯一的玩法逻辑读取点** |
| `Wallet.get_DebugDisableTaxes` | `0x8143E0` | `+0x01` 读 | 属性 getter |
| `Wallet.set_DebugDisableTaxes` | `0x8147C0` | `+0x01` 写 | 属性 setter |
| `DebugTools.OnButtonToggleTaxes` | `0x9D6F00` | `+0x01` 读+写 | debug 面板“税收”按钮 |
| `<>c.<Awake>b__175_6`（DebugTools 的 lambda） | `0x9E4950` | `+0x01` 读 | 初始化按钮显示状态 |
| `DebugTools.OnButtonBuildSettlement` | `0x9CFCA0` | `+0x00` **写 1** | **debug“一键建村”时把 InfiniteMoney 打开** |
| `DebugTools.OnButtonAutoBuildEnabled` | `0x9CFA70` | `+0x00` 写 `!autoBuild` | **debug“自动建造”开关联动 InfiniteMoney** |
| `<AutoBuilder>d__379.MoveNext` | `0x9E1680` | `+0x00` 写 | 自动建造协程里写回 |

另外：`get_InfiniteMoney` / `set_InfiniteMoney` 的**直接调用者为 0**（全代码节扫描 `E8 rel32`），说明所有读取都是被编译器内联的静态字段访问——也就是上表这一份完整清单。

> **旁证（最有说服力）**：游戏自己的 debug 工具在“自动建造（AutoBuild）”“一键建村（BuildSettlement）”时，**主动把 `InfiniteMoney` 置 1**，目的就是让建造不花钱。这直接证明了该字段的语义就是“购买不扣资源”。

### 5.3 `RemoveCurrencyAfterMoving` 反汇编（RVA 0x812550）

```asm
0x812597  mov  rax, [rip+0x350F3E2]        ; Wallet klass (metadata usage slot 0x3D21980)
0x81259E  mov  rcx, [rax+0xB8]             ; klass->static_fields
0x8125A5  cmp  byte ptr [rcx], 0           ; <InfiniteMoney>
0x8125A8  je   0x8125DE                    ; InfiniteMoney==0 -> 照常扣钱
0x8125AA  ...  mov rdi,[rbx+0x70]          ; this->_currencyBag
0x8125D0  call UnityEngine.Object::op_Equality(_currencyBag, null)
0x8125DA  test al, al
0x8125DC  je   0x812622                    ; _currencyBag != null -> 直接 return（不扣钱）
0x8125DE  ...  mov rcx,[rbx+0x60]          ; _currencyAmount
0x81260C  call CurrencyMap<int>.get_Item(type)     ; 当前数量
0x812619  lea  r8d, [rax-1]                ; amount = cur - 1
0x81261D  call Wallet.SetCurrency          ; 0x812AC0
```

等价 C#：

```csharp
public void RemoveCurrencyAfterMoving(CurrencyType type) {
    if (!InfiniteMoney || _currencyBag == null)      // 正常玩家一定有 _currencyBag
        SetCurrency(type, GetCurrency(type) - 1);
}
```

**即：`InfiniteMoney = true` 且玩家有钱袋（正常情况）时，扣款被整段跳过。**

### 5.4 它**不**影响什么（要清楚边界）

* `Wallet.RemoveCurrency`（RVA 0x812640）不受影响，它的调用者只有 3 个：
  `Wallet.OnGameLossHandler`（0x811600）、`Wallet.TransferImportantCurrencies`（0x813940）、`Player.RecvCurrencyDroppedFromDamage`（0x698960 内 0x6989C0 处）。
  → 战败丢钱、换岛转移货币、被打掉钱 **照常发生**（这是游戏设计，不是 bug）。
* 直接调用 `SetCurrency` 的路径也不受影响：`CampaignSaveData.ApplyCarryForwardCurrency`、`Player.LoseCrown`、`Player.ForceCrownState`、`Player.ResetCrownStatus`、`Player.Persistent.IBehaviour.ApplyData`、`HermesDock.PurgeShadesFromBag`、`BunnyStatue.PurgeEggsFromIsland`、`Wallet.DropCurrency`、`Wallet.SuckCurrency` 区间内的 0x8136CA 等。
* 想“连战败/被打也不掉钱”，那就得改 hook `SetCurrency`（见 §6.2），但那会破坏存档/流程一致性，**不建议**。

### 5.5 如果你不想要全局静态开关：等价 hook 方案

直接 detour `Wallet.RemoveCurrencyAfterMoving`（RVA `0x812550`）让它立即 `return`，效果与 `InfiniteMoney=true` **完全一致**，但只作用于扣款入口，不动全局状态。两者二选一即可。

---

## 6. 谁在扣钱：调用图与 hook 候选

### 6.1 实测调用图（全代码节 `E8 rel32` 扫描 + `.pdata` 归属）

```
玩家按住付款键
  Player.UpdatePayState                       RVA 0x69DBA0
    ├─ Wallet.HasCurrency(type)               RVA 0x8103A0   (判断钱够不够)
    ├─ Player.AddCurrencyToSelectedPayable    RVA 0x6925D0
    │     └─ Wallet.MoveCurrencyObject(...)   RVA 0x811080 / 0x811120
    │           ├─ Wallet.PrepareCurrencyObjectBeforeMoving  RVA 0x8120D0  (生成飞行的硬币)
    │           └─ Wallet.RemoveCurrencyAfterMoving          RVA 0x812550  ★ 扣 1 枚
    │                 └─ Wallet.SetCurrency(t, cur-1)        RVA 0x812AC0  ★ 唯一写入汇聚点
    └─ Payable.TransactionComplete            RVA 0x66B590   (交易完成 → 建筑生效)
  Payable.RecvPay                             RVA 0x66A2C0
    ├─ Payable.PerformPay                     RVA 0x669E20
    └─ Wallet.AddCurrency                     RVA 0x80FD90   (退款/返还时)
```

`RemoveCurrencyAfterMoving` 的调用者**只有** `Wallet.MoveCurrencyObject` 的两个重载（各 1 处），
`MoveCurrencyObject(4参)` 的调用者只有 `Player.AddCurrencyToSelectedPayable` 内的 0x69267A。

> 因为宝石购买走的是同一个 `Payable._currency`（`Payable+0x28`，`CurrencyType`）→ `AddCurrencyToSelectedPayable` → `MoveCurrencyObject` 路径，**所以 `InfiniteMoney` 对宝石消费同样有效**。

### 6.2 hook 候选对比

| # | 目标 | RVA | argc / 调用约定 | 效果 | 评价 |
|---|---|---|---|---|---|
| **1** | **写静态 `InfiniteMoney = 1`** | 字段 `+0x00`；setter `0x814810` | `void(bool)`，static | 购买/建造/升级/给乞丐都不扣钱；其它丢钱逻辑照旧 | **★★★ 首选**，游戏自带机制，零 hook 风险 |
| **2** | `Wallet.RemoveCurrencyAfterMoving` | `0x812550` | `void(Wallet* this, int type)` | 与 #1 等价，作用域更窄 | ★★★ 备选（不想动全局静态时） |
| 3 | `Wallet.SetCurrency` | `0x812AC0` | `void(Wallet*, int type, int amount)` | 拦掉所有“值变小”的写入（含战败/被打掉钱） | ★★ 慎用：会连带冻结 `RemoveCurrency` 路径，可能影响存档/流程 |
| 4 | `Wallet.FastSetCurrency` | `0x810AD0` | `void(Wallet*, int type, int amount)` | 只影响存档/建造返还类直接写 | ★ 不需要 |
| 5 | `Payable.PerformPay` | `0x669E20` | `void(Payable*)` | 让建筑“免费生效”但钱照扣 | ✖ 不是需求 |
| 6 | `Player.CustomCurrencyPaymentMethod` | `0x693A50` | `IPayableCurrency*(Player*, int, Transform, Vector3)` | 自定义支付 | ✖ 调用者为 0，未使用 |

**#1 / #2 的 hook 写法（“扣钱不生效但仍能成功”）**：不要拦 `SetCurrency`（会连带把钱袋/统计/联机都搞乱），
而是**让“扣款那一步”不发生**——因为付款的成功判定发生在更上层：

* `Player.UpdatePayState` 先用 `HasCurrency` 判断余额（钱还在 → 判断通过）；
* `Payable.TransactionComplete` / `RecvPay` / `PerformPay` 只认“硬币对象已送达”这个事件，**不复查余额**；
* 所以跳过 `RemoveCurrencyAfterMoving` 里的 `SetCurrency`，交易依旧完成，建筑照常建造/升级。

---

## 7. 实例获取链：怎么拿到 `Wallet*`

### 7.1 推荐链（全部有 dump 证据）

```
Managers.Inst                       ← 静态单例（Coatsink.Common.SingletonMonoBehaviour`1<Managers> 的静态字段 _Inst）
  +0x58  Kingdom   kingdom          （dump.txt:19883）
           +0x80 Player playerOne   （dump.txt:17563）
           +0x88 Player playerTwo   （dump.txt:17564）
           +0xD0 Player[] _activePlayers （dump.txt:17575）
                    └─ +0xE8 Wallet <wallet>k__BackingField  （dump.txt:26399，get_wallet RVA 0x6920F0 = `mov rax,[rcx+0xE8]; ret` 已反汇编确认）
```

dump 证据：

```
[CLASS] Managers  :  Coatsink.Common.SingletonMonoBehaviour`1   size=0x130
    F i off=0x0050 CurrencyManager                        currency
    F i off=0x0058 Kingdom                                kingdom
    F i off=0x0080 Game                                   game
    F i off=0x0120 System.Collections.Generic.List<Manager> managers
[CLASS] Coatsink.Common.SingletonMonoBehaviour`1  :  UnityEngine.MonoBehaviour   size=0x0
    F S off=0x0000 T                                      _Inst
    M get_Inst                           ret=T  argc=0 flags=0x896 (static)
[CLASS] Kingdom  :  Manager   size=0x3A8
    F i off=0x0080 Player                                 playerOne
    F i off=0x0088 Player                                 playerTwo
    F i off=0x00D0 Player[]                               _activePlayers
    M get_ActivePlayers                  ret=IEnumerable<Player> argc=0 p0=00007FFF7A3FE440
[CLASS] Player  :  UnityEngine.MonoBehaviour   size=0x290
    F i off=0x00E8 Wallet                                 <wallet>k__BackingField
    M get_wallet                         ret=Wallet  argc=0 p0=00007FFF7A4E20F0
```

取 `Managers.Inst` 的两种写法（`get_Inst` 是**共享泛型**方法，p0=0x7FFF7A235DD0 需要额外 `MethodInfo*`，**不推荐直接调**）：

```c
// 写法 A（推荐）：读静态字段 _Inst
Il2CppClass* managers = il2cpp_class_from_name(image, "", "Managers");
Il2CppClass* parent   = il2cpp_class_get_parent(managers);            // SingletonMonoBehaviour<Managers>
const FieldInfo* f    = il2cpp_class_get_field_from_name(parent, "_Inst");
void* inst = NULL; il2cpp_field_static_get_value(f, &inst);

// 写法 B：运行时解析具体方法指针再调用
const MethodInfo* mi = il2cpp_class_get_method_from_name(parent, "get_Inst", 0);
void* (*get_Inst)() = (void*(*)())mi->methodPointer;
void* inst = get_Inst();
```

### 7.2 备用/兜底：`UnityEngine.Object.FindObjectsOfType`

| 方法 | RVA | 签名 |
|---|---|---|
| `UnityEngine.Object.FindObjectsOfType(System.Type)` | `0x2C8CE00` | `Object[] (Type)` |
| `UnityEngine.Object.FindObjectsByType(Type, FindObjectsInactive, FindObjectsSortMode)` | `0x2C8CDB0` | `Object[] (Type,int,int)` |
| `UnityEngine.Object.FindObjectOfType(System.Type, bool)` | `0x2C8CD10` | `Object (Type,bool)` |

```c
Il2CppClass* walletKlass = il2cpp_class_from_name(image, "", "Wallet");
Il2CppObject* arr = FindObjectsOfType(il2cpp_class_get_type(walletKlass));  // 返回 Object[]
// arr 是托管数组：长度在 +0x18，元素从 +0x20 开始（引用类型数组 → 8 字节指针）
```

优点：完全不依赖单例/父类结构；缺点：需要 `il2cpp_class_get_type`（或 `il2cpp_type_get_object`），且只在场景加载后有效。

### 7.3 `Player` 相关字段速查

| Player 偏移 | 类型 | 名称 |
|---|---|---|
| `0xE8` | `Wallet` | `<wallet>k__BackingField` ★ |
| `0x120` | `CurrencyBag` | `_currencyBag` |
| `0x130` | `Payable` | `_completingPayable` |
| `0x170` | `Player.PayState` | `_payState`（2 = Transaction） |
| `0x178` | `List<IPayableCurrency>` | `_floatingCurrency` |
| `0x1CC` | float | `_payTimer` |
| `0x208` | int | `_walletCountIndex`（RPC 索引） |
| `0x240` | `Payable` | `<selectedPayable>k__BackingField` |
| `0x270` | bool | `autoDropCoins` |
| `0x1F8` | bool | `hasLocalAuthority`（SetCurrency 里会检查） |

---

## 8. 三个功能的精确实现

### 8.1 +10 金币

```c
Wallet* w = GetLocalWallet();
if (w) {
    w->AddCurrency(0 /*Coins*/, 10);        // RVA 0x80FD90：内部 = SetCurrency(0, cur+10)
    // 若发现数量没变（被 NetworkBigBoss 门控或超容量钳制）：
    int cur = w->GetCurrency(0);            // RVA 0x810D60
    w->FastSetCurrency(0, cur + 10);        // RVA 0x810AD0：无门控、无钳制
}
```

要点：
* `AddCurrency` 的 `amount <= 0` 会被夹成 0（`cmovle`），负数不会倒扣。
* `SetCurrency` 会把结果**钳制到 `Wallet+0x54 TotalCapacity`**；如果玩家钱袋容量小（例如只有 20），`AddCurrency` 加不上去。要突破上限就用 `FastSetCurrency`（或直接写字典 Entry）。
* `SetCurrency` 受 `NetworkBigBoss.HasWorldAuth || HasClientCaughtUp` 门控，单人局一般成立；不确定时先读回验证，再退回 `FastSetCurrency`。

### 8.2 +10 宝石

与金币完全相同，`CurrencyType = 1`：

```c
w->AddCurrency(1 /*Gems*/, 10);                    // 或
w->set_Gems(w->get_Gems() + 10);                   // RVA 0x814800 / 0x814420（等价，更直观）
```

### 8.3 资源不消耗（花钱/宝石时不减少，仍能正常购买建造）

**方案 A（首选，零 hook）**：

```c
// 1) 直接调静态 setter（最简单，RVA 0x814810）
typedef void (*SetInfiniteMoney_t)(bool);
((SetInfiniteMoney_t)(gaBase + 0x814810))(true);

// 2) 或者直接写静态字段（不依赖调用约定）
Il2CppClass* wk = il2cpp_class_from_name(image, "", "Wallet");
uint8_t* sf = *(uint8_t**)((uint8_t*)wk + 0xB8);   // Il2CppClass.static_fields（本版本 = +0xB8，已由 set_InfiniteMoney 机器码确认）
sf[0x00] = 1;                                      // <InfiniteMoney>k__BackingField
// 更稳妥（不依赖 0xB8）：用导出 API
const FieldInfo* fi = il2cpp_class_get_field_from_name(wk, "<InfiniteMoney>k__BackingField");
uint8_t one = 1; il2cpp_field_static_set_value(fi, &one);
```

**方案 B（等价 hook，不动全局状态）**：

```c
// detour Wallet.RemoveCurrencyAfterMoving (RVA 0x812550)
// 原型：void (*)(Wallet* this, int32_t currencyType)
static void __fastcall Hook_RemoveCurrencyAfterMoving(void* self, int32_t type) {
    if (g_FreeSpend) return;                 // 直接吞掉扣款
    Orig_RemoveCurrencyAfterMoving(self, type);
}
```

两种方案的效果与副作用边界见 §5.3 / §5.4。

---

## 9. 实现方案（伪 C++，按步骤）

```cpp
// ============================================================
// 0) 基础：拿到 IL2CPP 导出 + GameAssembly 基址 + 镜像
// ============================================================
HMODULE ga = GetModuleHandleA("GameAssembly.dll");          // 与 UnityPlayer 同目录
auto p_il2cpp_domain_get          = (Il2CppDomain*(*)())        GetProcAddress(ga, "il2cpp_domain_get");
auto p_il2cpp_domain_get_assemblies=(const Il2CppAssembly*(*)(Il2CppDomain*, size_t*)) GetProcAddress(ga,"il2cpp_domain_get_assemblies");
auto p_il2cpp_assembly_get_image  = (Il2CppImage*(*)(const Il2CppAssembly*)) GetProcAddress(ga,"il2cpp_assembly_get_image");
auto p_il2cpp_class_from_name     = (Il2CppClass*(*)(const Il2CppImage*, const char*, const char*)) GetProcAddress(ga,"il2cpp_class_from_name");
auto p_il2cpp_class_get_parent    = (Il2CppClass*(*)(Il2CppClass*)) GetProcAddress(ga,"il2cpp_class_get_parent");
auto p_il2cpp_class_get_field_from_name = (const FieldInfo*(*)(Il2CppClass*,const char*)) GetProcAddress(ga,"il2cpp_class_get_field_from_name");
auto p_il2cpp_field_static_get_value = (void(*)(const FieldInfo*, void*)) GetProcAddress(ga,"il2cpp_field_static_get_value");
auto p_il2cpp_field_static_set_value = (void(*)(const FieldInfo*, void*)) GetProcAddress(ga,"il2cpp_field_static_set_value");
auto p_il2cpp_class_get_method_from_name = (const MethodInfo*(*)(Il2CppClass*,const char*,int)) GetProcAddress(ga,"il2cpp_class_get_method_from_name");
auto p_il2cpp_thread_attach       = (Il2CppThread*(*)(Il2CppDomain*)) GetProcAddress(ga,"il2cpp_thread_attach");
auto p_il2cpp_class_get_type      = (Il2CppType*(*)(Il2CppClass*)) GetProcAddress(ga,"il2cpp_class_get_type");

// 注意：必须在游戏线程里操作，或先 il2cpp_thread_attach(domain)
Il2CppDomain* dom = p_il2cpp_domain_get();
p_il2cpp_thread_attach(dom);                       // 在自己的线程里必须做

size_t n = 0;
const Il2CppAssembly** asms = p_il2cpp_domain_get_assemblies(dom, &n);
Il2CppImage* img = nullptr;
for (size_t i = 0; i < n; ++i) {
    Il2CppImage* im = p_il2cpp_assembly_get_image(asms[i]);
    if (strcmp(il2cpp_image_get_name(im), "Assembly-CSharp.dll") == 0) { img = im; break; }
}

// ============================================================
// 1) 解析类
// ============================================================
Il2CppClass* kWallet   = p_il2cpp_class_from_name(img, "", "Wallet");
Il2CppClass* kManagers = p_il2cpp_class_from_name(img, "", "Managers");
Il2CppClass* kSingletonBase = p_il2cpp_class_get_parent(kManagers);   // SingletonMonoBehaviour<Managers>

// ============================================================
// 2) 找实例
// ============================================================
// 2a) Managers 单例
void* managers = nullptr;
{
    const FieldInfo* f = p_il2cpp_class_get_field_from_name(kSingletonBase, "_Inst");
    p_il2cpp_field_static_get_value(f, &managers);          // 静态字段 = 8 字节指针
}
if (!managers) return;                                      // 未进关卡

// 2b) Kingdom（Managers+0x58）→ Player[]（Kingdom+0xD0 / playerOne@0x80）
void* kingdom = *(void**)((uint8_t*)managers + 0x58);
void* player  = nullptr;
if (kingdom) {
    void* arr = *(void**)((uint8_t*)kingdom + 0xD0);        // _activePlayers : Player[]
    int   len = arr ? *(int*)((uint8_t*)arr + 0x18) : 0;    // 数组长度
    if (len > 0) player = *(void**)((uint8_t*)arr + 0x20);  // 第 0 个元素
    if (!player) player = *(void**)((uint8_t*)kingdom + 0x80);  // playerOne 兜底
}
if (!player) return;

// 2c) Wallet（Player+0xE8）
void* wallet = *(void**)((uint8_t*)player + 0xE8);

// 备选：FindObjectsOfType 扫描（不依赖单例链）
// void* arr = ((void*(*)(void*))（ga + 0x2C8CE00))(p_il2cpp_class_get_type(kWallet));

// ============================================================
// 3) 读 / 写（两条路，任选）
// ============================================================
// 3a) 调用游戏自己的方法（推荐；这些是非泛型具体方法，无需 MethodInfo 参数）
typedef int  (*Fn_GetCurrency)(void* self, int32_t type);                  // RVA 0x810D60
typedef void (*Fn_SetCurrency)(void* self, int32_t type, int32_t amount);  // RVA 0x812AC0
typedef void (*Fn_FastSet)(void* self, int32_t type, int32_t amount);      // RVA 0x810AD0
typedef void (*Fn_AddCurrency)(void* self, int32_t type, int32_t amount);  // RVA 0x80FD90

uint8_t* gaBase = (uint8_t*)ga;
auto GetCurrency   = (Fn_GetCurrency)(gaBase + 0x810D60);
auto SetCurrency   = (Fn_SetCurrency)(gaBase + 0x812AC0);
auto FastSetCurrency = (Fn_FastSet)  (gaBase + 0x810AD0);
auto AddCurrency   = (Fn_AddCurrency)(gaBase + 0x80FD90);

int coins = GetCurrency(wallet, 0 /*Coins*/);
int gems  = GetCurrency(wallet, 1 /*Gems*/);

AddCurrency(wallet, 0, +10);                      // +10 金币
AddCurrency(wallet, 1, +10);                      // +10 宝石
// 若被门控/钳制：FastSetCurrency(wallet, 0, coins + 10);

// 3b) 纯内存读写（静默、零副作用、无门控）
struct CurrencyMapInt { uint8_t pad[0x10]; int32_t v[8]; void* _map; /* +0x30 */ };
static int32_t ReadCurrencyRaw(void* wallet, int32_t type, bool* found) {
    auto* map  = *(uint8_t**)((uint8_t*)wallet + 0x60);
    if (!map) { *found = false; return 0; }
    void* dict = *(void**)(map + 0x30);                       // CurrencyMap<int>._map
    if (!dict) { *found = false; return 0; }
    void* ent  = *(void**)((uint8_t*)dict + 0x18);            // _entries
    int   cnt  = *(int*)((uint8_t*)dict + 0x20);              // _count
    *found = false;
    for (int i = 0; i < cnt && ent; ++i) {
        uint8_t* e = (uint8_t*)ent + 0x20 + i * 0x10;
        if (*(int*)(e + 0x00) < 0) continue;                  // 空槽
        if (*(int*)(e + 0x08) == type) { *found = true; return *(int*)(e + 0x0C); }
    }
    return 0;
}
static void WriteCurrencyRaw(void* wallet, int32_t type, int32_t value) {
    auto* map  = *(uint8_t**)((uint8_t*)wallet + 0x60);
    void* dict = map ? *(void**)(map + 0x30) : nullptr;
    void* ent  = dict ? *(void**)((uint8_t*)dict + 0x18) : nullptr;
    int   cnt  = dict ? *(int*)((uint8_t*)dict + 0x20) : 0;
    for (int i = 0; i < cnt && ent; ++i) {
        uint8_t* e = (uint8_t*)ent + 0x20 + i * 0x10;
        if (*(int*)(e + 0x00) < 0) continue;
        if (*(int*)(e + 0x08) == type) { *(int*)(e + 0x0C) = value; return; }
    }
}

// ============================================================
// 4) 资源不消耗
// ============================================================
// 4a) 首选：打开游戏自带开关
{
    const FieldInfo* fi = p_il2cpp_class_get_field_from_name(kWallet, "<InfiniteMoney>k__BackingField");
    uint8_t one = 1;  p_il2cpp_field_static_set_value(fi, &one);
}
// 4b) 备选：inline hook 扣款落点
//     目标 RVA 0x812550  void Wallet::RemoveCurrencyAfterMoving(Wallet* this /*rcx*/, int32_t type /*edx*/)
static void(__fastcall* Orig_RemoveAfterMove)(void*, int32_t);
static void __fastcall Hook_RemoveAfterMove(void* self, int32_t type) {
    if (g_FreeSpend) return;
    Orig_RemoveAfterMove(self, type);
}
// 安装（x64，长度≥12 的蹦床；0x812550 前 12 字节：
//   48 89 5C 24 10 56 48 83 EC 20 80 3D  → 可用 12 字节相对跳转）
//   48 B8 <addr64> FF E0            ; mov rax, imm64 ; jmp rax
//   或 E9 <rel32>（配合 5 字节覆盖 + 长度还原）

// 4c) 另一种“免费建造”写法：不 hook，只在付款时把余额补回去（不推荐，会有可见的掉钱动画）
```

**hook 时机注意**：`Wallet` / `Managers` 类在游戏启动早期（`il2cpp_init` 之后）就已注册，但 `Managers._Inst` 只有在进入关卡场景后才非空；`InfiniteMoney` 静态字段是全局的，可以随时写（静态块由 IL2CPP 懒初始化，首次访问时分配——**建议先调一次 `set_InfiniteMoney` 或 `get_InfiniteMoney` 触发初始化**，再直接写内存）。

---

## 10. 风险与注意事项

1. **ASLR / 版本绑定**：本报告所有 RVA 只在 **2.4.2 / Unity 6000.0.66f2 / 这份 GameAssembly.dll** 上成立（已用文件内指针表 + 反汇编双重校验）。换版本必须重新对 dump 求 RVA；`Il2CppClass.static_fields` 的 `+0xB8` 偏移也可能变。
2. **`SetCurrency` 的容量钳制**：`TotalCapacity`（Wallet+0x54）会限制 `SetCurrency` 的结果；要“无限钱”请用 `FastSetCurrency` 或直接写字典。
3. **`SetCurrency` 的网络门控**：`NetworkBigBoss.HasWorldAuth || HasClientCaughtUp`；单人局一般成立，但**写完后一定要读回校验**。
4. **联机**：`InfiniteMoney` 是全局静态，会影响本机钱包的扣款；联机时钱数会通过 `Player.SendWalletCurrencyCount`（RVA 0x699FA0）同步，可能与对端不一致。单机/本地合作（同屏双人）无碍。
5. **不要写 `CurrencyMap` 的 `Coins/Gems/...` 那 8 个 `+0x10..+0x2C` 字段**：它们只是 Unity 序列化镜像，运行时游戏不读；写它们不会有任何效果（甚至会在保存时被覆盖）。
6. **不要写 `CurrencyBag._currentCurrency`(0x70) / `_currencyToAdd`(0x78)**：那是钱袋动画状态，改了会出现显示与真实数值不一致。
7. **线程**：所有 IL2CPP 调用与托管对象访问都必须在附着到 IL2CPP 运行时的线程上进行（`il2cpp_thread_attach`），或直接在游戏主线程（hook 回调 / 输入线程）里做。
8. **静态字段读取的稳健写法**：优先 `il2cpp_field_static_get_value/set_value`，而不是硬编码 `klass+0xB8`。

---

## 附录 A：从 dump 直接生成的完整表

> 以下表格由 `dump.txt` 自动解析生成；`RVA = p0 - 0x7FFF79E50000`。
> 列含义：`field / type / off / note`（note 里的 `static` 表示静态字段，`off` 是相对静态数据块的偏移）；`method / ret / argc / p0 / RVA`。

### A.1 Wallet 字段与方法（全量 68 方法）

`[CLASS] Wallet : UnityEngine.MonoBehaviour size=0xA8`

| field | type | off | note |
|---|---|---|---|
| `MAX_PAY_COINS_FORCE` | System.Single | 0x0000 | static |
| `PLAYER_COIN_SPEED_MODIFIER` | System.Single | 0x0000 | static |
| `OnPickupCurrency` | System.Action<DroppableCurrency> | 0x0020 |  |
| `payTaxesAbove` | System.Int32 | 0x0028 |  |
| `position` | UnityEngine.Vector3 | 0x002C |  |
| `currencyPickupSound` | CurrencyMap<AudioEmitter> | 0x0038 |  |
| `currencyDropSound` | CurrencyMap<AudioEmitter> | 0x0040 |  |
| `payTaxesSound` | AudioEmitter | 0x0048 |  |
| `onlyGrabPlayerCoins` | System.Boolean | 0x0050 |  |
| `showCurrencyBag` | System.Boolean | 0x0051 |  |
| `TotalCapacity` | System.Int32 | 0x0054 |  |
| `_allowedCurrencies` | CurrencyMap<System.Boolean> | 0x0058 |  |
| `_currencyAmount` | CurrencyMap<System.Int32> | 0x0060 |  |
| `_pickupAttributesProvider` | IPickupAttributeProvider | 0x0068 |  |
| `_currencyBag` | CurrencyBag | 0x0070 |  |
| `_playerRef` | Player | 0x0078 |  |
| `_mover` | Mover | 0x0080 |  |
| `_payingTaxes` | System.Boolean | 0x0088 |  |
| `parentHeaderRef` | CRPCHeader | 0x0090 |  |
| `suckCurrencyRPCIndex` | System.Int32 | 0x0098 |  |
| `spawnLossPrefabRPCIndex` | System.Int32 | 0x009C |  |
| `<InfiniteMoney>k__BackingField` | System.Boolean | 0x0000 | static |
| `<DebugDisableTaxes>k__BackingField` | System.Boolean | 0x0001 | static |
| `AllowCollect` | System.Boolean | 0x00A0 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `add_OnPickupCurrency(System.Action<DroppableCurrency>)` | System.Void | 1 | `0x7FFF7A664290` | `0x814290` |
| `remove_OnPickupCurrency(System.Action<DroppableCurrency>)` | System.Void | 1 | `0x7FFF7A6646A0` | `0x8146A0` |
| `get_GetGO` | UnityEngine.GameObject | 0 | `0x7FFF7A2FDC00` | `0x4ADC00` |
| `get_InfiniteMoney` | System.Boolean | 0 | `0x7FFF7A664570` | `0x814570` |
| `set_InfiniteMoney(System.Boolean)` | System.Void | 1 | `0x7FFF7A664810` | `0x814810` |
| `get_DebugDisableTaxes` | System.Boolean | 0 | `0x7FFF7A6643E0` | `0x8143E0` |
| `set_DebugDisableTaxes(System.Boolean)` | System.Void | 1 | `0x7FFF7A6647C0` | `0x8147C0` |
| `get_HasDroppableCurrency` | System.Boolean | 0 | `0x7FFF7A664470` | `0x814470` |
| `get_TotalCurrency` | System.Int32 | 0 | `0x7FFF7A6645B0` | `0x8145B0` |
| `get_Coins` | System.Int32 | 0 | `0x7FFF7A664390` | `0x814390` |
| `set_Coins(System.Int32)` | System.Void | 1 | `0x7FFF7A6647B0` | `0x8147B0` |
| `get_Gems` | System.Int32 | 0 | `0x7FFF7A664420` | `0x814420` |
| `set_Gems(System.Int32)` | System.Void | 1 | `0x7FFF7A664800` | `0x814800` |
| `get_CanGrabCoins` | System.Boolean | 0 | `0x7FFF7A664340` | `0x814340` |
| `set_CanGrabCoins(System.Boolean)` | System.Void | 1 | `0x7FFF7A664750` | `0x814750` |
| `GetCurrency(CurrencyType)` | System.Int32 | 1 | `0x7FFF7A660D60` | `0x810D60` |
| `SetCurrency(CurrencyType,System.Int32)` | System.Void | 2 | `0x7FFF7A662AC0` | `0x812AC0` |
| `GetCurrencyAmounts` | CurrencyMap<System.Int32> | 0 | `0x7FFF7A660D10` | `0x810D10` |
| `Reset` | System.Void | 0 | `0x7FFF7A6626C0` | `0x8126C0` |
| `FastSetCurrency(CurrencyType,System.Int32)` | System.Void | 2 | `0x7FFF7A660AD0` | `0x810AD0` |
| `FirstAvailableDroppableCurrency` | CurrencyType | 0 | `0x7FFF7A660B80` | `0x810B80` |
| `UpdateCurrencyStats(CurrencyType)` | System.Void | 1 | `0x7FFF7A663F70` | `0x813F70` |
| `CanGrabCurrency(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A660400` | `0x810400` |
| `SetCanGrabCurrency(CurrencyType,System.Boolean)` | System.Void | 2 | `0x7FFF7A662930` | `0x812930` |
| `AllowGrabCurrency` | System.Void | 0 | `0x7FFF7A65FE10` | `0x80FE10` |
| `DisallowGrabCurrency` | System.Void | 0 | `0x7FFF7A6605B0` | `0x8105B0` |
| `Awake` | System.Void | 0 | `0x7FFF7A65FEF0` | `0x80FEF0` |
| `OnLevelLoadedHandler(System.Boolean)` | System.Void | 1 | `0x7FFF7A661840` | `0x811840` |
| `OnP2JoinLeaveHandler(System.Boolean)` | System.Void | 1 | `0x7FFF7A661870` | `0x811870` |
| `TransferImportantCurrencies` | System.Void | 0 | `0x7FFF7A663940` | `0x813940` |
| `OnGameLossHandler` | System.Void | 0 | `0x7FFF7A661600` | `0x811600` |
| `SpawnLossPrefab(CurrencyType,System.Int16)` | System.Void | 2 | `0x7FFF7A662EA0` | `0x812EA0` |
| `OnSailAwayHandler` | System.Void | 0 | `0x7FFF7A661880` | `0x811880` |
| `SetCurrencyBag(CurrencyBag)` | System.Void | 1 | `0x7FFF7A662A70` | `0x812A70` |
| `Start` | System.Void | 0 | `0x7FFF7A663350` | `0x813350` |
| `OnEnable` | System.Void | 0 | `0x7FFF7A661550` | `0x811550` |
| `OnDisable` | System.Void | 0 | `0x7FFF7A661480` | `0x811480` |
| `OnDestroy` | System.Void | 0 | `0x7FFF7A6611B0` | `0x8111B0` |
| `HasCurrency(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A6603A0` | `0x8103A0` |
| `AddCurrency(CurrencyType,System.Int32)` | System.Void | 2 | `0x7FFF7A65FD90` | `0x80FD90` |
| `RemoveCurrency(CurrencyType,System.Int32)` | System.Void | 2 | `0x7FFF7A662640` | `0x812640` |
| `TryToPayTaxes(UnityEngine.Collider2D)` | System.Boolean | 1 | `0x7FFF7A663DB0` | `0x813DB0` |
| `PayTaxes` | System.Collections.IEnumerator | 0 | `0x7FFF7A661BB0` | `0x811BB0` |
| `TryToGrabCurrency(UnityEngine.GameObject)` | System.Boolean | 1 | `0x7FFF7A663C10` | `0x813C10` |
| `SuckCurrency(DroppableCurrency,System.Boolean)` | System.Boolean | 2 | `0x7FFF7A6633D0` | `0x8133D0` |
| `SendSuckCurrency(DroppableCurrency)` | System.Void | 1 | `0x7FFF7A6627C0` | `0x8127C0` |
| `MoveCurrencyObject(CurrencyType,UnityEngine.Transform,UnityEngine.Vector3,System.Boolean)` | IPayableCurrency | 4 | `0x7FFF7A661080` | `0x811080` |
| `MoveCurrencyObject(CurrencyType,UnityEngine.Vector3,System.Boolean)` | IPayableCurrency | 3 | `0x7FFF7A661120` | `0x811120` |
| `PrepareCurrencyObjectBeforeMoving(CurrencyType)` | DroppableCurrency | 1 | `0x7FFF7A6620D0` | `0x8120D0` |
| `RemoveCurrencyAfterMoving(CurrencyType)` | System.Void | 1 | `0x7FFF7A662550` | `0x812550` |
| `DropCurrency(CurrencyType,PickUpPolicy,System.Boolean)` | DroppableCurrency | 3 | `0x7FFF7A660690` | `0x810690` |
| `DropCurrency` | DroppableCurrency | 7 | `0x7FFF7A660720` | `0x810720` |
| `CanDropCurrency(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A6603A0` | `0x8103A0` |
| `HideCurrencyBagDelayIfNoPayable(System.Single)` | System.Void | 1 | `0x7FFF7A660EF0` | `0x810EF0` |
| `ShowCurrencyBag` | System.Void | 0 | `0x7FFF7A662D50` | `0x812D50` |
| `HideCurrencyBag` | System.Void | 0 | `0x7FFF7A660FE0` | `0x810FE0` |
| `SetCurrencyBagVisible(System.Boolean)` | System.Void | 1 | `0x7FFF7A6629A0` | `0x8129A0` |
| `OnTriggerEnter2D(UnityEngine.Collider2D)` | System.Void | 1 | `0x7FFF7A661A90` | `0x811A90` |
| `OnTriggerStay2D(UnityEngine.Collider2D)` | System.Void | 1 | `0x7FFF7A661B20` | `0x811B20` |
| `Persistent.IBehaviour.RetrieveData` | System.Object | 0 | `0x7FFF7A661F40` | `0x811F40` |
| `Persistent.IBehaviour.ApplyData(System.Object)` | System.Void | 1 | `0x7FFF7A661C10` | `0x811C10` |
| `BeginRegisteringRPCs(CRPCHeader)` | System.Boolean | 1 | `0x7FFF7A660260` | `0x810260` |
| `GetSerializationData(System.Type&)` | System.Void | 1 | `0x7FFF7A660DC0` | `0x810DC0` |
| `DeserializeFromData` | System.Void | 0 | `0x7FFF7A660460` | `0x810460` |
| `ReceiveSuckCoinRPC` | System.Void | 0 | `0x7FFF7A662220` | `0x812220` |
| `SendSpawnLossPrefab(CurrencyType,System.Int16)` | System.Void | 2 | `0x7FFF7A662710` | `0x812710` |
| `SpawnLossPrefabRPC` | System.Void | 0 | `0x7FFF7A662E20` | `0x812E20` |
| `.ctor` | System.Void | 0 | `0x7FFF7A664200` | `0x814200` |

### A.2 Player 中与钱包/货币/支付相关的成员

`[CLASS] Player : UnityEngine.MonoBehaviour size=0x290`

| field | type | off | note |
|---|---|---|---|
| `timeBetweenCoins` | System.Single | 0x0040 |  |
| `timeBetweenCoinsForTouchSupport` | System.Single | 0x0044 |  |
| `overflowCoinsWaterProbability` | System.Single | 0x0050 |  |
| `currencyLaunchSound` | CurrencyMap<AudioEmitter> | 0x0058 |  |
| `lostCrownSound` | AudioEmitter | 0x0060 |  |
| `pickupCrownSound` | AudioEmitter | 0x0068 |  |
| `<wallet>k__BackingField` | Wallet | 0x00E8 |  |
| `_currencyBag` | CurrencyBag | 0x0120 |  |
| `_completingPayable` | Payable | 0x0130 |  |
| `_payState` | Player.PayState | 0x0170 |  |
| `_floatingCurrency` | System.Collections.Generic.List<IPayableCurrency> | 0x0178 |  |
| `_payTimer` | System.Single | 0x01CC |  |
| `_isPickingUpCrown` | System.Boolean | 0x01E0 |  |
| `_pickingUpCrownRoutine` | UnityEngine.Coroutine | 0x01F0 |  |
| `_walletCountIndex` | System.Int32 | 0x0208 |  |
| `_setCrownStateIndex` | System.Int32 | 0x020C |  |
| `_currencyDroppedFromDamageIndex` | System.Int32 | 0x0210 |  |
| `<selectedPayable>k__BackingField` | Payable | 0x0240 |  |
| `<currencyDroppingEnabled>k__BackingField` | System.Boolean | 0x0248 |  |
| `autoDropCoins` | System.Boolean | 0x0270 |  |
| `_petrifyCrownEffectDelay` | System.Single | 0x0288 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_wallet` | Wallet | 0 | `0x7FFF7A4E20F0` | `0x6920F0` |
| `set_wallet(Wallet)` | System.Void | 1 | `0x7FFF7A4EFAB0` | `0x69FAB0` |
| `get_selectedPayable` | Payable | 0 | `0x7FFF7A3FE7B0` | `0x5AE7B0` |
| `set_selectedPayable(Payable)` | System.Void | 1 | `0x7FFF7A4EFA80` | `0x69FA80` |
| `get_currencyDroppingEnabled` | System.Boolean | 0 | `0x7FFF7A4EF450` | `0x69F450` |
| `set_currencyDroppingEnabled(System.Boolean)` | System.Void | 1 | `0x7FFF7A4EF920` | `0x69F920` |
| `get_coins` | System.Int32 | 0 | `0x7FFF7A4EF420` | `0x69F420` |
| `get_gems` | System.Int32 | 0 | `0x7FFF7A4EF570` | `0x69F570` |
| `SetCurrencyBag(CurrencyBag)` | System.Void | 1 | `0x7FFF7A4EA630` | `0x69A630` |
| `GetCurrencyBag` | CurrencyBag | 0 | `0x7FFF7A40A920` | `0x5BA920` |
| `SelectPayable(Payable)` | System.Void | 1 | `0x7FFF7A4E9860` | `0x699860` |
| `DeselectPayable` | System.Void | 0 | `0x7FFF7A4E3E70` | `0x693E70` |
| `SetPayState(Player.PayState)` | System.Void | 1 | `0x7FFF7A4EA890` | `0x69A890` |
| `UpdatePayState(System.Boolean,System.Boolean,System.Boolean)` | System.Void | 3 | `0x7FFF7A4EDBA0` | `0x69DBA0` |
| `AddCurrencyToSelectedPayable` | System.Void | 0 | `0x7FFF7A4E25D0` | `0x6925D0` |
| `CustomCurrencyPaymentMethod(CurrencyType,UnityEngine.Transform,UnityEngine.Vector3)` | IPayableCurrency | 3 | `0x7FFF7A4E3A50` | `0x693A50` |
| `PayWithCrown(UnityEngine.Transform,UnityEngine.Vector3)` | IPayableCurrency | 2 | `0x7FFF7A4E7550` | `0x697550` |
| `CustomCurrencyPaymentFailed(CurrencyType)` | System.Void | 1 | `0x7FFF7A4E3A20` | `0x693A20` |
| `CustomCurrencyPaymentSuccessful(CurrencyType)` | System.Void | 1 | `0x7FFF7A4E3B30` | `0x693B30` |
| `DebugPayForSelection(System.Single)` | System.Void | 1 | `0x7FFF7A4E3C60` | `0x693C60` |
| `DebugPayForSelectionInternal(System.Single)` | System.Collections.IEnumerator | 1 | `0x7FFF7A4E3BE0` | `0x693BE0` |
| `DropFloatingCurrency` | System.Void | 0 | `0x7FFF7A4E4430` | `0x694430` |
| `CoinFellFromBag(CurrencyType)` | System.Void | 1 | `0x7FFF7A4E38D0` | `0x6938D0` |
| `TryPickupCoin(UnityEngine.GameObject)` | System.Boolean | 1 | `0x7FFF7A4EC410` | `0x69C410` |
| `TryDropCurrency(CurrencyType)` | System.Void | 1 | `0x7FFF7A4EC380` | `0x69C380` |
| `SendCurrencyDroppedFromDamage(System.Collections.Generic.Dictionary<CurrencyType,System.Int32>)` | System.Void | 1 | `0x7FFF7A4E9AB0` | `0x699AB0` |
| `RecvCurrencyDroppedFromDamage` | System.Void | 0 | `0x7FFF7A4E8960` | `0x698960` |
| `SendWalletCurrencyCount(CurrencyType)` | System.Void | 1 | `0x7FFF7A4E9FA0` | `0x699FA0` |
| `RecvWalletCurrencyCount` | System.Void | 0 | `0x7FFF7A4E8BE0` | `0x698BE0` |

### A.3 CurrencyMap`1（泛型定义，off 全为 0 属正常）

`[CLASS] CurrencyMap`1 : System.Object size=0x0`

| field | type | off | note |
|---|---|---|---|
| `Coins` | T | 0x0000 |  |
| `Gems` | T | 0x0000 |  |
| `Crown` | T | 0x0000 |  |
| `Skulls` | T | 0x0000 |  |
| `Shades` | T | 0x0000 |  |
| `Merchandise` | T | 0x0000 |  |
| `Candle` | T | 0x0000 |  |
| `Egg` | T | 0x0000 |  |
| `_map` | System.Collections.Generic.Dictionary<CurrencyType,T> | 0x0000 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_Count` | System.Int32 | 0 | `0x7FFF7A128B70` | `0x2D8B70` |
| `.ctor` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `ResetValues` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `Clone` | CurrencyMap<T> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `get_Item(CurrencyType)` | T | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `set_Item(CurrencyType,T)` | System.Void | 2 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `Contains(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A120C50` | `0x2D0C50` |
| `GetEnumerator` | System.Collections.Generic.IEnumerator<System.Collections.Generic.KeyValuePair<CurrencyType,T>> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.IEnumerable.GetEnumerator` | System.Collections.IEnumerator | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `OnBeforeSerialize` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `OnAfterDeserialize` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `GetMap` | System.Collections.Generic.Dictionary<CurrencyType,T> | 0 | `0x7FFF7A123580` | `0x2D3580` |

### A.4 CurrencyType 枚举成员

`[CLASS] CurrencyType : System.Enum size=0x14`

| field | type | off | note |
|---|---|---|---|
| `value__` | System.Int32 | 0x0010 |  |
| `Coins` | CurrencyType | 0x0000 | static |
| `Gems` | CurrencyType | 0x0000 | static |
| `Crown` | CurrencyType | 0x0000 | static |
| `Skulls` | CurrencyType | 0x0000 | static |
| `Shades` | CurrencyType | 0x0000 | static |
| `Merchandise` | CurrencyType | 0x0000 | static |
| `Candle` | CurrencyType | 0x0000 | static |
| `Egg` | CurrencyType | 0x0000 | static |

### A.5 CurrencyBag（钱袋，Wallet._currencyBag 指向它）

`[CLASS] CurrencyBag : UnityEngine.MonoBehaviour size=0xD0`

| field | type | off | note |
|---|---|---|---|
| `FADE_IN_TIME` | System.Single | 0x0000 | static |
| `OPEN_TIME` | System.Single | 0x0000 | static |
| `CLOSE_TIME` | System.Single | 0x0000 | static |
| `FADE_OUT_TIME` | System.Single | 0x0000 | static |
| `OVERFLOW_LIMIT` | System.Int32 | 0x0000 | static |
| `_front` | UnityEngine.SpriteRenderer | 0x0020 |  |
| `_back` | UnityEngine.SpriteRenderer | 0x0028 |  |
| `_closed` | UnityEngine.SpriteRenderer | 0x0030 |  |
| `CurrencySpawnInterval` | System.Single | 0x0038 |  |
| `TargetPosYOffset` | System.Single | 0x003C |  |
| `_upgradeVFX` | UnityEngine.ParticleSystem | 0x0040 |  |
| `OwningPlayerId` | System.Int32 | 0x0048 |  |
| `CachedInterfaceCam` | InterfaceCamera | 0x0050 |  |
| `<CurrentFadeState>k__BackingField` | CurrencyBag.FadeState | 0x0058 |  |
| `_currencyObjects` | System.Collections.Generic.List<BagCurrency> | 0x0060 |  |
| `_pools` | CurrencyMap<System.Collections.Generic.Stack<BagCurrency>> | 0x0068 |  |
| `_currentCurrency` | CurrencyMap<System.Int32> | 0x0070 |  |
| `_currencyToAdd` | CurrencyMap<System.Int32> | 0x0078 |  |
| `_container` | UnityEngine.Transform | 0x0080 |  |
| `_show` | Coatsink.Common.IHaglet | 0x0088 |  |
| `_blink` | Coatsink.Common.IHaglet | 0x0090 |  |
| `_hide` | Coatsink.Common.IHaglet | 0x0098 |  |
| `_dripCurrency` | Coatsink.Common.IHaglet | 0x00A0 |  |
| `_hideLater` | Coatsink.Common.IHaglet<System.Single> | 0x00A8 |  |
| `_tweenSequence` | DG.Tweening.Sequence | 0x00B0 |  |
| `_clearTweenSequence` | DG.Tweening.TweenCallback | 0x00B8 |  |
| `_showingCurrency` | System.Boolean | 0x00C0 |  |
| `_hasInitialised` | System.Boolean | 0x00C1 |  |
| `<DebugHideCurrencyBag>k__BackingField` | System.Boolean | 0x0000 | static |
| `<player>k__BackingField` | Player | 0x00C8 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_CurrentFadeState` | CurrencyBag.FadeState | 0 | `0x7FFF7A5C1B70` | `0x771B70` |
| `set_CurrentFadeState(CurrencyBag.FadeState)` | System.Void | 1 | `0x7FFF7A5C1B80` | `0x771B80` |
| `get_DebugHideCurrencyBag` | System.Boolean | 0 | `0x7FFF7A80DC10` | `0x9BDC10` |
| `set_DebugHideCurrencyBag(System.Boolean)` | System.Void | 1 | `0x7FFF7A80DF40` | `0x9BDF40` |
| `get_ShouldShow` | System.Boolean | 0 | `0x7FFF7A80DCF0` | `0x9BDCF0` |
| `get_IsBusy` | System.Boolean | 0 | `0x7FFF7A80DC50` | `0x9BDC50` |
| `get_player` | Player | 0 | `0x7FFF7A2FF900` | `0x4AF900` |
| `set_player(Player)` | System.Void | 1 | `0x7FFF7A2FFA10` | `0x4AFA10` |
| `get_firstBagPosX` | System.Single | 0 | `0x7FFF7A80DDA0` | `0x9BDDA0` |
| `get_secondBagPosX` | System.Single | 0 | `0x7FFF7A80DE70` | `0x9BDE70` |
| `RecalcPosition` | System.Void | 0 | `0x7FFF7A80C2E0` | `0x9BC2E0` |
| `SetPlayer(Player)` | System.Void | 1 | `0x7FFF7A80CC40` | `0x9BCC40` |
| `Awake` | System.Void | 0 | `0x7FFF7A80B350` | `0x9BB350` |
| `Refresh` | System.Void | 0 | `0x7FFF7A80CA20` | `0x9BCA20` |
| `Init` | System.Void | 0 | `0x7FFF7A80BC10` | `0x9BBC10` |
| `ShowCurrency(System.Collections.Generic.Dictionary<CurrencyType,System.Int32>)` | System.Void | 1 | `0x7FFF7A80CFB0` | `0x9BCFB0` |
| `StartShow` | System.Void | 0 | `0x7FFF7A80DA40` | `0x9BDA40` |
| `HideImmediate` | System.Void | 0 | `0x7FFF7A80B9B0` | `0x9BB9B0` |
| `StartHide` | System.Void | 0 | `0x7FFF7A80D970` | `0x9BD970` |
| `StartHideLater(System.Single)` | System.Void | 1 | `0x7FFF7A80D8A0` | `0x9BD8A0` |
| `HideLater(System.Single)` | System.Collections.Generic.IEnumerator<Coatsink.Common.Routine.Yield> | 1 | `0x7FFF7A80BB40` | `0x9BBB40` |
| `Blink` | System.Collections.Generic.IEnumerator<Coatsink.Common.Routine.Yield> | 0 | `0x7FFF7A80B360` | `0x9BB360` |
| `Show` | System.Collections.Generic.IEnumerator<Coatsink.Common.Routine.Yield> | 0 | `0x7FFF7A80D400` | `0x9BD400` |
| `HasEnoughCurrency(CurrencyMap<System.Int32>)` | System.Boolean | 1 | `0x7FFF7A80B7E0` | `0x9BB7E0` |
| `Hide` | System.Collections.Generic.IEnumerator<Coatsink.Common.Routine.Yield> | 0 | `0x7FFF7A80BBB0` | `0x9BBBB0` |
| `ShowActiveCurrency(System.Boolean)` | System.Void | 1 | `0x7FFF7A80CF00` | `0x9BCF00` |
| `AddCurrency(CurrencyType,System.Int32)` | System.Void | 2 | `0x7FFF7A80B1C0` | `0x9BB1C0` |
| `RemoveCurrency(CurrencyType,System.Int32)` | System.Void | 2 | `0x7FFF7A80CA60` | `0x9BCA60` |
| `Clear` | System.Void | 0 | `0x7FFF7A80B3C0` | `0x9BB3C0` |
| `DespawnCurrency(BagCurrency)` | System.Void | 1 | `0x7FFF7A80B610` | `0x9BB610` |
| `DripCurrency` | System.Collections.Generic.IEnumerator<Coatsink.Common.Routine.Yield> | 0 | `0x7FFF7A80B780` | `0x9BB780` |
| `SpawnCurrency(CurrencyType)` | System.Void | 1 | `0x7FFF7A80D460` | `0x9BD460` |
| `DetermineBagLayer(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A80B700` | `0x9BB700` |
| `CurrencyFell(BagCurrency)` | System.Void | 1 | `0x7FFF7A80B480` | `0x9BB480` |
| `PlayUpgradeEffect` | System.Void | 0 | `0x7FFF7A80C260` | `0x9BC260` |
| `OnDestroy` | System.Void | 0 | `0x7FFF7A80C220` | `0x9BC220` |
| `.ctor` | System.Void | 0 | `0x7FFF7A80DB30` | `0x9BDB30` |
| `<Init>b__52_0` | System.Void | 0 | `0x7FFF7A3E27B0` | `0x5927B0` |

### A.6 CurrencyBagHandler / CurrencyBagPayable

`[CLASS] CurrencyBagHandler : UnityEngine.MonoBehaviour size=0x48`

| field | type | off | note |
|---|---|---|---|
| `_regularBags` | CurrencyBag[] | 0x0020 |  |
| `_hermesBags` | CurrencyBag[] | 0x0028 |  |
| `_eggBaskets` | CurrencyBag[] | 0x0030 |  |
| `COIN_BAG_MERGE_HEIGHT` | System.Single | 0x0000 | static |
| `COIN_BAG_SPLIT_HORI_HEIGHT_TOP` | System.Single | 0x0000 | static |
| `COIN_BAG_SPLIT_HORI_HEIGHT_BOTTOM` | System.Single | 0x0000 | static |
| `_currentPlayerBags` | CurrencyBag[] | 0x0038 |  |
| `_isUpgrade` | System.Boolean | 0x0040 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `Awake` | System.Void | 0 | `0x7FFF7A8089A0` | `0x9B89A0` |
| `OnDestroy` | System.Void | 0 | `0x7FFF7A809680` | `0x9B9680` |
| `OnGameStartHandler` | System.Void | 0 | `0x7FFF7A809820` | `0x9B9820` |
| `OnP2JoinLeaveHandler(System.Boolean)` | System.Void | 1 | `0x7FFF7A809990` | `0x9B9990` |
| `SendUpdateCurrencyBag(CurrencyBagType)` | System.Void | 1 | `0x7FFF7A809DA0` | `0x9B9DA0` |
| `ReceiveUpdateCurrencyBag` | System.Void | 0 | `0x7FFF7A809D30` | `0x9B9D30` |
| `ChangeCurrencyBag(CurrencyBagType,System.Int32)` | System.Void | 2 | `0x7FFF7A808C80` | `0x9B8C80` |
| `GetCurrencyBagByType(CurrencyBagType,System.Int32)` | CurrencyBag | 2 | `0x7FFF7A809260` | `0x9B9260` |
| `SetCurrencyBag(CurrencyBagType,System.Int32)` | CurrencyBag | 2 | `0x7FFF7A809E80` | `0x9B9E80` |
| `OnCamerasMerged` | System.Void | 0 | `0x7FFF7A8092C0` | `0x9B92C0` |
| `OnCamerasSplit(System.Boolean)` | System.Void | 1 | `0x7FFF7A809430` | `0x9B9430` |
| `RecalcPositions` | System.Void | 0 | `0x7FFF7A809C30` | `0x9B9C30` |
| `.ctor` | System.Void | 0 | `0x7FFF7A80A0D0` | `0x9BA0D0` |

### A.7 CurrencyBagPayable

`[CLASS] CurrencyBagPayable : UnityEngine.MonoBehaviour size=0x30`

| field | type | off | note |
|---|---|---|---|
| `_unlocksCurrencyBagType` | CurrencyBagType | 0x0020 |  |
| `_despawnDurationSeconds` | System.Single | 0x0024 |  |
| `_spriteFX` | SpriteRendererFX | 0x0028 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_UnlocksCurrencyBagType` | CurrencyBagType | 0 | `0x7FFF7A300680` | `0x4B0680` |
| `Awake` | System.Void | 0 | `0x7FFF7A80A120` | `0x9BA120` |
| `get_OnPay` | System.Action<Player> | 0 | `0x7FFF7A80A2C0` | `0x9BA2C0` |
| `CanPay(Player)` | System.Boolean | 1 | `0x7FFF7A80A190` | `0x9BA190` |
| `IsLocked(Player,LockIndicator.LockReason&)` | System.Boolean | 2 | `0x7FFF7A36BDC0` | `0x51BDC0` |
| `OnPayHandler(Player)` | System.Void | 1 | `0x7FFF7A80A200` | `0x9BA200` |
| `.ctor` | System.Void | 0 | `0x7FFF7A80A2A0` | `0x9BA2A0` |

### A.8 CurrencyManager / CurrencyManagerExt

`[CLASS] CurrencyManager : Manager size=0x30`

| field | type | off | note |
|---|---|---|---|
| `AllCurrencyTypes` | CurrencyType[] | 0x0000 | static |
| `_currencyMapping` | System.Collections.Generic.List<CurrencyManager.CurrencyDataMapping> | 0x0020 |  |
| `_currencyMappingCache` | System.Collections.Generic.Dictionary<CurrencyType,CurrencyManager.CurrencyDataMapping> | 0x0028 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `GetCurrencyTypeIndicator(CurrencyType)` | CurrencyIndicator | 1 | `0x7FFF7A80F250` | `0x9BF250` |
| `GetCurrencyTypePrefab(CurrencyType)` | T | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `Init` | System.Void | 0 | `0x7FFF7A2F70F0` | `0x4A70F0` |
| `OnValidate` | System.Void | 0 | `0x7FFF7A80F430` | `0x9BF430` |
| `OnBeforeSerialize` | System.Void | 0 | `0x7FFF7A2F70F0` | `0x4A70F0` |
| `OnAfterDeserialize` | System.Void | 0 | `0x7FFF7A80F2C0` | `0x9BF2C0` |
| `FillCurrencyTypes` | System.Void | 0 | `0x7FFF7A80EED0` | `0x9BEED0` |
| `TryGetData(CurrencyType,CurrencyConfig&)` | System.Boolean | 2 | `0x7FFF7A80F500` | `0x9BF500` |
| `.ctor` | System.Void | 0 | `0x7FFF7A80F690` | `0x9BF690` |
| `.cctor` | System.Void | 0 | `0x7FFF7A80F5D0` | `0x9BF5D0` |

### A.9 CurrencyManagerExt

`[CLASS] CurrencyManagerExt : System.Object size=0x10`

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `SpawnCurrency(CurrencyType,UnityEngine.Vector3,UnityEngine.Quaternion,UnityEngine.Transform)` | T | 4 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `SpawnCurrency(CurrencyType,UnityEngine.Vector3,UnityEngine.Quaternion,UnityEngine.Transform)` | DroppableCurrency | 4 | `0x7FFF7A80EBF0` | `0x9BEBF0` |
| `CanBeDropped(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A80E7F0` | `0x9BE7F0` |
| `CanDropInWater(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A80E870` | `0x9BE870` |
| `GoesIntoBag(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A80EAF0` | `0x9BEAF0` |
| `GetCurrencyOnClientDisconnectHandling(CurrencyType)` | CurrencyOnClientDisconnectHandling | 1 | `0x7FFF7A80E8F0` | `0x9BE8F0` |
| `GetCurrencyOnGameLossHandling(CurrencyType)` | CurrencyOnGameLossHandling | 1 | `0x7FFF7A80E970` | `0x9BE970` |
| `GetCurrencyOnGameLossPrefab(CurrencyType)` | UnityEngine.GameObject | 1 | `0x7FFF7A80E9F0` | `0x9BE9F0` |
| `GetCurrencyOnSailAwayHandling(CurrencyType)` | CurrencyOnSailAwayHandling | 1 | `0x7FFF7A80EA70` | `0x9BEA70` |
| `RandomizeLaunchSound(CurrencyType)` | System.Boolean | 1 | `0x7FFF7A80EB70` | `0x9BEB70` |
| `TryGetDroppedOnGroundStat(CurrencyType,Stat&)` | System.Boolean | 2 | `0x7FFF7A80ECF0` | `0x9BECF0` |
| `TryGetFellOutStat(CurrencyType,Stat&)` | System.Boolean | 2 | `0x7FFF7A80ED90` | `0x9BED90` |
| `TryGetIndicatorSpacing(CurrencyType,System.Single&)` | System.Boolean | 2 | `0x7FFF7A80EE30` | `0x9BEE30` |

### A.10 CurrencyConfig（每种货币的配置，SetCurrency 会查它）

`[CLASS] CurrencyConfig : UnityEngine.ScriptableObject size=0x78`

| field | type | off | note |
|---|---|---|---|
| `CurrencyType` | CurrencyType | 0x0018 |  |
| `Indicator` | CurrencyIndicator | 0x0020 |  |
| `Prefab` | DroppableCurrency | 0x0028 |  |
| `BagPrefab` | BagCurrency | 0x0030 |  |
| `PerPlayerStats` | Stat[] | 0x0038 |  |
| `HeldStat` | Stat | 0x0040 |  |
| `FellOutStat` | Stat | 0x0044 |  |
| `DroppedOnGroundStat` | Stat | 0x0048 |  |
| `GoesIntoBag` | System.Boolean | 0x004C |  |
| `ShouldShowCurrencyBag` | System.Boolean | 0x004D |  |
| `HasCustomPaymentMethods` | System.Boolean | 0x004E |  |
| `CanBeDropped` | System.Boolean | 0x004F |  |
| `CanDropInWater` | System.Boolean | 0x0050 |  |
| `PickupCooldownOnDamage` | System.Single | 0x0054 |  |
| `IndicatorSpacing` | System.Single | 0x0058 |  |
| `HandlingOnClientDisconnect` | CurrencyOnClientDisconnectHandling | 0x005C |  |
| `HandlingOnGameLoss` | CurrencyOnGameLossHandling | 0x0060 |  |
| `GameLossPrefab` | UnityEngine.GameObject | 0x0068 |  |
| `HandlingOnSailAway` | CurrencyOnSailAwayHandling | 0x0070 |  |
| `RandomizeLaunchSound` | System.Boolean | 0x0074 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `.ctor` | System.Void | 0 | `0x7FFF7A80DF80` | `0x9BDF80` |

### A.11 CurrencyDataMapping / WalletData

`[CLASS] CurrencyDataMapping : System.ValueType size=0x20`

| field | type | off | note |
|---|---|---|---|
| `CurrencyType` | CurrencyType | 0x0010 |  |
| `Config` | CurrencyConfig | 0x0018 |  |

### A.12 WalletData（存档侧结构）

`[CLASS] WalletData : System.Object size=0x28`

| field | type | off | note |
|---|---|---|---|
| `coins` | System.Int32 | 0x0010 |  |
| `gems` | System.Int32 | 0x0014 |  |
| `usesCurrencySystem` | System.Boolean | 0x0018 |  |
| `currency` | CurrencyMap<System.Int32> | 0x0020 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `.ctor` | System.Void | 0 | `0x7FFF7A2F86D0` | `0x4A86D0` |

### A.13 CurrencyIndicator

`[CLASS] CurrencyIndicator : UnityEngine.MonoBehaviour size=0x60`

| field | type | off | note |
|---|---|---|---|
| `_currencyAnchorZOffset` | System.Single | 0x0020 |  |
| `_cachedTransform` | UnityEngine.Transform | 0x0028 |  |
| `_parallaxAnchor` | UnityEngine.Transform | 0x0030 |  |
| `_lockIconObj` | UnityEngine.GameObject | 0x0038 |  |
| `_renderer` | UnityEngine.SpriteRenderer | 0x0040 |  |
| `_lockIconSprite` | UnityEngine.SpriteRenderer | 0x0048 |  |
| `_payable` | Payable | 0x0050 |  |
| `_index` | System.Int32 | 0x0058 |  |
| `_playerId` | System.Int32 | 0x005C |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_CachedTransform` | UnityEngine.Transform | 0 | `0x7FFF7A30C240` | `0x4BC240` |
| `get_ParallaxAnchor` | UnityEngine.Transform | 0 | `0x7FFF7A30C250` | `0x4BC250` |
| `get_Renderer` | UnityEngine.SpriteRenderer | 0 | `0x7FFF7A30C2C0` | `0x4BC2C0` |
| `get_LockIconSprite` | UnityEngine.SpriteRenderer | 0 | `0x7FFF7A30C2E0` | `0x4BC2E0` |
| `Init(Payable,System.Int32,System.Int32)` | System.Void | 3 | `0x7FFF7A4A8DF0` | `0x658DF0` |
| `OnEnable` | System.Void | 0 | `0x7FFF7A4A8F70` | `0x658F70` |
| `UpdateParallaxPosition` | System.Collections.IEnumerator | 0 | `0x7FFF7A4A8FF0` | `0x658FF0` |
| `OnDestroy` | System.Void | 0 | `0x7FFF7A4A8F50` | `0x658F50` |
| `.ctor` | System.Void | 0 | `0x7FFF7A4A9050` | `0x659050` |

### A.14 DroppableCurrency（地上/飞行的钱币）

`[CLASS] DroppableCurrency : Droppable size=0x1A0`

| field | type | off | note |
|---|---|---|---|
| `DISTANCE_TO_INDICATOR_THRESHOLD` | System.Single | 0x0000 | static |
| `_currencyType` | CurrencyType | 0x0118 |  |
| `_moveSpeed` | System.Single | 0x011C |  |
| `_moveCurve` | UnityEngine.AnimationCurve | 0x0120 |  |
| `_hitGroundSound` | AudioEmitter | 0x0128 |  |
| `_launchSound` | AudioEmitter | 0x0130 |  |
| `_pickupSound` | AudioEmitter | 0x0138 |  |
| `_dropSound` | AudioEmitter | 0x0140 |  |
| `_moveStart` | UnityEngine.Vector3 | 0x0148 |  |
| `_moveDuration` | System.Single | 0x0154 |  |
| `_moveElapsedTime` | System.Single | 0x0158 |  |
| `_moveTargetObject` | UnityEngine.Transform | 0x0160 |  |
| `_moveTarget` | UnityEngine.Vector3 | 0x0168 |  |
| `_moveTargetOffset` | UnityEngine.Vector3 | 0x0174 |  |
| `_destroyAfterMove` | System.Boolean | 0x0180 |  |
| `_hasHitGround` | System.Boolean | 0x0181 |  |
| `_moving` | System.Boolean | 0x0182 |  |
| `_doneMove` | System.Boolean | 0x0183 |  |
| `_positionSync` | PositionSync | 0x0188 |  |
| `<Persistent>k__BackingField` | Persistent | 0x0190 |  |
| `<MoveTimeUntilNearTarget>k__BackingField` | System.Single | 0x0198 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_Persistent` | Persistent | 0 | `0x7FFF7A497B40` | `0x647B40` |
| `set_Persistent(Persistent)` | System.Void | 1 | `0x7FFF7A4AA3A0` | `0x65A3A0` |
| `get_CurrencyType` | CurrencyType | 0 | `0x7FFF7A4AA370` | `0x65A370` |
| `get_MoveTimeUntilNearTarget` | System.Single | 0 | `0x7FFF7A4AA380` | `0x65A380` |
| `set_MoveTimeUntilNearTarget(System.Single)` | System.Void | 1 | `0x7FFF7A4AA390` | `0x65A390` |
| `OnEnable` | System.Void | 0 | `0x7FFF7A4A9A20` | `0x659A20` |
| `OnDisable` | System.Void | 0 | `0x7FFF7A4A9A00` | `0x659A00` |
| `Update` | System.Void | 0 | `0x7FFF7A4A9EA0` | `0x659EA0` |
| `StopMoving` | System.Void | 0 | `0x7FFF7A4A9E30` | `0x659E30` |
| `OnPrepareDrop` | System.Void | 0 | `0x7FFF7A4A9B80` | `0x659B80` |
| `OnRemoteSpawn` | System.Void | 0 | `0x7FFF7A4A9C40` | `0x659C40` |
| `OnFakeChanged(System.Boolean)` | System.Void | 1 | `0x7FFF7A4A9AD0` | `0x659AD0` |
| `CullModeChange` | System.Collections.IEnumerator | 0 | `0x7FFF7A4A90F0` | `0x6590F0` |
| `OnTriggerExit(UnityEngine.Collider)` | System.Void | 1 | `0x7FFF7A4A9C50` | `0x659C50` |
| `OnCollisionEnter2D(UnityEngine.Collision2D)` | System.Void | 1 | `0x7FFF7A4A98C0` | `0x6598C0` |
| `Bounce` | System.Void | 0 | `0x7FFF7A4A9060` | `0x659060` |
| `MoveTo(UnityEngine.Transform,UnityEngine.Vector3,System.Boolean)` | System.Void | 3 | `0x7FFF7A4A9550` | `0x659550` |
| `MoveTo(UnityEngine.Vector3,System.Boolean)` | System.Void | 2 | `0x7FFF7A4A9320` | `0x659320` |
| `GetSerializationData(System.Type&)` | System.Void | 1 | `0x7FFF7A4A92C0` | `0x6592C0` |
| `OnPrepareForFloatingCurrency` | System.Void | 0 | `0x7FFF7A4A9BA0` | `0x659BA0` |
| `OnAddedToFloatingCurrency` | System.Void | 0 | `0x7FFF7A4A9830` | `0x659830` |
| `DropFromIndicator` | System.Void | 0 | `0x7FFF7A4A9150` | `0x659150` |
| `PayableTransactionComplete` | System.Void | 0 | `0x7FFF7A4A9D20` | `0x659D20` |
| `ResetClientState` | System.Void | 0 | `0x7FFF7A4A9DF0` | `0x659DF0` |
| `TryGetLaunchSound(AudioEmitter&)` | System.Boolean | 1 | `0x7FFF7A4A9E60` | `0x659E60` |
| `TryGetDropSound(AudioEmitter&)` | System.Boolean | 1 | `0x7FFF7A4A9E40` | `0x659E40` |
| `TryGetPickupSound(AudioEmitter&)` | System.Boolean | 1 | `0x7FFF7A4A9E80` | `0x659E80` |
| `.ctor` | System.Void | 0 | `0x7FFF7A4AA320` | `0x65A320` |

### A.15 IPayableCurrency 接口

`[CLASS] IPayableCurrency : ? size=0x0`

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_CurrencyType` | CurrencyType | 0 | `0x7FFF7A128B70` | `0x2D8B70` |
| `get_MoveTimeUntilNearTarget` | System.Single | 0 | `0x7FFF7A12D330` | `0x2DD330` |
| `OnPrepareForFloatingCurrency` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `OnAddedToFloatingCurrency` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `DropFromIndicator` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `PayableTransactionComplete` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `TryGetLaunchSound(AudioEmitter&)` | System.Boolean | 1 | `0x7FFF7A126D70` | `0x2D6D70` |

### A.16 Payable（建筑付款点）关键成员

`[CLASS] Payable : UnityEngine.MonoBehaviour size=0x140`

| field | type | off | note |
|---|---|---|---|
| `price` | System.Int32 | 0x0020 |  |
| `priceIncrease` | System.Int32 | 0x0024 |  |
| `_currency` | CurrencyType | 0x0028 |  |
| `singlePlayerCoinPos` | UnityEngine.Vector2 | 0x0048 |  |
| `_spawnParticlesOnlyWhenPayable` | System.Boolean | 0x0060 |  |
| `soundOnPay` | AudioEmitter | 0x0078 |  |
| `playerPayPointOffset` | UnityEngine.Vector2 | 0x0080 |  |
| `playerPayDistance` | System.Single | 0x0088 |  |
| `payablePlacementExclusionOffset` | UnityEngine.Vector2 | 0x008C |  |
| `payablePlacementExclusionDistance` | System.Single | 0x0094 |  |
| `interactingPlayerHermitType` | System.Nullable<Hermit.HermitType> | 0x0098 |  |
| `_interactingPlayer` | Player | 0x00A0 |  |
| `_currencyIndicatorContainer` | FixedDirection | 0x00E8 |  |
| `_currencyIndicatorHeight` | System.Single[] | 0x00F0 |  |
| `forceBlockPayment` | System.Boolean | 0x0100 |  |
| `<selectedByP1>k__BackingField` | System.Boolean | 0x0110 |  |
| `<selectedByP2>k__BackingField` | System.Boolean | 0x0111 |  |
| `<PlayerSelecting>k__BackingField` | Player | 0x0118 |  |
| `_payRPCIndex` | System.Int32 | 0x012C |  |
| `_playerPayStateIndex` | System.Int32 | 0x0130 |  |
| `_cachedIndicatorCurrencyType` | CurrencyType | 0x0138 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `GetCurrencyIndicatorPosition` | UnityEngine.Vector2 | 0 | `0x7FFF7A4B9350` | `0x669350` |
| `get_Currency` | CurrencyType | 0 | `0x7FFF7A30B9B0` | `0x4BB9B0` |
| `set_Currency(CurrencyType)` | System.Void | 1 | `0x7FFF7A30BC50` | `0x4BBC50` |
| `Pay` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `CanPay(Player)` | System.Boolean | 1 | `0x7FFF7A4B7CF0` | `0x667CF0` |
| `CanSelect(Player)` | System.Boolean | 1 | `0x7FFF7A4B7E40` | `0x667E40` |
| `PrepareCurrencyIndicatorContainer` | System.Void | 0 | `0x7FFF7A4BA0C0` | `0x66A0C0` |
| `UpdateCurrency(CurrencyType,System.Int32)` | System.Void | 2 | `0x7FFF7A4BBB10` | `0x66BB10` |
| `AddCurrencyIndicators(Player)` | System.Void | 1 | `0x7FFF7A4B7300` | `0x667300` |
| `RepositionCurrencyIndicators(Player)` | System.Void | 1 | `0x7FFF7A4BAA30` | `0x66AA30` |
| `GetCurrencyIndicatorHeight(Player)` | System.Single | 1 | `0x7FFF7A4B90B0` | `0x6690B0` |
| `GetCurrencyIndicatorOffset(System.Int32)` | UnityEngine.Vector3 | 1 | `0x7FFF7A4B9150` | `0x669150` |
| `FloatingCurrencyInRow(System.Int32,System.Int32)` | UnityEngine.Vector3 | 2 | `0x7FFF7A4B8C80` | `0x668C80` |
| `OnTransactionStarted(Player)` | System.Void | 1 | `0x7FFF7A2F70F0` | `0x4A70F0` |
| `TransactionStarted(Player)` | System.Void | 1 | `0x7FFF7A4BB950` | `0x66B950` |
| `TransactionComplete` | System.Void | 0 | `0x7FFF7A4BB590` | `0x66B590` |
| `PerformPay` | System.Void | 0 | `0x7FFF7A4B9E20` | `0x669E20` |
| `FakePay` | System.Void | 0 | `0x7FFF7A4B81D0` | `0x6681D0` |
| `PlayerPayPoint` | System.Single | 0 | `0x7FFF7A4B9FE0` | `0x669FE0` |
| `PayableExclusionPoint` | System.Single | 0 | `0x7FFF7A4B9D40` | `0x669D40` |
| `RecvPay` | System.Void | 0 | `0x7FFF7A4BA2C0` | `0x66A2C0` |
| `RecvPayState` | System.Void | 0 | `0x7FFF7A4BA1F0` | `0x66A1F0` |
| `SendTransactionStarted(Player)` | System.Void | 1 | `0x7FFF7A4BB210` | `0x66B210` |
| `RecvTransactionStarted` | System.Void | 0 | `0x7FFF7A4BA8B0` | `0x66A8B0` |

### A.17 Kingdom 中玩家相关成员

`[CLASS] Kingdom : Manager size=0x3A8`

| field | type | off | note |
|---|---|---|---|
| `playerOne` | Player | 0x0080 |  |
| `playerTwo` | Player | 0x0088 |  |
| `_numStolenGems` | System.Int32 | 0x00B8 |  |
| `_activePlayers` | Player[] | 0x00D0 |  |
| `droppedP1Crown` | Crown | 0x00E0 |  |
| `droppedP2Crown` | Crown | 0x00E8 |  |
| `playerTwoGhost` | Ghost | 0x01E0 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_ActivePlayers` | System.Collections.Generic.IEnumerable<Player> | 0 | `0x7FFF7A3FE440` | `0x5AE440` |
| `GetRightmostPlayer` | Player | 0 | `0x7FFF7A3F31C0` | `0x5A31C0` |
| `GetNearestPlayer(System.Single,System.Func<Player,System.Boolean>)` | Player | 2 | `0x7FFF7A3F2CF0` | `0x5A2CF0` |
| `GetNearestPlayer(System.Single)` | Player | 1 | `0x7FFF7A3F2CE0` | `0x5A2CE0` |
| `GetNearestPlayerWithCrown(System.Single)` | Player | 1 | `0x7FFF7A3F2BE0` | `0x5A2BE0` |
| `GetNearestUntransformedPlayer(System.Single)` | Player | 1 | `0x7FFF7A3F2F50` | `0x5A2F50` |
| `GetNearestUntransformedPlayerWithCrown(System.Single)` | Player | 1 | `0x7FFF7A3F2E50` | `0x5A2E50` |
| `GetNearestPlayerOrCrown(System.Single)` | UnityEngine.GameObject | 1 | `0x7FFF7A3F27D0` | `0x5A27D0` |
| `GetPlayer(System.Int32)` | Player | 1 | `0x7FFF7A3F3110` | `0x5A3110` |
| `SetupPlayerIDs` | System.Void | 0 | `0x7FFF7A3FB320` | `0x5AB320` |
| `UpdatePlayerIDs` | System.Void | 0 | `0x7FFF7A3FD4B0` | `0x5AD4B0` |
| `UpdatePlayerAvatars` | System.Void | 0 | `0x7FFF7A3FD210` | `0x5AD210` |

### A.18 Managers 单例

`[CLASS] Managers : Coatsink.Common.SingletonMonoBehaviour`1 size=0x130`

| field | type | off | note |
|---|---|---|---|
| `world` | World | 0x0020 |  |
| `caveHelper` | CaveHelper | 0x0028 |  |
| `director` | Director | 0x0030 |  |
| `enemies` | EnemyManager | 0x0038 |  |
| `blender` | Blender | 0x0040 |  |
| `holder` | Holder | 0x0048 |  |
| `currency` | CurrencyManager | 0x0050 |  |
| `kingdom` | Kingdom | 0x0058 |  |
| `shopPlanner` | ShopPlanner | 0x0060 |  |
| `moveBlockers` | MoveBlockManager | 0x0068 |  |
| `stats` | Stats | 0x0070 |  |
| `tutorial` | TutorialManager | 0x0078 |  |
| `game` | Game | 0x0080 |  |
| `audioManager` | AudioManager | 0x0088 |  |
| `payables` | PayableManager | 0x0090 |  |
| `ambient` | AmbientManager | 0x0098 |  |
| `achievements` | AchievementManager | 0x00A0 |  |
| `challenger` | ChallengeManager | 0x00A8 |  |
| `touchControls` | TouchControls | 0x00B0 |  |
| `lightFaker` | LightFaker | 0x00B8 |  |
| `dropManager` | DroppableRegistrar | 0x00C0 |  |
| `targetCache` | TargetCacher | 0x00C8 |  |
| `prefabs` | PrefabManager | 0x00D0 |  |
| `compCacher` | ComponentCacher | 0x00D8 |  |
| `progressHelper` | ProgressHelper | 0x00E0 |  |
| `richPresence` | RichPresence | 0x00E8 |  |
| `jobPriorities` | JobPriorities | 0x00F0 |  |
| `embarkableManager` | EmbarkableRegistrar | 0x00F8 |  |
| `eventCrownManager` | EventCrownManager | 0x0100 |  |
| `pools` | PoolManager | 0x0108 |  |
| `screen` | ScreenManager | 0x0110 |  |
| `ps4AchievementManager` | PS4AchievementManager | 0x0118 |  |
| `managers` | System.Collections.Generic.List<Manager> | 0x0120 |  |
| `_level` | Level | 0x0128 |  |
| `COOP_ENABLED` | System.Boolean | 0x0000 | static |
| `TABLET_COOP_ENABLED` | System.Boolean | 0x0001 | static |
| `IS_COOP_SKY_2_SKY` | System.Boolean | 0x0002 | static |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_level` | Level | 0 | `0x7FFF7A4481B0` | `0x5F81B0` |
| `get_IsP2Active` | System.Boolean | 0 | `0x7FFF7A448090` | `0x5F8090` |
| `get_IsP2Playing` | System.Boolean | 0 | `0x7FFF7A448140` | `0x5F8140` |
| `Awake` | System.Void | 0 | `0x7FFF7A447830` | `0x5F7830` |
| `Find(T&)` | System.Void | 1 | `0x7FFF7A1211E0` | `0x2D11E0` |
| `FindAndInit(T&)` | System.Void | 1 | `0x7FFF7A1211E0` | `0x2D11E0` |
| `OnLevelLoaded(System.Boolean)` | System.Void | 1 | `0x7FFF7A447E70` | `0x5F7E70` |
| `RunStaticRoutine(System.Collections.IEnumerator)` | System.Void | 1 | `0x7FFF7A448040` | `0x5F8040` |
| `PrepareUnload` | System.Void | 0 | `0x7FFF7A447FD0` | `0x5F7FD0` |
| `DisableAnalytics` | System.Void | 0 | `0x7FFF7A447E30` | `0x5F7E30` |
| `.ctor` | System.Void | 0 | `0x7FFF7A448050` | `0x5F8050` |

### A.19 SingletonMonoBehaviour`1 / `2

`[CLASS] Coatsink.Common.SingletonMonoBehaviour`1 : UnityEngine.MonoBehaviour size=0x0`

| field | type | off | note |
|---|---|---|---|
| `_Inst` | T | 0x0000 | static |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_Inst` | T | 0 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `get_InstExists` | System.Boolean | 0 | `0x7FFF7A126D00` | `0x2D6D00` |
| `Awake` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `OnDestroy` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `.ctor` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |

### A.20 SingletonMonoBehaviour`2

`[CLASS] Coatsink.Common.SingletonMonoBehaviour`2 : UnityEngine.MonoBehaviour size=0x0`

| field | type | off | note |
|---|---|---|---|
| `_Inst` | BT | 0x0000 | static |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `get_Inst` | IT | 0 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `get_InstExists` | System.Boolean | 0 | `0x7FFF7A126D00` | `0x2D6D00` |
| `Awake` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `OnDestroy` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `.ctor` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |

### A.21 Dictionary`2 字段

`[CLASS] System.Collections.Generic.Dictionary`2 : System.Object size=0x0`

| field | type | off | note |
|---|---|---|---|
| `_buckets` | System.Int32[] | 0x0000 |  |
| `_entries` | System.Collections.Generic.Dictionary.Entry<TKey,TValue>[] | 0x0000 |  |
| `_count` | System.Int32 | 0x0000 |  |
| `_freeList` | System.Int32 | 0x0000 |  |
| `_freeCount` | System.Int32 | 0x0000 |  |
| `_version` | System.Int32 | 0x0000 |  |
| `_comparer` | System.Collections.Generic.IEqualityComparer<TKey> | 0x0000 |  |
| `_keys` | System.Collections.Generic.Dictionary.KeyCollection<TKey,TValue> | 0x0000 |  |
| `_values` | System.Collections.Generic.Dictionary.ValueCollection<TKey,TValue> | 0x0000 |  |
| `_syncRoot` | System.Object | 0x0000 |  |

| method | ret | argc | p0 | RVA |
|---|---|---|---|---|
| `.ctor` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `.ctor(System.Int32)` | System.Void | 1 | `0x7FFF7A121D00` | `0x2D1D00` |
| `.ctor(System.Collections.Generic.IEqualityComparer<TKey>)` | System.Void | 1 | `0x7FFF7A1211E0` | `0x2D11E0` |
| `.ctor(System.Int32,System.Collections.Generic.IEqualityComparer<TKey>)` | System.Void | 2 | `0x7FFF7A1235B0` | `0x2D35B0` |
| `.ctor(System.Collections.Generic.IDictionary<TKey,TValue>)` | System.Void | 1 | `0x7FFF7A1211E0` | `0x2D11E0` |
| `.ctor(System.Collections.Generic.IDictionary<TKey,TValue>,System.Collections.Generic.IEqualityComparer<TKey>)` | System.Void | 2 | `0x7FFF7A12C1A0` | `0x2DC1A0` |
| `.ctor(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` | System.Void | 2 | `0x0` | `0x-7FFF79E50000` |
| `get_Count` | System.Int32 | 0 | `0x7FFF7A128B70` | `0x2D8B70` |
| `get_Keys` | System.Collections.Generic.Dictionary.KeyCollection<TKey,TValue> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.Generic.IDictionary<TKey,TValue>.get_Keys` | System.Collections.Generic.ICollection<TKey> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.Generic.IReadOnlyDictionary<TKey,TValue>.get_Keys` | System.Collections.Generic.IEnumerable<TKey> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `get_Values` | System.Collections.Generic.Dictionary.ValueCollection<TKey,TValue> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.Generic.IDictionary<TKey,TValue>.get_Values` | System.Collections.Generic.ICollection<TValue> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.Generic.IReadOnlyDictionary<TKey,TValue>.get_Values` | System.Collections.Generic.IEnumerable<TValue> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `get_Item(TKey)` | TValue | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `set_Item(TKey,TValue)` | System.Void | 2 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `Add(TKey,TValue)` | System.Void | 2 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `System.Collections.Generic.ICollection<System.Collections.Generic.KeyValuePair<TKey,TValue>>.Add(System.Collections.Generic.KeyValuePair<TKey,TValue>)` | System.Void | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `System.Collections.Generic.ICollection<System.Collections.Generic.KeyValuePair<TKey,TValue>>.Contains(System.Collections.Generic.KeyValuePair<TKey,TValue>)` | System.Boolean | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `System.Collections.Generic.ICollection<System.Collections.Generic.KeyValuePair<TKey,TValue>>.Remove(System.Collections.Generic.KeyValuePair<TKey,TValue>)` | System.Boolean | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `Clear` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `ContainsKey(TKey)` | System.Boolean | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `ContainsValue(TValue)` | System.Boolean | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `CopyTo(System.Collections.Generic.KeyValuePair<TKey,TValue>[],System.Int32)` | System.Void | 2 | `0x7FFF7A12A160` | `0x2DA160` |
| `GetEnumerator` | System.Collections.Generic.Dictionary.Enumerator<TKey,TValue> | 0 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `System.Collections.Generic.IEnumerable<System.Collections.Generic.KeyValuePair<TKey,TValue>>.GetEnumerator` | System.Collections.Generic.IEnumerator<System.Collections.Generic.KeyValuePair<TKey,TValue>> | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `GetObjectData(System.Runtime.Serialization.SerializationInfo,System.Runtime.Serialization.StreamingContext)` | System.Void | 2 | `0x0` | `0x-7FFF79E50000` |
| `FindEntry(TKey)` | System.Int32 | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `Initialize(System.Int32)` | System.Int32 | 1 | `0x7FFF7A130710` | `0x2E0710` |
| `TryInsert(TKey,TValue,System.Collections.Generic.InsertionBehavior)` | System.Boolean | 3 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `OnDeserialization(System.Object)` | System.Void | 1 | `0x7FFF7A1211E0` | `0x2D11E0` |
| `Resize` | System.Void | 0 | `0x7FFF7A134F80` | `0x2E4F80` |
| `Resize(System.Int32,System.Boolean)` | System.Void | 2 | `0x0` | `0x-7FFF79E50000` |
| `Remove(TKey)` | System.Boolean | 1 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `Remove(TKey,TValue&)` | System.Boolean | 2 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `TryGetValue(TKey,TValue&)` | System.Boolean | 2 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `TryAdd(TKey,TValue)` | System.Boolean | 2 | `0x7FFF7A235DD0` | `0x3E5DD0` |
| `System.Collections.Generic.ICollection<System.Collections.Generic.KeyValuePair<TKey,TValue>>.get_IsReadOnly` | System.Boolean | 0 | `0x7FFF7A126CD0` | `0x2D6CD0` |
| `System.Collections.Generic.ICollection<System.Collections.Generic.KeyValuePair<TKey,TValue>>.CopyTo(System.Collections.Generic.KeyValuePair<TKey,TValue>[],System.Int32)` | System.Void | 2 | `0x7FFF7A12A160` | `0x2DA160` |
| `System.Collections.ICollection.CopyTo(System.Array,System.Int32)` | System.Void | 2 | `0x7FFF7A12A160` | `0x2DA160` |
| `System.Collections.IEnumerable.GetEnumerator` | System.Collections.IEnumerator | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `EnsureCapacity(System.Int32)` | System.Int32 | 1 | `0x7FFF7A130710` | `0x2E0710` |
| `System.Collections.ICollection.get_IsSynchronized` | System.Boolean | 0 | `0x7FFF7A126CD0` | `0x2D6CD0` |
| `System.Collections.ICollection.get_SyncRoot` | System.Object | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.IDictionary.get_IsFixedSize` | System.Boolean | 0 | `0x7FFF7A126CD0` | `0x2D6CD0` |
| `System.Collections.IDictionary.get_IsReadOnly` | System.Boolean | 0 | `0x7FFF7A126CD0` | `0x2D6CD0` |
| `System.Collections.IDictionary.get_Keys` | System.Collections.ICollection | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.IDictionary.get_Values` | System.Collections.ICollection | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.IDictionary.get_Item(System.Object)` | System.Object | 1 | `0x7FFF7A123930` | `0x2D3930` |
| `System.Collections.IDictionary.set_Item(System.Object,System.Object)` | System.Void | 2 | `0x7FFF7A12C1A0` | `0x2DC1A0` |
| `IsCompatibleKey(System.Object)` | System.Boolean | 1 | `0x7FFF7A11FBA0` | `0x2CFBA0` |
| `System.Collections.IDictionary.Add(System.Object,System.Object)` | System.Void | 2 | `0x7FFF7A12C1A0` | `0x2DC1A0` |
| `System.Collections.IDictionary.Contains(System.Object)` | System.Boolean | 1 | `0x7FFF7A126D70` | `0x2D6D70` |
| `System.Collections.IDictionary.GetEnumerator` | System.Collections.IDictionaryEnumerator | 0 | `0x7FFF7A123580` | `0x2D3580` |
| `System.Collections.IDictionary.Remove(System.Object)` | System.Void | 1 | `0x7FFF7A1211E0` | `0x2D11E0` |

