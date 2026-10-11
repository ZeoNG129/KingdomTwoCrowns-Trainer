// ktc_mod.dll - Kingdom Two Crowns memory trainer (injected, memory-only)
// Nothing is ever written to the game directory.
#include "../common/il2cpp_api.h"
#include "../common/ktc_cfg.h"
#include "../../third_party/minhook-master/include/MinHook.h"
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <string>
#include <vector>
#include <unordered_set>
#include <io.h>
#include <fcntl.h>
#include <windows.h>

// ==================================================================== config
static const char* FEAT_NAME[F_COUNT] = {
    "add10coins", "add10gems", "noConsume", "mountBoost", "subjectsInvincible",
    "crownSafe", "instantBuild", "buildingsInvincible", "fastSpawn", "gameSpeed"
};

static HANDLE g_map = nullptr;
static KtcCfg g_local{};
static KtcCfg* C = &g_local;

static void InitSharedCfg() {
    g_map = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(KtcCfg), KTC_CFG_NAME);
    if (g_map) {
        void* p = MapViewOfFile(g_map, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KtcCfg));
        if (p) {
            C = (KtcCfg*)p;
            if (C->magic != KTC_CFG_MAGIC) {
                memset(C, 0, sizeof(KtcCfg));
                C->magic = KTC_CFG_MAGIC;
                C->version = KTC_CFG_VER;
                C->gameSpeed = 1.0f;
                C->runMultiplier = 2.2f;
                C->hotkeysEnabled = 1;
            }
        } else {
            CloseHandle(g_map);
            g_map = nullptr;
        }
    }
}

// ==================================================================== logging
static FILE* g_log = nullptr;
static char  g_root[MAX_PATH] = {0};
static char  g_cmdPath[MAX_PATH] = {0};
#define LOG(...) do { if (g_log) { fprintf(g_log, __VA_ARGS__); fflush(g_log); } } while (0)

// ==================================================================== safe memory
static bool Plausible(const void* p) {
    uintptr_t v = (uintptr_t)p;
    return v > 0x10000 && v < 0x00007FFFFFFFFFFFULL && (v & 3) == 0;
}
static bool Readable(const void* p, size_t n) {
    if (!Plausible(p)) return false;
    MEMORY_BASIC_INFORMATION mbi{};
    if (!VirtualQuery(p, &mbi, sizeof(mbi))) return false;
    if (mbi.State != MEM_COMMIT) return false;
    if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
    return ((uintptr_t)p + n) <= ((uintptr_t)mbi.BaseAddress + mbi.RegionSize);
}
template <class T> static T    R(void* base, size_t off) { return *(T*)((char*)base + off); }
template <class T> static void W(void* base, size_t off, T v) { *(T*)((char*)base + off) = v; }
static void* ObjKlass(void* obj) { return obj ? *(void**)obj : nullptr; }
static const char* KlassName(void* k) { return k ? i2c_class_get_name((Il2CppClass*)k) : "?"; }

// ==================================================================== il2cpp helpers
static Il2CppImage* g_imgGame = nullptr;
static Il2CppImage* g_imgCore = nullptr;

static Il2CppImage* FindImage(const char* name) {
    size_t n = 0;
    const Il2CppAssembly** asms = i2c_domain_get_assemblies(i2c_domain_get(), &n);
    for (size_t i = 0; i < n; i++) {
        Il2CppImage* im = i2c_assembly_get_image(asms[i]);
        const char* nm = i2c_image_get_name(im);
        if (nm && strcmp(nm, name) == 0) return im;
    }
    return nullptr;
}
static Il2CppClass* Cls(Il2CppImage* img, const char* ns, const char* name) {
    return img ? i2c_class_from_name(img, ns, name) : nullptr;
}
static size_t FOff(Il2CppClass* k, const char* name) {
    if (!k) return (size_t)-1;
    FieldInfo* f = i2c_class_get_field_from_name(k, name);
    return f ? i2c_field_get_offset(f) : (size_t)-1;
}
static size_t FOffOf(void* obj, const char* name) {
    return obj ? FOff((Il2CppClass*)ObjKlass(obj), name) : (size_t)-1;
}
static MethodInfo* Meth(Il2CppClass* k, const char* name, int argc) {
    return k ? i2c_class_get_method_from_name(k, name, argc) : nullptr;
}
static void* MethPtr(MethodInfo* m) { return m ? *(void**)m : nullptr; }
static bool ReadStatic(Il2CppClass* k, const char* name, void* out) {
    if (!k) return false;
    FieldInfo* f = i2c_class_get_field_from_name(k, name);
    if (!f) return false;
    i2c_field_static_get_value(f, out);
    return true;
}
static bool WriteStatic(Il2CppClass* k, const char* name, void* val) {
    if (!k) return false;
    FieldInfo* f = i2c_class_get_field_from_name(k, name);
    if (!f) return false;
    i2c_field_static_set_value(f, val);
    return true;
}
static size_t ArrLen(void* arr) { return arr ? i2c_array_length((Il2CppArray*)arr) : 0; }
static void*  ArrAt(void* arr, size_t i) { return arr ? *(void**)((char*)arr + 0x20 + i * 8) : nullptr; }

// call a managed method (must be on the main thread!)
static Il2CppObject* Invoke(MethodInfo* m, void* self, void** args) {
    if (!m) return nullptr;
    Il2CppObject* exc = nullptr;
    return i2c_runtime_invoke(m, self, args, &exc);
}

// ==================================================================== game refs
struct GameRefs {
    Il2CppClass* kingdom = nullptr;
    Il2CppClass* player = nullptr;
    Il2CppClass* wallet = nullptr;
    Il2CppClass* steed = nullptr;
    Il2CppClass* damageable = nullptr;
    Il2CppClass* character = nullptr;
    Il2CppClass* beggarCamp = nullptr;
    Il2CppClass* construction = nullptr;
    Il2CppClass* wall = nullptr;
    Il2CppClass* objCls = nullptr;     // UnityEngine.Object
    Il2CppClass* timeCls = nullptr;    // UnityEngine.Time
    Il2CppClass* droppableRegistrar = nullptr;

    size_t k_players, k_chars, k_walls, k_towers, k_camps, k_steeds, k_farmlands, k_farmhouses;
    size_t p_wallet, p_damageable, p_steed, p_mover, p_playerId, p_hasAuth;
    size_t w_currency, w_capacity;
    size_t s_stamina, s_walk, s_run;
    size_t d_hp, d_invuln, d_initialHP, d_playerRef;
    size_t c_damageable;
    size_t b_spawnInterval, b_maxBeggars, b_hunger;
    size_t cc_buildPoints, cc_current, cc_hasStarted;
    size_t cc_scaffolding = (size_t)-1;   // Scaffolding reference (cleared once built)
    size_t wl_damageable;   // Wall._damageable

    MethodInfo* m_setCoins = nullptr;
    MethodInfo* m_setGems = nullptr;
    MethodInfo* m_addCurrency = nullptr;   // Wallet.AddCurrency(CurrencyType,int)
    MethodInfo* m_findAll = nullptr;
    MethodInfo* m_setTimeScale = nullptr;
    MethodInfo* m_forceComplete = nullptr;
    MethodInfo* m_overrideBuildPts = nullptr;
    MethodInfo* m_updateBuildPts = nullptr;
    MethodInfo* m_kUpdate = nullptr;   // Kingdom.Update (tick source)
    MethodInfo* m_loseCrown = nullptr;
    MethodInfo* m_receiveDamage = nullptr;
    MethodInfo* m_receiveFatal = nullptr;
    MethodInfo* m_constructionComplete = nullptr;
    MethodInfo* m_constrAwake = nullptr;
    MethodInfo* m_constrInit = nullptr;
    // large multi-part builds (castle / boat upgrades) use AssembledBuildProject
    Il2CppClass* buildPart = nullptr;
    MethodInfo*  m_updateWork = nullptr;
    size_t bp_current = (size_t)-1, bp_total = (size_t)-1;
    Il2CppClass* beggarSM = nullptr;         // BeggarCamp's <SlowUpdate>d__13 coroutine state machine
    MethodInfo*  m_beggarMoveNext = nullptr;
    bool ok = false;
};
static GameRefs G;

static bool ResolveGame() {
    if (G.ok) return true;
    if (!g_imgGame) g_imgGame = FindImage("Assembly-CSharp.dll");
    if (!g_imgGame) return false;
    g_imgCore = FindImage("UnityEngine.CoreModule.dll");

    G.kingdom  = Cls(g_imgGame, "", "Kingdom");
    G.player   = Cls(g_imgGame, "", "Player");
    G.wallet   = Cls(g_imgGame, "", "Wallet");
    G.steed    = Cls(g_imgGame, "", "Steed");
    G.damageable = Cls(g_imgGame, "", "Damageable");
    G.character  = Cls(g_imgGame, "", "Character");
    G.beggarCamp = Cls(g_imgGame, "", "BeggarCamp");
    G.construction = Cls(g_imgGame, "", "ConstructionBuildingComponent");
    G.wall     = Cls(g_imgGame, "", "Wall");
    G.droppableRegistrar = Cls(g_imgGame, "", "DroppableRegistrar");
    G.objCls   = Cls(g_imgCore, "UnityEngine", "Object");
    G.timeCls  = Cls(g_imgCore, "UnityEngine", "Time");

    if (!G.kingdom || !G.player || !G.wallet || !G.damageable || !G.kingdom) return false;

    G.k_players  = FOff(G.kingdom, "_activePlayers");
    G.k_chars    = FOff(G.kingdom, "_characters");
    G.k_walls    = FOff(G.kingdom, "_walls");
    G.k_towers   = FOff(G.kingdom, "_towers");
    G.k_camps    = FOff(G.kingdom, "<BeggarCamps>k__BackingField");
    G.k_steeds   = FOff(G.kingdom, "spawnedSteeds");
    G.k_farmlands= FOff(G.kingdom, "Farmlands");
    G.k_farmhouses = FOff(G.kingdom, "_farmHouses");

    G.p_wallet     = FOff(G.player, "<wallet>k__BackingField");
    G.p_damageable = FOff(G.player, "<damageable>k__BackingField");
    G.p_steed      = FOff(G.player, "_steed");
    G.p_mover      = FOff(G.player, "<mover>k__BackingField");
    G.p_playerId   = FOff(G.player, "playerId");
    G.p_hasAuth    = FOff(G.player, "hasLocalAuthority");

    G.w_currency = FOff(G.wallet, "_currencyAmount");
    G.w_capacity = FOff(G.wallet, "TotalCapacity");

    G.s_stamina = FOff(G.steed, "<Stamina>k__BackingField");
    G.s_walk    = FOff(G.steed, "walkSpeed");
    G.s_run     = FOff(G.steed, "runSpeed");

    G.d_hp        = FOff(G.damageable, "_hitPoints");
    G.d_invuln    = FOff(G.damageable, "_isInvulnerable");
    G.d_initialHP = FOff(G.damageable, "initialHitPoints");
    G.d_playerRef = FOff(G.damageable, "_playerRef");

    G.c_damageable = FOff(G.character, "_damageable");

    G.b_spawnInterval = FOff(G.beggarCamp, "spawnInterval");
    G.b_maxBeggars    = FOff(G.beggarCamp, "maxBeggars");
    G.b_hunger        = FOff(G.beggarCamp, "hungerInterval");

    G.cc_buildPoints = FOff(G.construction, "_buildPoints");
    G.cc_current     = FOff(G.construction, "_currentBuildPoints");
    G.cc_hasStarted  = FOff(G.construction, "_hasStarted");
    G.cc_scaffolding = FOff(G.construction, "_scaffolding");

    G.wl_damageable = FOff(G.wall, "_damageable");

    G.m_setCoins = Meth(G.wallet, "set_Coins", 1);
    G.m_setGems  = Meth(G.wallet, "set_Gems", 1);
    G.m_addCurrency = Meth(G.wallet, "AddCurrency", 2);
    // UnityEngine.Object.FindObjectsOfType(Type)  /  UnityEngine.Resources.FindObjectsOfTypeAll(Type)
    G.m_findAll = G.objCls ? Meth(G.objCls, "FindObjectsOfType", 1) : nullptr;
    if (!G.m_findAll) {
        Il2CppClass* resCls = Cls(g_imgCore, "UnityEngine", "Resources");
        G.m_findAll = resCls ? Meth(resCls, "FindObjectsOfTypeAll", 1) : nullptr;
    }
    G.m_setTimeScale = G.timeCls ? Meth(G.timeCls, "set_timeScale", 1) : nullptr;
    G.m_forceComplete = Meth(G.construction, "ForceComplete", 0);
    G.m_overrideBuildPts = Meth(G.construction, "OverrideCurrentBuildPoints", 1);
    G.m_updateBuildPts = Meth(G.construction, "UpdateBuildPoints", 1);
    G.m_kUpdate = Meth(G.kingdom, "Update", 0);
    G.m_loseCrown = Meth(G.player, "LoseCrown", 3);
    G.m_receiveDamage = Meth(G.damageable, "ReceiveDamage", 3);
    G.m_receiveFatal = Meth(G.damageable, "ReceiveFatalDamage", 2);
    G.m_constructionComplete = Meth(G.construction, "ConstructionComplete", 1);
    G.m_constrAwake = Meth(G.construction, "Awake", 0);
    G.m_constrInit  = Meth(G.construction, "InitializeBuild", 0);

    G.buildPart = Cls(g_imgGame, "AssembledBuildProject", "AssembledBuildPart");
    if (G.buildPart) {
        G.m_updateWork = Meth(G.buildPart, "UpdateWork", 1);
        G.bp_current = FOff(G.buildPart, "_currentWorkAmount");
        G.bp_total   = FOff(G.buildPart, "_totalWorkRequired");
    }

    // BeggarCamp's coroutine state machine: the spawn interval / max count live inside
    // the generated <SlowUpdate>d__N state machine, so the instance fields alone are not enough.
    if (G.beggarCamp) {
        size_t nc = i2c_image_get_class_count(g_imgGame);
        for (size_t i = 0; i < nc; i++) {
            Il2CppClass* k = (Il2CppClass*)i2c_image_get_class(g_imgGame, i);
            const char* kn = i2c_class_get_name(k);
            if (!kn) continue;
            if (strncmp(kn, "<SlowUpdate>d__", 15) != 0) continue;
            if (i2c_class_get_declaring_type(k) != G.beggarCamp) continue;
            G.beggarSM = k;
            G.m_beggarMoveNext = Meth(k, "MoveNext", 0);
            break;
        }
    }

    G.ok = true;
    return true;
}

static void* KingdomInst() {
    if (!G.droppableRegistrar) return nullptr;
    void* k = nullptr;
    if (!ReadStatic(G.droppableRegistrar, "_kingdomRef", &k)) return nullptr;
    return Plausible(k) ? k : nullptr;
}

// ==================================================================== wallet
struct DictInfo { void* entries; int count; size_t entrySize; };
static bool GetDict(void* cm, DictInfo& di) {
    di = DictInfo{};
    if (!Plausible(cm)) return false;
    size_t offMap = FOffOf(cm, "_map");
    if (offMap == (size_t)-1) return false;
    void* dict = R<void*>(cm, offMap);
    if (!Plausible(dict)) return false;
    void* klass = ObjKlass(dict);
    size_t offEntries = FOff((Il2CppClass*)klass, "_entries");
    size_t offCount = FOff((Il2CppClass*)klass, "_count");
    if (offEntries == (size_t)-1 || offCount == (size_t)-1) return false;
    di.entries = R<void*>(dict, offEntries);
    di.count = R<int>(dict, offCount);
    if (!Plausible(di.entries) || di.count < 0 || di.count > 4096) return false;
    di.entrySize = i2c_array_element_size((Il2CppClass*)ObjKlass(di.entries));
    return di.entrySize > 0 && di.entrySize <= 64;
}
static int WalletGet(void* wallet, int type) {
    if (!Plausible(wallet) || G.w_currency == (size_t)-1) return -1;
    void* cm = R<void*>(wallet, G.w_currency);
    DictInfo di{};
    if (!GetDict(cm, di)) return -1;
    for (int i = 0; i < di.count; i++) {
        char* e = (char*)di.entries + 0x20 + (size_t)i * di.entrySize;
        if (*(int*)(e + 0) < 0) continue;
        if (*(int*)(e + 8) == type) return *(int*)(e + 12);
    }
    return 0;
}

// ==================================================================== protection set
static std::unordered_set<void*> g_protected;   // Damageable* that must not take damage
static std::unordered_set<void*> g_touchedSubjects, g_touchedStruct, g_touchedCrown;
static volatile LONG g_protectActive = 0;

static bool IsLiveDamageable(void* d) {
    return Plausible(d) && G.d_invuln != (size_t)-1 && Readable(d, 16) && ObjKlass(d) == G.damageable;
}
static void MarkInvuln(void* d, std::unordered_set<void*>& touched) {
    if (!IsLiveDamageable(d)) return;
    W<uint8_t>(d, G.d_invuln, 1);
    g_protected.insert(d);
    touched.insert(d);
}
static void ResetInvuln(std::unordered_set<void*>& touched) {
    for (void* d : touched) if (IsLiveDamageable(d)) W<uint8_t>(d, G.d_invuln, 0);
    touched.clear();
}

// ==================================================================== hooks
typedef void (*tKingdomUpdate)(void* self, void* method);
static tKingdomUpdate oKingdomUpdate = nullptr;

typedef void (*tLoseCrown)(void* self, float vx, float vy, bool a, bool b, void* method);
static tLoseCrown oLoseCrown = nullptr;

typedef void (*tReceiveDamage)(void* self, int amount, void* source, int dmgSource, void* method);
static tReceiveDamage oReceiveDamage = nullptr;

typedef void (*tReceiveFatal)(void* self, void* source, int dmgSource, void* method);
static tReceiveFatal oReceiveFatal = nullptr;

static void Tick(bool fromHook);

static void hkKingdomUpdate(void* self, void* method) {
    Tick(true);
    if (oKingdomUpdate) oKingdomUpdate(self, method);
}

static void hkLoseCrown(void* self, float vx, float vy, bool a, bool b, void* method) {
    if (C->features[F_CROWN]) { LOG("[hook] LoseCrown blocked\n"); return; }
    if (oLoseCrown) oLoseCrown(self, vx, vy, a, b, method);
}

static void hkReceiveDamage(void* self, int amount, void* source, int dmgSource, void* method) {
    if (g_protectActive && g_protected.count(self)) return;
    if (oReceiveDamage) oReceiveDamage(self, amount, source, dmgSource, method);
}

static void hkReceiveFatal(void* self, void* source, int dmgSource, void* method) {
    if (g_protectActive && g_protected.count(self)) return;
    if (oReceiveFatal) oReceiveFatal(self, source, dmgSource, method);
}

// ConstructionBuildingComponent.ConstructionComplete(bool) - the game's own "this building
// is finished" path. Logged so we can prove the completion logic really ran.
typedef void (*tConstrComplete)(void* self, bool ok, void* method);
static tConstrComplete oConstrComplete = nullptr;
static volatile LONG g_constrCompleteCalls = 0;
static void hkConstrComplete(void* self, bool ok, void* method) {
    LONG n = InterlockedIncrement(&g_constrCompleteCalls);
    if (n <= 20) LOG("[build] game ConstructionComplete(%d) on %p  (call #%ld)\n", ok ? 1 : 0, self, n);
    if (oConstrComplete) oConstrComplete(self, ok, method);
}

// ConstructionBuildingComponent.Awake / InitializeBuild - register every construction site
// the moment it is created. This catches sites whose GameObject is still inactive while
// waiting for a builder (Object.FindObjectsOfType would never see those).
static std::vector<std::pair<void*, uint64_t>> g_newSites;
typedef void (*tConstrAwake)(void* self, void* method);
static tConstrAwake oConstrAwake = nullptr;
static tConstrAwake oConstrInitBuild = nullptr;
static volatile LONG g_sitesRegistered = 0;

static void RegisterSite(void* self) {
    if (!Plausible(self)) return;
    for (auto& e : g_newSites) if (e.first == self) return;
    if (g_newSites.size() > 512) g_newSites.erase(g_newSites.begin());
    g_newSites.push_back({ self, GetTickCount64() });
    InterlockedIncrement(&g_sitesRegistered);
}
static void hkConstrAwake(void* self, void* method) {
    RegisterSite(self);
    if (oConstrAwake) oConstrAwake(self, method);
}
static void hkConstrInitBuild(void* self, void* method) {
    RegisterSite(self);
    if (oConstrInitBuild) oConstrInitBuild(self, method);
}

// BeggarCamp's SlowUpdate coroutine. The generated state machine caches maxBeggars /// spawnInterval in its own fields, so patching the camp instance has no effect - the
// values must be forced here, every time the coroutine body runs.
typedef void (*tBeggarMoveNext)(void* self, void* method);
static tBeggarMoveNext oBeggarMoveNext = nullptr;
static volatile LONG g_campHookHits = 0;
static volatile LONG g_campHookCampFound = 0;
static void* g_campFromSM = nullptr;

static void hkBeggarMoveNext(void* self, void* method) {
    if (C->features[F_SPAWN]) {
        LONG hits = InterlockedIncrement(&g_campHookHits);
        // The state machine keeps the owning BeggarCamp in its <>4__this field
        // (offset 0x20 in this build) - force the camp's own fields through it.
        // NOTE: never write to 0x20/0x24 here, that would corrupt the camp pointer.
        void* camp = nullptr;
        for (size_t off = 0x10; off <= 0x28; off += 8) {
            void* p = R<void*>(self, off);
            if (Plausible(p) && ObjKlass(p) == G.beggarCamp) { camp = p; break; }
        }
        if (camp) {
            g_campFromSM = camp;
            InterlockedIncrement(&g_campHookCampFound);
            if (hits <= 6)
                LOG("[camp] MoveNext hit %ld self=%p camp=%p interval=%.2f max=%d hunger=%.1f\n",
                    hits, self, camp, R<float>(camp, G.b_spawnInterval),
                    R<int>(camp, G.b_maxBeggars), R<float>(camp, G.b_hunger));
            if (G.b_maxBeggars != (size_t)-1)    W<int>(camp, G.b_maxBeggars, 20);
            if (G.b_spawnInterval != (size_t)-1) W<float>(camp, G.b_spawnInterval, 0.1f);
            if (G.b_hunger != (size_t)-1)        W<float>(camp, G.b_hunger, 600.0f);
        } else if (hits <= 6) {
            LOG("[camp] MoveNext hit %ld self=%p - camp not found in state machine\n", hits, self);
        }
    }
    if (oBeggarMoveNext) oBeggarMoveNext(self, method);
}

static bool InstallHook(const char* label, MethodInfo* m, void* detour, void** orig) {
    void* target = MethPtr(m);
    if (!target) { LOG("[-] hook %s: no method pointer\n", label); return false; }
    MH_STATUS st = MH_CreateHook(target, detour, orig);
    if (st != MH_OK) { LOG("[-] hook %s: MH_CreateHook=%d\n", label, (int)st); return false; }
    st = MH_EnableHook(target);
    if (st != MH_OK) { LOG("[-] hook %s: MH_EnableHook=%d\n", label, (int)st); return false; }
    LOG("[+] hook %s installed at %p\n", label, target);
    return true;
}

// ==================================================================== features state
struct MountSave { void* steed; float walk, run; };
static std::vector<MountSave> g_mountSaved;
struct CampSave { void* camp; float interval; int maxB; float hunger; };
static std::vector<CampSave> g_campSaved;
static long g_floorCoins = -1, g_floorGems = -1;
static int  g_speedIdx = 0;
static volatile LONG g_myGen = 0;
static volatile LONG g_shutdown = 0;
static const float SPEEDS[] = {1.0f, 1.5f, 2.0f, 3.0f, 5.0f, 8.0f};
static const int   SPEED_N = 6;

static void RestoreMount() {
    for (auto& s : g_mountSaved) {
        if (Plausible(s.steed)) { W<float>(s.steed, G.s_walk, s.walk); W<float>(s.steed, G.s_run, s.run); }
    }
    g_mountSaved.clear();
}
static void RestoreCamps() {
    for (auto& c : g_campSaved) {
        if (Plausible(c.camp)) {
            W<float>(c.camp, G.b_spawnInterval, c.interval);
            W<int>(c.camp, G.b_maxBeggars, c.maxB);
            W<float>(c.camp, G.b_hunger, c.hunger);
        }
    }
    g_campSaved.clear();
}

// iterate a HashSet<T> living in a Kingdom field
template <class Fn>
static void ForEachSet(void* kingdom, size_t fieldOff, Fn fn) {
    if (fieldOff == (size_t)-1 || !Plausible(kingdom)) return;
    void* set = R<void*>(kingdom, fieldOff);
    if (!Plausible(set)) return;
    void* klass = ObjKlass(set);
    size_t offSlots = FOff((Il2CppClass*)klass, "_slots");
    if (offSlots == (size_t)-1) offSlots = FOff((Il2CppClass*)klass, "_entries");
    size_t offCount = FOff((Il2CppClass*)klass, "_count");
    if (offSlots == (size_t)-1 || offCount == (size_t)-1) return;
    void* slots = R<void*>(set, offSlots);
    int count = R<int>(set, offCount);
    if (!Plausible(slots) || count <= 0 || count > 20000) return;
    size_t esz = i2c_array_element_size((Il2CppClass*)ObjKlass(slots));
    if (!esz || esz > 64) return;
    for (int i = 0; i < count; i++) {
        char* e = (char*)slots + 0x20 + (size_t)i * esz;
        if (*(int*)(e + 0) < 0) continue;
        void* val = *(void**)(e + 8);
        if (Plausible(val)) fn(val);
    }
}

// FindObjectsOfTypeAll -> array of managed objects (main thread only)
static void* FindAll(Il2CppClass* klass) {
    if (!G.m_findAll || !klass) return nullptr;
    Il2CppObject* typeObj = i2c_type_get_object(i2c_class_get_type(klass));
    if (!typeObj) return nullptr;
    void* args[1] = { typeObj };
    return Invoke(G.m_findAll, nullptr, args);
}

// ==================================================================== tick
static uint64_t g_lastHeavy = 0, g_lastTick = 0;
static void* g_constructCache = nullptr;
static void* g_buildPartCache = nullptr;
static uint64_t g_lastPartRefresh = 0;
static volatile LONG g_reqTestBuild = 0;
// main-thread statistics snapshot (read by the command handler / GUI)
static volatile LONG g_statTotal = 0, g_statPending = 0, g_statComplete = 0;
static volatile float g_statTimeScale = -1.f;
static volatile LONG g_statSubjTotal = 0, g_statSubjInv = 0, g_statWallTotal = 0, g_statWallInv = 0;
static volatile LONG g_statCampCount = 0;
static volatile float g_statCampInterval = -1.f;

static void SetCoins(void* wallet, int v) { void* a[1] = { &v }; Invoke(G.m_setCoins, wallet, a); }
static void SetGems(void* wallet, int v)  { void* a[1] = { &v }; Invoke(G.m_setGems, wallet, a); }
// Wallet.AddCurrency(CurrencyType, int) - the game's own "player received coins" path
static void AddCurrency(void* wallet, int type, int amount) {
    if (!Plausible(wallet) || !G.m_addCurrency || amount == 0) return;
    void* a[2] = { &type, &amount };
    Invoke(G.m_addCurrency, wallet, a);
}

// Finish one construction site through the game's own code path.
// Returns 1 when the site was actually pending and got completed. Main thread only.
static int CompleteSite(void* cc) {
    if (!Plausible(cc) || ObjKlass(cc) != G.construction) return 0;
    if (G.cc_current == (size_t)-1 || G.cc_buildPoints == (size_t)-1) return 0;
    float cur = R<float>(cc, G.cc_current);
    int   tgt = R<int>(cc, G.cc_buildPoints);
    if (tgt <= 0 || cur >= (float)tgt) return 0;

    // 1) mark as started so the game's own completion path is willing to run
    if (G.cc_hasStarted != (size_t)-1) W<uint8_t>(cc, G.cc_hasStarted, 1);

    // 2) push progress straight to the target through the game's own setter
    float t = (float)tgt;
    void* a1[1] = { &t };
    if (G.m_overrideBuildPts) Invoke(G.m_overrideBuildPts, cc, a1);
    if (R<float>(cc, G.cc_current) < (float)tgt) W<float>(cc, G.cc_current, t);

    // 3) the game's own "skip all hammering" entry point
    LONG before = g_constrCompleteCalls;
    if (G.m_forceComplete) Invoke(G.m_forceComplete, cc, nullptr);

    // 4) only when ForceComplete did not run the completion path, run it ourselves
    //    (exactly one completion per site - a second call would double the rewards)
    if (g_constrCompleteCalls == before && G.m_constructionComplete) {
        uint8_t arg = 1;
        void* a2[1] = { &arg };
        Invoke(G.m_constructionComplete, cc, a2);
        LOG("[build] site %p completed via ConstructionComplete fallback\n", cc);
    }
    LOG("[build] site %p forced complete (was %.1f / %d)\n", cc, cur, tgt);
    return 1;
}

static void Tick(bool fromHook) {
    (void)fromHook;
    if (C->generation != g_myGen) return;      // a newer instance took over
    uint64_t now = GetTickCount64();
    if (now - g_lastTick < 8) return;   // ~120Hz max
    g_lastTick = now;
    C->tickCount++;

    void* kingdom = KingdomInst();
    C->inGame = kingdom ? 1 : 0;
    if (!kingdom) { g_protectActive = 0; return; }

    // ---------------- players / wallet
    void* players = (G.k_players != (size_t)-1) ? R<void*>(kingdom, G.k_players) : nullptr;
    size_t nPlayers = ArrLen(players);
    void* wallet = nullptr;
    void* localPlayer = nullptr;
    for (size_t i = 0; i < nPlayers; i++) {
        void* p = ArrAt(players, i);
        if (!Plausible(p)) continue;
        if (!localPlayer) localPlayer = p;
        if (Plausible(p) && G.p_hasAuth != (size_t)-1 && R<uint8_t>(p, G.p_hasAuth)) localPlayer = p;
        if (Plausible(p) && !wallet) wallet = R<void*>(p, G.p_wallet);
    }
    if (Plausible(localPlayer)) {
        void* w2 = (G.p_wallet != (size_t)-1) ? R<void*>(localPlayer, G.p_wallet) : nullptr;
        if (Plausible(w2)) wallet = w2;
    }

    int coins = -1, gems = -1;
    if (Plausible(wallet)) {
        coins = WalletGet(wallet, 0);
        gems  = WalletGet(wallet, 1);
        C->coins = coins;
        C->gems = gems;
    } else { C->coins = C->gems = -1; }

    // ---- add coins / gems requests (game's own AddCurrency path)
    LONG rc = InterlockedExchange(&C->reqAddCoins, 0);
    if (rc > 0 && Plausible(wallet)) {
        AddCurrency(wallet, 0, (int)rc);
        coins = WalletGet(wallet, 0);
        C->coins = coins;
        LOG("[feat] +%ld coins -> %d\n", rc, coins);
    }
    LONG rg = InterlockedExchange(&C->reqAddGems, 0);
    if (rg > 0 && Plausible(wallet)) {
        AddCurrency(wallet, 1, (int)rg);
        gems = WalletGet(wallet, 1);
        C->gems = gems;
        LOG("[feat] +%ld gems -> %d\n", rg, gems);
    }

    // ---- empty the coin pouch (GUI button, no hotkey)
    if (InterlockedExchange(&C->reqClearWallet, 0) && Plausible(wallet)) {
        SetCoins(wallet, 0);
        SetGems(wallet, 0);
        coins = WalletGet(wallet, 0);
        gems  = WalletGet(wallet, 1);
        C->coins = coins; C->gems = gems;
        LOG("[feat] wallet cleared -> coins=%d gems=%d\n", coins, gems);
    }

    // ---- resources not consumed: Wallet.InfiniteMoney (the game's own debug flag).
    //      RemoveCurrencyAfterMoving() checks it before deducting on payment.
    if (Plausible(wallet)) {
        static int lastNoConsume = -1;
        int want = C->features[F_NOCONSUME] ? 1 : 0;
        if (want != lastNoConsume) {
            uint8_t v = (uint8_t)want;
            if (WriteStatic(G.wallet, "<InfiniteMoney>k__BackingField", &v)) {
                LOG("[feat] Wallet.InfiniteMoney = %d\n", want);
                lastNoConsume = want;
            }
        }
        C->status[F_NOCONSUME] = lastNoConsume > 0 ? 1 : 0;
    }
    g_floorCoins = coins; g_floorGems = gems;

    // ---------------- protection set (subjects + buildings + monarch)
    g_protected.clear();
    void* localDmg = (Plausible(localPlayer) && G.p_damageable != (size_t)-1)
                     ? R<void*>(localPlayer, G.p_damageable) : nullptr;

    if (C->features[F_SUBJECTS]) {
        ForEachSet(kingdom, G.k_chars, [](void* ch) {
            if (G.c_damageable == (size_t)-1) return;
            MarkInvuln(R<void*>(ch, G.c_damageable), g_touchedSubjects);
        });
        if (IsLiveDamageable(localDmg)) g_protected.insert(localDmg);
        C->status[F_SUBJECTS] = (LONG)g_touchedSubjects.size();
    } else if (!g_touchedSubjects.empty()) {
        ResetInvuln(g_touchedSubjects);
        LOG("[feat] subjects invulnerability cleared\n");
    }

    if (C->features[F_STRUCT]) {
        ForEachSet(kingdom, G.k_walls, [](void* wall) {
            if (G.wl_damageable == (size_t)-1) return;
            MarkInvuln(R<void*>(wall, G.wl_damageable), g_touchedStruct);
        });
        ForEachSet(kingdom, G.k_towers, [](void* t) {
            size_t off = FOffOf(t, "_damageable");
            if (off == (size_t)-1) off = FOffOf(t, "damageable");
            if (off == (size_t)-1) return;
            MarkInvuln(R<void*>(t, off), g_touchedStruct);
        });
        ForEachSet(kingdom, G.k_farmhouses, [](void* t) {
            size_t off = FOffOf(t, "_damageable");
            if (off == (size_t)-1) return;
            MarkInvuln(R<void*>(t, off), g_touchedStruct);
        });
        C->status[F_STRUCT] = (LONG)g_touchedStruct.size();
    } else if (!g_touchedStruct.empty()) {
        ResetInvuln(g_touchedStruct);
        LOG("[feat] building invulnerability cleared\n");
    }

    if (C->features[F_CROWN]) {
        MarkInvuln(localDmg, g_touchedCrown);
        C->status[F_CROWN] = 1;
    } else if (!g_touchedCrown.empty()) {
        ResetInvuln(g_touchedCrown);
        C->status[F_CROWN] = 0;
    }

    g_protectActive = g_protected.empty() ? 0 : 1;

    // ---------------- mount: infinite stamina + speed
    if (C->features[F_MOUNT]) {
        uint8_t inf = 1;
        WriteStatic(G.player, "DebugInfiniteStamina", &inf);
        void* steed = nullptr;
        if (Plausible(localPlayer) && G.p_steed != (size_t)-1) steed = R<void*>(localPlayer, G.p_steed);
        if (Plausible(steed)) {
            if (G.s_stamina != (size_t)-1) W<float>(steed, G.s_stamina, 1.0f);
            // speed boost (remember originals)
            float mult = C->runMultiplier > 0.1f ? C->runMultiplier : 2.2f;
            bool saved = false;
            for (auto& s : g_mountSaved) if (s.steed == steed) { saved = true; break; }
            if (!saved && G.s_walk != (size_t)-1 && G.s_run != (size_t)-1) {
                MountSave ms{ steed, R<float>(steed, G.s_walk), R<float>(steed, G.s_run) };
                g_mountSaved.push_back(ms);
                W<float>(steed, G.s_walk, ms.walk * mult);
                W<float>(steed, G.s_run, ms.run * mult);
                LOG("[feat] mount speed x%.2f (walk %.2f->%.2f run %.2f->%.2f)\n", mult,
                    ms.walk, ms.walk * mult, ms.run, ms.run * mult);
            } else if (saved) {
                for (auto& s : g_mountSaved) if (s.steed == steed) {
                    W<float>(steed, G.s_walk, s.walk * mult);
                    W<float>(steed, G.s_run, s.run * mult);
                }
            }
            // mover multiplier as well
            void* mover = (G.p_mover != (size_t)-1 && Plausible(localPlayer)) ? R<void*>(localPlayer, G.p_mover) : nullptr;
            if (Plausible(mover)) {
                size_t offM = FOffOf(mover, "_multiplier");
                size_t offT = FOffOf(mover, "_multiplierTimeout");
                if (offM != (size_t)-1) W<float>(mover, offM, mult);
                if (offT != (size_t)-1) W<float>(mover, offT, 5.0f);
            }
        }
    } else {
        if (!g_mountSaved.empty()) { RestoreMount(); LOG("[feat] mount speed restored\n"); }
        uint8_t inf = 0;
        WriteStatic(G.player, "DebugInfiniteStamina", &inf);
    }

    // ---------------- fast spawn (beggar camps)
    if (C->features[F_SPAWN]) {
        ForEachSet(kingdom, G.k_camps, [](void* camp) {
            bool known = false;
            for (auto& c : g_campSaved) if (c.camp == camp) { known = true; break; }
            if (!known && G.b_spawnInterval != (size_t)-1) {
                CampSave cs{ camp, R<float>(camp, G.b_spawnInterval), R<int>(camp, G.b_maxBeggars),
                             (G.b_hunger != (size_t)-1) ? R<float>(camp, G.b_hunger) : 0.f };
                g_campSaved.push_back(cs);
                LOG("[feat] camp %p spawnInterval %.2f -> 1.0, maxBeggars %d -> 12, hunger %.1f -> 600\n",
                    camp, cs.interval, cs.maxB, cs.hunger);
            }
            if (G.b_spawnInterval != (size_t)-1) W<float>(camp, G.b_spawnInterval, 1.0f);
            if (G.b_maxBeggars != (size_t)-1) W<int>(camp, G.b_maxBeggars, 12);
            if (G.b_hunger != (size_t)-1) W<float>(camp, G.b_hunger, 600.0f);
        });
    } else if (!g_campSaved.empty()) { RestoreCamps(); LOG("[feat] camps restored\n"); }

    // ---------------- instant build (runs on the main thread)
    if (now - g_lastHeavy > 500) {
        g_lastHeavy = now;
        if (G.m_findAll) g_constructCache = FindAll(G.construction);
        // refresh statistics snapshot
        int tot = 0, pend = 0;
        size_t n = ArrLen(g_constructCache);
        for (size_t i = 0; i < n && i < 800; i++) {
            void* cc = ArrAt(g_constructCache, i);
            if (!Plausible(cc) || ObjKlass(cc) != G.construction) continue;
            tot++;
            if (R<float>(cc, G.cc_current) < (float)R<int>(cc, G.cc_buildPoints)) pend++;
        }
        g_statTotal = tot; g_statPending = pend; g_statComplete = tot - pend;
        MethodInfo* gm = G.timeCls ? Meth(G.timeCls, "get_timeScale", 0) : nullptr;
        if (gm) {
            Il2CppObject* r = Invoke(gm, nullptr, nullptr);
            if (r) g_statTimeScale = *(float*)i2c_object_unbox(r);
        }
    }

    if (InterlockedExchange(&g_reqTestBuild, 0) && Plausible(g_constructCache)) {
        size_t n = ArrLen(g_constructCache);
        for (size_t i = 0; i < n && i < 800; i++) {
            void* cc = ArrAt(g_constructCache, i);
            if (!Plausible(cc) || ObjKlass(cc) != G.construction) continue;
            if (R<float>(cc, G.cc_current) >= (float)R<int>(cc, G.cc_buildPoints)) {
                // simulate a freshly placed, not-yet-started construction site
                W<float>(cc, G.cc_current, 0.0f);
                if (G.cc_hasStarted != (size_t)-1) W<uint8_t>(cc, G.cc_hasStarted, 0);
                LOG("[test] forced %p -> _currentBuildPoints=0, _hasStarted=0 (target %d)\n",
                    cc, R<int>(cc, G.cc_buildPoints));
                break;
            }
        }
    }

    if (C->features[F_BUILD]) {
        int done = 0;
        // a) sites registered by the Awake / InitializeBuild hooks (catches inactive objects)
        for (auto& e : g_newSites) done += CompleteSite(e.first);
        uint64_t now2 = GetTickCount64();
        for (size_t i = 0; i < g_newSites.size(); ) {
            if (now2 - g_newSites[i].second > 20000) g_newSites.erase(g_newSites.begin() + i);
            else i++;
        }
        // b) every construction component currently in the scene
        size_t n = ArrLen(g_constructCache);
        for (size_t i = 0; i < n && i < 800; i++) {
            void* cc = ArrAt(g_constructCache, i);
            if (!Plausible(cc) || ObjKlass(cc) != G.construction) continue;
            done += CompleteSite(cc);
        }
        // c) large multi-part builds (castle / boat upgrades): force each part's work to done
        if (G.buildPart && G.m_updateWork && Plausible(G.m_findAll)) {
            if (now2 - g_lastPartRefresh > 1500) {
                g_lastPartRefresh = now2;
                g_buildPartCache = FindAll(G.buildPart);
            }
            size_t np = ArrLen(g_buildPartCache);
            for (size_t i = 0; i < np && i < 400; i++) {
                void* part = ArrAt(g_buildPartCache, i);
                if (!Plausible(part) || ObjKlass(part) != G.buildPart) continue;
                int cur = R<int>(part, G.bp_current);
                int tot = R<int>(part, G.bp_total);
                if (tot > 0 && cur < tot) {
                    int delta = tot - cur;
                    void* a[1] = { &delta };
                    Invoke(G.m_updateWork, part, a);
                    done++;
                    LOG("[build] assembled part %p work %d/%d -> done\n", part, cur, tot);
                }
            }
        }
        C->status[F_BUILD] = done;
    } else {
        C->status[F_BUILD] = 0;
    }

    // ---------------- game speed
    if (C->features[F_SPEED]) {
        float sp = C->gameSpeed;
        if (sp < 0.1f) sp = 1.0f;
        if (sp > 8.0f) sp = 8.0f;
        void* a[1] = { &sp };
        Invoke(G.m_setTimeScale, nullptr, a);
        C->status[F_SPEED] = (LONG)(sp * 100);
    } else {
        static bool restored = true;
        if (!restored) {
            float sp = 1.0f; void* a[1] = { &sp };
            Invoke(G.m_setTimeScale, nullptr, a);
            restored = true;
            C->status[F_SPEED] = 100;
        }
    }
}

// ==================================================================== hotkeys
static void HandleHotkeys() {
    if (!C->hotkeysEnabled) return;
    static bool prev[32] = {0};
    struct { int vk; int idx; } keys[] = {
        {VK_F1, F_COINS}, {VK_F2, F_GEMS}, {VK_F3, F_NOCONSUME}, {VK_F4, F_MOUNT},
        {VK_F5, F_SUBJECTS}, {VK_F6, F_CROWN}, {VK_F7, F_BUILD}, {VK_F8, F_STRUCT},
        {VK_F9, F_SPAWN}, {VK_F10, F_SPEED},
    };
    for (auto& k : keys) {
        bool down = (GetAsyncKeyState(k.vk) & 0x8000) != 0;
        int slot = k.vk - VK_F1;
        if (down && !prev[slot]) {
            if (k.idx == F_COINS) { InterlockedIncrement(&C->reqAddCoins); C->reqAddCoins += 9; }
            else if (k.idx == F_GEMS) { InterlockedIncrement(&C->reqAddGems); C->reqAddGems += 9; }
            else if (k.idx == F_SPEED) {
                g_speedIdx = (g_speedIdx + 1) % SPEED_N;
                C->gameSpeed = SPEEDS[g_speedIdx];
                C->features[F_SPEED] = 1;
            } else {
                C->features[k.idx] = C->features[k.idx] ? 0 : 1;
            }
            LOG("[key] F%d -> %s\n", slot + 1, k.idx < F_COUNT ? FEAT_NAME[k.idx] : "?");
        }
        prev[slot] = down;
    }
    // F11: all off
    static bool prev11 = false;
    bool d11 = (GetAsyncKeyState(VK_F11) & 0x8000) != 0;
    if (d11 && !prev11) {
        for (int i = 0; i < F_COUNT; i++) C->features[i] = 0;
        C->gameSpeed = 1.0f; g_speedIdx = 0;
        LOG("[key] F11 -> all off\n");
    }
    prev11 = d11;
}

// ==================================================================== commands (test interface)
static std::string g_lastCmd;
static void HandleCommand(const std::string& cmd);

static void PollCommands() {
    if (!g_cmdPath[0]) return;
    HANDLE h = CreateFileA(g_cmdPath, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (h == INVALID_HANDLE_VALUE) return;
    char buf[4096] = {0};
    DWORD rd = 0;
    ReadFile(h, buf, sizeof(buf) - 1, &rd, nullptr);
    CloseHandle(h);
    std::string content(buf, rd);
    if (content.empty() || content == g_lastCmd) return;
    g_lastCmd = content;
    size_t pos = 0;
    while (pos <= content.size()) {
        size_t nl = content.find('\n', pos);
        std::string line = content.substr(pos, nl == std::string::npos ? std::string::npos : nl - pos);
        HandleCommand(line);
        if (nl == std::string::npos) break;
        pos = nl + 1;
    }
}

static void HandleCommand(const std::string& raw) {
    std::string cmd = raw;
    while (!cmd.empty() && (cmd.back() == '\r' || cmd.back() == ' ' || cmd.back() == '\t')) cmd.pop_back();
    size_t s = cmd.find_first_not_of(" \t");
    if (s == std::string::npos) return;
    cmd = cmd.substr(s);
    if (cmd[0] == '#') return;      // comment line
    LOG("[cmd] %s\n", cmd.c_str());

    char name[64] = {0};
    long val = 0;
    int n = sscanf_s(cmd.c_str(), "%63s %ld", name, (unsigned)sizeof(name), &val);
    std::string c = name;
    if (c == "dump") {
        LOG("--- state: coins=%ld gems=%ld inGame=%ld tick=%ld protect=%ld\n",
            C->coins, C->gems, C->inGame, C->tickCount, g_protectActive);
        for (int i = 0; i < F_COUNT; i++) LOG("    %-22s = %ld\n", FEAT_NAME[i], C->features[i]);
        LOG("    gameSpeed=%.2f runMult=%.2f\n", C->gameSpeed, C->runMultiplier);
        return;
    }
    if (c == "coins" && n == 2) { C->reqAddCoins += (LONG)val; LOG("  reqAddCoins += %ld\n", val); return; }
    if (c == "gems" && n == 2)  { C->reqAddGems  += (LONG)val; LOG("  reqAddGems += %ld\n", val); return; }
    if (c == "speed" && n == 2) { C->gameSpeed = (float)val; C->features[F_SPEED] = 1; return; }
    if (c == "runmult" && n == 2) { C->runMultiplier = (float)val; return; }
    if (c == "testbuild") { InterlockedExchange(&g_reqTestBuild, 1); LOG("  test-build requested\n"); return; }
    if (c == "clear") { InterlockedExchange(&C->reqClearWallet, 1); LOG("  wallet clear requested\n"); return; }
    if (c == "builds") {
        void* arr = Plausible(g_constructCache) ? g_constructCache : nullptr;
        LOG("buildings: cache=%p len=%zu total=%ld pending=%ld | sitesRegistered=%ld inQueue=%zu | completeCalls=%ld\n",
            arr, ArrLen(arr), g_statTotal, g_statPending, g_sitesRegistered, g_newSites.size(),
            g_constrCompleteCalls);
        LOG("  buildPartCls=%p updateWork=%p partsCached=%zu (cur off=%zd tot off=%zd)\n",
            (void*)G.buildPart, MethPtr(G.m_updateWork), ArrLen(g_buildPartCache),
            (ptrdiff_t)G.bp_current, (ptrdiff_t)G.bp_total);
        size_t n = ArrLen(arr);
        for (size_t i = 0; i < n && i < 30; i++) {
            void* cc = ArrAt(arr, i);
            if (!Plausible(cc) || ObjKlass(cc) != G.construction) continue;
            float cur = R<float>(cc, G.cc_current);
            int tgt = R<int>(cc, G.cc_buildPoints);
            int started = (G.cc_hasStarted != (size_t)-1) ? (int)R<uint8_t>(cc, G.cc_hasStarted) : -1;
            void* scaf = (G.cc_scaffolding != (size_t)-1) ? R<void*>(cc, G.cc_scaffolding) : nullptr;
            if (cur < (float)tgt || i < 6)
                LOG("  [%zu] %p cur=%.2f tgt=%d started=%d scaffold=%p\n", i, cc, cur, tgt, started, scaf);
        }
        return;
    }
    if (c == "campdebug") {
        LOG("BeggarCamp=%p stateMachine=%p moveNext=%p\n", (void*)G.beggarCamp,
            (void*)G.beggarSM, MethPtr(G.m_beggarMoveNext));
        if (G.beggarSM) {
            LOG("  state machine size=%u fields:\n", (unsigned)i2c_class_instance_size(G.beggarSM));
            void* it = nullptr;
            while (FieldInfo* f = i2c_class_get_fields(G.beggarSM, &it)) {
                const Il2CppType* ft = i2c_field_get_type(f);
                char* tn = ft ? i2c_type_get_name(ft) : nullptr;
                LOG("    .%-30s off=0x%04zX %s\n", i2c_field_get_name(f), i2c_field_get_offset(f), tn ? tn : "?");
                if (tn) i2c_free(tn);
            }
        }
        void* kingdom = KingdomInst();
        if (!kingdom) { LOG("  (no kingdom)\n"); return; }
        ForEachSet(kingdom, G.k_camps, [](void* camp) {
            size_t offList = (G.b_spawnInterval != (size_t)-1) ? FOffOf(camp, "_beggars") : (size_t)-1;
            void* lst = offList != (size_t)-1 ? R<void*>(camp, offList) : nullptr;
            int cnt = -1;
            if (Plausible(lst)) {
                size_t oi = FOffOf(lst, "_items"), os = FOffOf(lst, "_size");
                if (os != (size_t)-1) cnt = R<int>(lst, os);
                (void)oi;
            }
            LOG("  camp %p interval=%.2f maxBeggars=%d hunger=%.1f beggarsInCamp=%d\n", camp,
                R<float>(camp, G.b_spawnInterval), R<int>(camp, G.b_maxBeggars),
                R<float>(camp, G.b_hunger), cnt);
        });
        LOG("  hook hits=%ld campFound=%ld lastCamp=%p\n",
            g_campHookHits, g_campHookCampFound, g_campFromSM);
        return;
    }
    if (c == "wallet") {
        void* kingdom = KingdomInst();
        if (!kingdom) { LOG("  no kingdom\n"); return; }
        void* players = R<void*>(kingdom, G.k_players);
        for (size_t i = 0; i < ArrLen(players); i++) {
            void* p = ArrAt(players, i);
            if (!Plausible(p)) continue;
            void* w = R<void*>(p, G.p_wallet);
            LOG("  player[%zu]=%p wallet=%p coins=%d gems=%d capacity=%d\n", i, p, w,
                WalletGet(w, 0), WalletGet(w, 1), (int)R<int>(w, G.w_capacity));
        }
        return;
    }
    if (c == "verify") {
        void* kingdom = KingdomInst();
        LOG("=========== VERIFY ===========\n");
        LOG("[world] kingdom=%p inGame=%ld tick=%ld\n", kingdom, C->inGame, C->tickCount);
        if (!kingdom) { LOG("(not in a level)\n"); return; }
        // economy
        void* players = R<void*>(kingdom, G.k_players);
        void* wallet = nullptr; void* player = nullptr;
        for (size_t i = 0; i < ArrLen(players); i++) {
            void* p = ArrAt(players, i);
            if (Plausible(p)) { player = p; wallet = R<void*>(p, G.p_wallet); break; }
        }
        uint8_t inf = 0; ReadStatic(G.wallet, "<InfiniteMoney>k__BackingField", &inf);
        LOG("[1/2 money] coins=%d gems=%d | Wallet.InfiniteMoney=%d\n",
            WalletGet(wallet, 0), WalletGet(wallet, 1), inf);
        // mount
        void* steed = (player && G.p_steed != (size_t)-1) ? R<void*>(player, G.p_steed) : nullptr;
        uint8_t dbg = 0; ReadStatic(G.player, "DebugInfiniteStamina", &dbg);
        if (Plausible(steed))
            LOG("[4 mount] steed=%p Stamina=%.3f walkSpeed=%.3f runSpeed=%.3f | DebugInfiniteStamina=%d | saved=%zu\n",
                steed, R<float>(steed, G.s_stamina), R<float>(steed, G.s_walk), R<float>(steed, G.s_run),
                dbg, g_mountSaved.size());
        else LOG("[4 mount] no steed\n");
        // subjects
        int nSubj = 0, nSubjInv = 0;
        ForEachSet(kingdom, G.k_chars, [&](void* ch) {
            void* d = R<void*>(ch, G.c_damageable);
            if (IsLiveDamageable(d)) { nSubj++; if (R<uint8_t>(d, G.d_invuln)) nSubjInv++; }
        });
        LOG("[5 subjects] characters with live Damageable=%d invulnerable=%d | protect-set=%zu\n",
            nSubj, nSubjInv, g_protected.size());
        // buildings
        int nWall = 0, nWallInv = 0;
        ForEachSet(kingdom, G.k_walls, [&](void* wl) {
            void* d = R<void*>(wl, G.wl_damageable);
            if (IsLiveDamageable(d)) { nWall++; if (R<uint8_t>(d, G.d_invuln)) nWallInv++; }
        });
        int nTower = 0; ForEachSet(kingdom, G.k_towers, [&](void*) { nTower++; });
        LOG("[8 buildings] walls=%d invulnerable=%d | towers=%d\n", nWall, nWallInv, nTower);
        // camps
        int nCamp = 0;
        ForEachSet(kingdom, G.k_camps, [&](void* camp) {
            nCamp++;
            if (nCamp <= 3)
                LOG("[9 spawn] camp %p spawnInterval=%.2f maxBeggars=%d hunger=%.1f\n", camp,
                    R<float>(camp, G.b_spawnInterval), R<int>(camp, G.b_maxBeggars),
                    R<float>(camp, G.b_hunger));
        });
        if (!nCamp) LOG("[9 spawn] no camps in this level\n");
        // construction sites (stats are computed on the main thread each 500ms)
        LOG("[7 build] ConstructionBuildingComponent total=%ld pending=%ld complete=%ld\n",
            g_statTotal, g_statPending, g_statComplete);
        // game speed (read back through the game's own getter on the main thread)
        LOG("[10 speed] Time.timeScale=%.2f (cfg %.2f) feature=%ld\n",
            g_statTimeScale, C->gameSpeed, C->features[F_SPEED]);
        LOG("[6 crown] LoseCrown hook=%s monarchDmg=%p invuln=%d\n",
            oLoseCrown ? "installed" : "MISSING",
            (player && G.p_damageable != (size_t)-1) ? R<void*>(player, G.p_damageable) : nullptr,
            (player && G.p_damageable != (size_t)-1 && IsLiveDamageable(R<void*>(player, G.p_damageable)))
                ? (int)R<uint8_t>(R<void*>(player, G.p_damageable), G.d_invuln) : -1);
        LOG("==============================\n");
        return;
    }
    if (c == "on" || c == "off") {
        LONG v = (c == "on") ? 1 : 0;
        char feat[64] = {0};
        int got = sscanf_s(cmd.c_str(), "%*s %63s", feat, (unsigned)sizeof(feat));
        if (got == 1 && strcmp(feat, "all") != 0) {
            for (int i = 0; i < F_COUNT; i++) {
                if (!strcmp(feat, FEAT_NAME[i])) {
                    C->features[i] = v;
                    LOG("  %s = %ld\n", feat, v);
                    return;
                }
            }
            LOG("  (unknown feature '%s')\n", feat);
            return;
        }
        for (int i = 0; i < F_COUNT; i++) C->features[i] = v;
        if (v == 0) { C->gameSpeed = 1.0f; g_speedIdx = 0; }
        LOG("  all features = %ld\n", v);
        return;
    }
    LOG("  (unknown command)\n");
}

// ==================================================================== worker
static HMODULE g_self = nullptr;

static DWORD WINAPI Worker(LPVOID) {
    // IMPORTANT: this thread is created from DllMain, which runs while the process
    // loader lock is held. Any loader-lock dependent call (GetModuleHandle/GetProcAddress/
    // LoadLibrary) made before DllMain returns can deadlock. Wait it out first.
    Sleep(700);

    InitSharedCfg();
    // trainer root: 1) path handed over by the launcher  2) ktc_root.txt marker  3) dll folder
    if (C->rootPath[0]) {
        strncpy_s(g_root, C->rootPath, _TRUNCATE);
    }
    if (!g_root[0]) {
        char probe[MAX_PATH] = {0};
        GetModuleFileNameA(g_self, probe, MAX_PATH);
        char* slash0 = strrchr(probe, '\\');
        if (slash0) *slash0 = 0;
        strncpy_s(g_root, probe, _TRUNCATE);            // fallback: dll folder
        for (int i = 0; i < 8; i++) {
            char* sl = strrchr(probe, '\\');
            if (!sl) break;
            *sl = 0;
            char marker[MAX_PATH];
            snprintf(marker, sizeof(marker), "%s\\ktc_root.txt", probe);
            if (GetFileAttributesA(marker) != INVALID_FILE_ATTRIBUTES) { strncpy_s(g_root, probe, _TRUNCATE); break; }
        }
    }
    snprintf(g_cmdPath, sizeof(g_cmdPath), "%s\\dump\\cmd.txt", g_root);

    char logPath[MAX_PATH];
    snprintf(logPath, sizeof(logPath), "%s\\logs\\mod.log", g_root);
    CreateDirectoryA((std::string(g_root) + "\\logs").c_str(), nullptr);
    // append mode: several injected generations may share this log
    HANDLE lh = CreateFileA(logPath, FILE_APPEND_DATA, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (lh != INVALID_HANDLE_VALUE) {
        int fd = _open_osfhandle((intptr_t)lh, _O_WRONLY | _O_TEXT | _O_APPEND);
        if (fd != -1) g_log = _fdopen(fd, "a");
    }
    LOG("\n===== ktc_mod start  pid=%lu  root=%s =====\n", GetCurrentProcessId(), g_root);

    g_myGen = InterlockedIncrement(&C->generation);
    LOG("shared cfg = %p (magic %08X) generation=%ld\n", (void*)C, C->magic, g_myGen);

    Il2CppDomain* dom = nullptr;
    bool i2cReady = false;
    for (int i = 0; i < 240; i++) {
        HMODULE ga = GetModuleHandleA("GameAssembly.dll");
        if (ga) {
            if (!i2cReady) {
                if (i2c::Init()) i2cReady = true;
                else if (i < 5 || (i % 40) == 0) LOG("[!] il2cpp api resolve failed (attempt %d)\n", i);
            }
            if (i2cReady) {
                dom = i2c_domain_get();
                if (dom) break;
            }
        }
        if (i < 5 || (i % 40) == 0) LOG("[*] waiting for IL2CPP runtime... attempt %d (GameAssembly=%p)\n", i, (void*)ga);
        Sleep(500);
    }
    if (!dom) { LOG("FATAL: no IL2CPP domain after waiting\n"); return 1; }
    i2c_thread_attach(dom);
    LOG("attached domain %p\n", (void*)dom);

    g_imgGame = FindImage("Assembly-CSharp.dll");
    g_imgCore = FindImage("UnityEngine.CoreModule.dll");
    for (int i = 0; i < 400 && !ResolveGame(); i++) Sleep(250);
    if (!G.ok) { LOG("FATAL: classes unresolved\n"); return 1; }

    LOG("resolved: K=%p P=%p W=%p D=%p S=%p CC=%p Camp=%p Wall=%p beggarSM=%p moveNext=%p\n",
        (void*)G.kingdom, (void*)G.player, (void*)G.wallet, (void*)G.damageable,
        (void*)G.steed, (void*)G.construction, (void*)G.beggarCamp, (void*)G.wall,
        (void*)G.beggarSM, MethPtr(G.m_beggarMoveNext));
    LOG("methods: setCoins=%p setGems=%p findAll=%p setTimeScale=%p forceComplete=%p kUpdate=%p loseCrown=%p receiveDamage=%p\n",
        MethPtr(G.m_setCoins), MethPtr(G.m_setGems), MethPtr(G.m_findAll), MethPtr(G.m_setTimeScale),
        MethPtr(G.m_forceComplete), MethPtr(G.m_kUpdate), MethPtr(G.m_loseCrown), MethPtr(G.m_receiveDamage));

    if (MH_Initialize() != MH_OK) LOG("[-] MH_Initialize failed\n");
    // hot-swap: wait until the previous instance has released the inline hooks
    for (int i = 0; i < 60 && InterlockedCompareExchange(&C->hooksActive, 0, 0) != 0; i++) {
        LOG("[*] waiting for previous instance to release hooks (%d)\n", i);
        Sleep(250);
    }
    for (int attempt = 0; attempt < 20 && C->generation == g_myGen; attempt++) {
        int ok = 0, want = 4;
        ok += InstallHook("Kingdom.Update", G.m_kUpdate, (void*)hkKingdomUpdate, (void**)&oKingdomUpdate);
        ok += InstallHook("Player.LoseCrown", G.m_loseCrown, (void*)hkLoseCrown, (void**)&oLoseCrown);
        ok += InstallHook("Damageable.ReceiveDamage", G.m_receiveDamage, (void*)hkReceiveDamage, (void**)&oReceiveDamage);
        ok += InstallHook("Damageable.ReceiveFatalDamage", G.m_receiveFatal, (void*)hkReceiveFatal, (void**)&oReceiveFatal);
        if (G.m_beggarMoveNext) {
            want++;
            ok += InstallHook("BeggarCamp.<SlowUpdate>.MoveNext", G.m_beggarMoveNext,
                              (void*)hkBeggarMoveNext, (void**)&oBeggarMoveNext);
        } else {
            LOG("[!] BeggarCamp SlowUpdate state machine not found\n");
        }
        if (G.m_constructionComplete) {
            want++;
            ok += InstallHook("ConstructionBuildingComponent.ConstructionComplete", G.m_constructionComplete,
                              (void*)hkConstrComplete, (void**)&oConstrComplete);
        }
        if (G.m_constrAwake) {
            want++;
            ok += InstallHook("ConstructionBuildingComponent.Awake", G.m_constrAwake,
                              (void*)hkConstrAwake, (void**)&oConstrAwake);
        }
        if (G.m_constrInit) {
            want++;
            ok += InstallHook("ConstructionBuildingComponent.InitializeBuild", G.m_constrInit,
                              (void*)hkConstrInitBuild, (void**)&oConstrInitBuild);
        }
        if (ok >= want) { InterlockedExchange(&C->hooksActive, 1); break; }
        MH_DisableHook(MH_ALL_HOOKS);
        MH_RemoveHook(MH_ALL_HOOKS);
        Sleep(300);
    }
    C->ready = 1;

    while (!g_shutdown) {
        if (C->generation != g_myGen) {
            LOG("[*] superseded by generation %ld, releasing hooks\n", C->generation);
            MH_DisableHook(MH_ALL_HOOKS);
            MH_RemoveHook(MH_ALL_HOOKS);
            MH_Uninitialize();
            g_protectActive = 0;
            ResetInvuln(g_touchedSubjects);
            ResetInvuln(g_touchedStruct);
            ResetInvuln(g_touchedCrown);
            RestoreMount();
            RestoreCamps();
            InterlockedExchange(&C->hooksActive, 0);
            LOG("[*] hooks released\n");
            return 0;
        }
        HandleHotkeys();
        PollCommands();
        InterlockedIncrement(&C->heartbeat);
        Sleep(30);
    }
    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
