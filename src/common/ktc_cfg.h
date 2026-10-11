// Shared configuration between KTCTrainer.exe (launcher) and ktc_mod.dll (injected)
// Layout must stay byte-identical on both sides.
#pragma once
#include <cstdint>
#include <windows.h>

// NOTE: bump the mapping name whenever this struct layout changes, otherwise an
// older (smaller) mapping object left alive by a previous instance breaks MapViewOfFile.
#define KTC_CFG_NAME  "KTCTrainer_Cfg_v4"
#define KTC_CFG_MAGIC 0x4B544331u   /* 'KTC1' */
#define KTC_CFG_VER   4u

enum KtcFeat {
    F_COINS = 0, F_GEMS, F_NOCONSUME, F_MOUNT, F_SUBJECTS,
    F_CROWN, F_BUILD, F_STRUCT, F_SPAWN, F_SPEED, F_COUNT
};

inline const char* KtcFeatName(int i) {
    static const char* n[F_COUNT] = {
        "add10coins", "add10gems", "noConsume", "mountBoost", "subjectsInvincible",
        "crownSafe", "instantBuild", "buildingsInvincible", "fastSpawn", "gameSpeed"
    };
    return (i >= 0 && i < F_COUNT) ? n[i] : "?";
}

inline const char* KtcFeatLabel(int i) {
    static const char* n[F_COUNT] = {
        "F1  +10 金币", "F2  +10 宝石", "F3  资源不消耗", "F4  马匹快速奔跑 + 无限耐力",
        "F5  子民无敌", "F6  王冠不掉落", "F7  建造秒完成", "F8  建筑无敌",
        "F9  野外刷新点快速刷人", "F10 游戏速度"
    };
    return (i >= 0 && i < F_COUNT) ? n[i] : "?";
}

#pragma pack(push, 4)
struct KtcCfg {
    uint32_t magic;
    uint32_t version;
    volatile LONG  features[F_COUNT];
    volatile float gameSpeed;        // 1.0 .. 8.0
    volatile float runMultiplier;    // mount speed multiplier
    volatile LONG  reqAddCoins;      // request counters (mod consumes, zeroes)
    volatile LONG  reqAddGems;
    volatile LONG  reqClearWallet;   // 1 = empty the coin pouch (coins & gems -> 0)
    volatile LONG  status[F_COUNT];  // mod -> launcher feedback
    volatile LONG  ready;
    volatile LONG  inGame;
    volatile LONG  coins;
    volatile LONG  gems;
    volatile LONG  hotkeysEnabled;
    volatile LONG  tickCount;
    volatile LONG  lastError;
    volatile LONG  generation;       // newest injected instance owns the hooks
    volatile LONG  hooksActive;
    volatile LONG  heartbeat;        // incremented by the mod worker ~30x/second
    char           rootPath[MAX_PATH];  // trainer root dir, written by the launcher
};
#pragma pack(pop)

inline KtcCfg* KtcOpenCfg(bool createIfMissing) {
    HANDLE h = nullptr;
    if (createIfMissing)
        h = CreateFileMappingA(INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0, sizeof(KtcCfg), KTC_CFG_NAME);
    else
        h = OpenFileMappingA(FILE_MAP_ALL_ACCESS, FALSE, KTC_CFG_NAME);
    if (!h) return nullptr;
    void* p = MapViewOfFile(h, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(KtcCfg));
    return (KtcCfg*)p;
}
