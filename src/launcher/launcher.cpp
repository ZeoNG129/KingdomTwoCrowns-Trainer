// KTCTrainer.exe - Kingdom Two Crowns memory trainer launcher / control panel
// Lives outside the game directory. Injects ktc_mod.dll (found next to this exe)
// into KingdomTwoCrowns.exe and drives it through shared memory. No game file is touched.
#include "../common/ktc_cfg.h"
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <cwchar>
#include <string>

// ---------------------------------------------------------------- controls
#define ID_STATUS      2001
#define ID_DETAIL      2002
#define ID_INJECT      1001
#define ID_AUTOINJECT  1010
#define ID_RESETALL    1002
#define ID_CLEARMOD    1003
#define ID_QUIT        1004
#define ID_ADDCOIN     1200
#define ID_ADDGEM      1201
#define ID_CLEARWALLET 1202
#define ID_FEATBASE    1100
#define ID_SPEEDBOX    1300
#define ID_RUNMULT     1301
#define ID_APPLYMULT   1302

static KtcCfg* g_cfg = nullptr;
static HWND    g_hwnd = nullptr;
static HWND    g_status = nullptr, g_detail = nullptr, g_speedBox = nullptr, g_runMult = nullptr;
static HWND    g_feat[F_COUNT] = {0};
static DWORD   g_injectedPid = 0;
static bool    g_autoInject = true;
static bool    g_modAlive = false;
static LONG    g_lastBeat = -1;
static DWORD   g_lastBeatTime = 0;
static wchar_t g_exeDir[MAX_PATH] = {0};
static const float SPEEDS[] = {1.0f, 1.5f, 2.0f, 3.0f, 5.0f, 8.0f};
static const int   SPEED_N = 6;

static const wchar_t* FeatLabelW(int i) {
    static const wchar_t* n[F_COUNT] = {
        L"F1  +10 金币", L"F2  +10 宝石", L"F3  资源不消耗", L"F4  马匹快速奔跑 + 无限耐力",
        L"F5  子民无敌", L"F6  王冠不掉落", L"F7  建造秒完成", L"F8  建筑无敌",
        L"F9  野外刷新点快速刷人", L"F10 游戏速度"
    };
    return (i >= 0 && i < F_COUNT) ? n[i] : L"?";
}

// ---------------------------------------------------------------- process / injection
static DWORD FindGamePid() {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{}; pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, L"KingdomTwoCrowns.exe") == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool InjectDll(DWORD pid, const wchar_t* dllPath, DWORD* err) {
    HANDLE hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                            PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hp) { *err = GetLastError(); return false; }
    SIZE_T bytes = (wcslen(dllPath) + 1) * sizeof(wchar_t);
    void* remote = VirtualAllocEx(hp, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { *err = GetLastError(); CloseHandle(hp); return false; }
    if (!WriteProcessMemory(hp, remote, dllPath, bytes, nullptr)) {
        *err = GetLastError(); VirtualFreeEx(hp, remote, 0, MEM_RELEASE); CloseHandle(hp); return false;
    }
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(GetModuleHandleW(L"kernel32.dll"), "LoadLibraryW");
    HANDLE th = CreateRemoteThread(hp, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) { *err = GetLastError(); VirtualFreeEx(hp, remote, 0, MEM_RELEASE); CloseHandle(hp); return false; }
    WaitForSingleObject(th, 20000);
    DWORD modBase = 0;
    GetExitCodeThread(th, &modBase);
    CloseHandle(th);
    VirtualFreeEx(hp, remote, 0, MEM_RELEASE);
    CloseHandle(hp);
    *err = modBase ? 0 : ERROR_DLL_INIT_FAILED;
    return modBase != 0;
}

static void DoInject(bool quiet) {
    DWORD pid = FindGamePid();
    if (!pid) {
        if (!quiet)
            MessageBoxW(g_hwnd, L"没有找到游戏进程 KingdomTwoCrowns.exe。\n\n请先启动《王国：两位君主》，再点击注入。",
                        L"未找到游戏", MB_ICONINFORMATION);
        return;
    }
    if (pid == g_injectedPid && g_modAlive) {
        if (!quiet)
            MessageBoxW(g_hwnd, L"修改器已经在游戏里运行了，直接回到游戏使用即可。\n\n"
                                L"（游戏内热键：F1 +10金币，F2 +10宝石，F3~F9 开关各项，F10 切速度，F11 全部关闭）",
                        L"无需重复注入", MB_ICONINFORMATION);
        return;
    }
    wchar_t dll[MAX_PATH];
    _snwprintf_s(dll, _TRUNCATE, L"%s\\ktc_mod.dll", g_exeDir);
    if (GetFileAttributesW(dll) == INVALID_FILE_ATTRIBUTES) {
        if (!quiet) {
            wchar_t msg[MAX_PATH + 128];
            _snwprintf_s(msg, _TRUNCATE, L"找不到 ktc_mod.dll：\n%s\n\n请把它和 KTCTrainer.exe 放在同一个文件夹。", dll);
            MessageBoxW(g_hwnd, msg, L"缺少文件", MB_ICONERROR);
        }
        return;
    }
    if (g_cfg) WideCharToMultiByte(CP_ACP, 0, g_exeDir, -1, g_cfg->rootPath, MAX_PATH, nullptr, nullptr);

    // Inject a uniquely named copy: LoadLibraryW on an already loaded path is a no-op,
    // so a fresh copy is what makes "inject again" actually reload the module.
    wchar_t tmpDir[MAX_PATH], runDir[MAX_PATH], dst[MAX_PATH];
    GetTempPathW(MAX_PATH, tmpDir);
    _snwprintf_s(runDir, _TRUNCATE, L"%sktc_run", tmpDir);
    CreateDirectoryW(runDir, nullptr);
    _snwprintf_s(dst, _TRUNCATE, L"%s\\ktc_mod_%lu_%lu.dll", runDir, pid, GetTickCount());
    if (!CopyFileW(dll, dst, FALSE)) {
        if (!quiet) {
            wchar_t msg[MAX_PATH + 128];
            _snwprintf_s(msg, _TRUNCATE, L"无法复制 ktc_mod.dll 到临时目录：\n%s\n\n错误码 %lu", dst, GetLastError());
            MessageBoxW(g_hwnd, msg, L"注入失败", MB_ICONERROR);
        }
        return;
    }

    DWORD err = 0;
    if (InjectDll(pid, dst, &err)) {
        g_injectedPid = pid;
        SetWindowTextW(g_status, L"游戏进程: 已注入，等待修改器就绪…");
    } else if (!quiet) {
        wchar_t msg[256];
        _snwprintf_s(msg, _TRUNCATE,
            L"注入失败（错误码 %lu）。\n\n可能原因：游戏以管理员身份运行，而本工具没有。\n"
            L"请右键「以管理员身份运行」本工具后重试。", err);
        MessageBoxW(g_hwnd, msg, L"注入失败", MB_ICONERROR);
    }
}

// ---------------------------------------------------------------- ui helpers
static void SetCheck(HWND cb, bool on) {
    if (cb && ((SendMessageW(cb, BM_GETCHECK, 0, 0) == BST_CHECKED) != on))
        SendMessageW(cb, BM_SETCHECK, on ? BST_CHECKED : BST_UNCHECKED, 0);
}

static void RefreshUI() {
    DWORD pid = FindGamePid();
    // liveness = the mod keeps incrementing its heartbeat
    if (g_cfg && g_cfg->magic == KTC_CFG_MAGIC) {
        LONG b = g_cfg->heartbeat;
        if (b != g_lastBeat) { g_lastBeat = b; g_lastBeatTime = GetTickCount(); }
    }
    g_modAlive = pid && g_cfg && (GetTickCount() - g_lastBeatTime < 2500);

    wchar_t buf[256];
    if (!pid) wcscpy_s(buf, L"游戏进程: 未运行");
    else if (!g_modAlive) _snwprintf_s(buf, _TRUNCATE, L"游戏进程: 运行中 (PID %lu)  —  修改器未加载", pid);
    else _snwprintf_s(buf, _TRUNCATE, L"游戏进程: 运行中 (PID %lu)  —  修改器已生效 ✓", pid);
    SetWindowTextW(g_status, buf);

    if (g_modAlive) {
        for (int i = 0; i < F_COUNT; i++) SetCheck(g_feat[i], g_cfg->features[i] != 0);
        int idx = 0;
        for (int i = 0; i < SPEED_N; i++) if (g_cfg->gameSpeed >= SPEEDS[i] - 0.01f) idx = i;
        SendMessageW(g_speedBox, CB_SETCURSEL, idx, 0);
        if (GetFocus() != g_runMult) {
            wchar_t mb[32]; _snwprintf_s(mb, _TRUNCATE, L"%.2f", g_cfg->runMultiplier);
            SetWindowTextW(g_runMult, mb);
        }
        wchar_t d[1600];
        _snwprintf_s(d, _TRUNCATE,
            L"存档内: %s\r\n"
            L"金币: %ld      宝石: %ld\r\n"
            L"运行帧计数: %ld\r\n"
            L"\r\n"
            L"资源不消耗: %s        子民无敌: %s\r\n"
            L"建筑无敌: %s        王冠保护: %s\r\n"
            L"建造秒完成: 本轮完工 %ld 个        马匹加速: %s\r\n"
            L"刷人点间隔: %s        游戏速度: %s\r\n"
            L"\r\n"
            L"游戏内热键: F1 +10金币   F2 +10宝石   F3~F9 开关各项\r\n"
            L"F10 切换游戏速度   F11 全部关闭",
            g_cfg->inGame ? L"已进入关卡" : L"未进入关卡（在主菜单）",
            g_cfg->coins, g_cfg->gems, g_cfg->tickCount,
            g_cfg->features[F_NOCONSUME] ? L"开" : L"关",
            g_cfg->features[F_SUBJECTS] ? L"开" : L"关",
            g_cfg->features[F_STRUCT] ? L"开" : L"关",
            g_cfg->features[F_CROWN] ? L"开" : L"关",
            g_cfg->status[F_BUILD],
            g_cfg->features[F_MOUNT] ? L"开" : L"关",
            g_cfg->features[F_SPAWN] ? L"开（约1秒/人）" : L"关",
            g_cfg->features[F_SPEED] ? L"加速中" : L"正常");
        SetWindowTextW(g_detail, d);
    } else {
        SetWindowTextW(g_detail,
            L"修改器未生效。\r\n\r\n"
            L"  1) 先启动游戏《王国：两位君主》\r\n"
            L"  2) 点击上方「注入修改器」（或勾选自动注入）\r\n"
            L"  3) 回到游戏，按 F1 即可加金币\r\n\r\n"
            L"本工具只修改游戏内存，不会写入、改动或删除任何游戏文件。");
    }
}

static void ResetAll() {
    if (!g_cfg) return;
    for (int i = 0; i < F_COUNT; i++) g_cfg->features[i] = 0;
    g_cfg->gameSpeed = 1.0f;
    g_cfg->runMultiplier = 2.2f;
    RefreshUI();
}

// ---------------------------------------------------------------- window
static HFONT g_font = nullptr, g_mono = nullptr;

static HWND Mk(const wchar_t* cls, const wchar_t* text, DWORD style, int x, int y, int w, int h, int id) {
    HWND c = CreateWindowExW(0, cls, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h, g_hwnd,
                             (HMENU)(INT_PTR)id, nullptr, nullptr);
    if (c && g_font) SendMessageW(c, WM_SETFONT, (WPARAM)g_font, TRUE);
    return c;
}

static void BuildUI() {
    Mk(L"STATIC", L"Kingdom Two Crowns 内存修改器     （只改内存，绝不改动游戏文件）", SS_LEFT, 16, 12, 540, 20, -1);

    Mk(L"BUTTON", L"游戏", BS_GROUPBOX, 12, 36, 556, 74, -1);
    g_status = Mk(L"STATIC", L"游戏进程: 检查中…", SS_LEFT, 26, 56, 530, 20, ID_STATUS);
    Mk(L"BUTTON", L"注入修改器", BS_PUSHBUTTON, 26, 78, 110, 26, ID_INJECT);
    HWND cbAuto = Mk(L"BUTTON", L"自动注入（游戏启动后自动加载）", BS_AUTOCHECKBOX, 150, 82, 300, 22, ID_AUTOINJECT);
    SendMessageW(cbAuto, BM_SETCHECK, BST_CHECKED, 0);

    Mk(L"BUTTON", L"功能开关", BS_GROUPBOX, 12, 118, 556, 240, -1);
    for (int i = 0; i < F_COUNT; i++) {
        int y = 138 + i * 21;
        g_feat[i] = Mk(L"BUTTON", FeatLabelW(i), BS_AUTOCHECKBOX, 26, y, 310, 20, ID_FEATBASE + i);
        if (i == F_COINS) Mk(L"BUTTON", L"+10", BS_PUSHBUTTON, 480, y - 2, 70, 22, ID_ADDCOIN);
        if (i == F_GEMS)  Mk(L"BUTTON", L"+10", BS_PUSHBUTTON, 480, y - 2, 70, 22, ID_ADDGEM);
        if (i == F_SPEED) {
            g_speedBox = Mk(L"COMBOBOX", L"", CBS_DROPDOWNLIST | WS_VSCROLL, 340, y - 2, 130, 200, ID_SPEEDBOX);
            const wchar_t* items[] = {L"1x (正常)", L"1.5x", L"2x", L"3x", L"5x", L"8x"};
            for (int k = 0; k < SPEED_N; k++) SendMessageW(g_speedBox, CB_ADDSTRING, 0, (LPARAM)items[k]);
            SendMessageW(g_speedBox, CB_SETCURSEL, 0, 0);
        }
    }

    Mk(L"STATIC", L"马匹速度倍率:", SS_LEFT, 26, 366, 96, 20, -1);
    g_runMult = Mk(L"EDIT", L"2.20", ES_LEFT | WS_BORDER, 124, 364, 60, 22, ID_RUNMULT);
    Mk(L"BUTTON", L"应用", BS_PUSHBUTTON, 190, 364, 60, 22, ID_APPLYMULT);
    Mk(L"BUTTON", L"清空钱袋（金币+宝石归零）", BS_PUSHBUTTON, 330, 362, 230, 26, ID_CLEARWALLET);

    Mk(L"BUTTON", L"运行状态", BS_GROUPBOX, 12, 396, 556, 236, -1);
    g_detail = Mk(L"STATIC", L"", SS_LEFT, 26, 414, 534, 216, ID_DETAIL);

    Mk(L"BUTTON", L"全部关闭（恢复原值）", BS_PUSHBUTTON, 12, 642, 180, 28, ID_CLEARMOD);
    Mk(L"BUTTON", L"刷新面板", BS_PUSHBUTTON, 200, 642, 110, 28, ID_RESETALL);
    Mk(L"BUTTON", L"退出", BS_PUSHBUTTON, 488, 642, 80, 28, ID_QUIT);
}

static LRESULT CALLBACK WndProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
    case WM_CREATE:
        g_hwnd = h;              // CreateWindowExW dispatches WM_CREATE before returning
        BuildUI();
        SetTimer(h, 1, 200, nullptr);
        return 0;
    case WM_TIMER:
        RefreshUI();
        if (g_autoInject && g_cfg) {
            DWORD pid = FindGamePid();
            if (pid && pid != g_injectedPid) DoInject(true);
        }
        return 0;
    case WM_COMMAND: {
        int id = LOWORD(w);
        if (id >= ID_FEATBASE && id < ID_FEATBASE + F_COUNT) {
            int i = id - ID_FEATBASE;
            if (g_cfg) g_cfg->features[i] = (SendMessageW(g_feat[i], BM_GETCHECK, 0, 0) == BST_CHECKED) ? 1 : 0;
            return 0;
        }
        switch (id) {
        case ID_INJECT: DoInject(false); break;
        case ID_AUTOINJECT:
            g_autoInject = (SendMessageW((HWND)l, BM_GETCHECK, 0, 0) == BST_CHECKED);
            break;
        case ID_ADDCOIN: if (g_cfg) InterlockedIncrement(&g_cfg->reqAddCoins); break;
        case ID_ADDGEM:  if (g_cfg) InterlockedIncrement(&g_cfg->reqAddGems); break;
        case ID_CLEARWALLET: if (g_cfg) InterlockedExchange(&g_cfg->reqClearWallet, 1); break;
        case ID_CLEARMOD: ResetAll(); break;
        case ID_RESETALL:
            if (g_cfg) for (int i = 0; i < F_COUNT; i++) SetCheck(g_feat[i], g_cfg->features[i] != 0);
            RefreshUI();
            break;
        case ID_SPEEDBOX:
            if (HIWORD(w) == CBN_SELCHANGE && g_cfg) {
                int sel = (int)SendMessageW(g_speedBox, CB_GETCURSEL, 0, 0);
                if (sel >= 0 && sel < SPEED_N) { g_cfg->gameSpeed = SPEEDS[sel]; g_cfg->features[F_SPEED] = 1; }
            }
            break;
        case ID_APPLYMULT:
            if (g_cfg) {
                wchar_t b[32]{}; GetWindowTextW(g_runMult, b, 31);
                float v = (float)_wtof(b);
                if (v < 1.0f) v = 1.0f;
                if (v > 8.0f) v = 8.0f;
                g_cfg->runMultiplier = v;
                wchar_t nb[32]; _snwprintf_s(nb, _TRUNCATE, L"%.2f", v); SetWindowTextW(g_runMult, nb);
            }
            break;
        case ID_QUIT: DestroyWindow(h); break;
        }
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(h);
        return 0;
    case WM_DESTROY:
        if (g_cfg) { for (int i = 0; i < F_COUNT; i++) g_cfg->features[i] = 0; g_cfg->gameSpeed = 1.0f; }
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE, LPSTR, int show) {
    SetProcessDPIAware();
    GetModuleFileNameW(nullptr, g_exeDir, MAX_PATH);
    wchar_t* sl = wcsrchr(g_exeDir, L'\\');
    if (sl) *sl = 0;

    g_cfg = KtcOpenCfg(true);
    if (!g_cfg) {
        MessageBoxW(nullptr, L"无法创建共享内存（可能被安全软件拦截）。\n修改器面板无法与注入的模块通信。",
                    L"初始化失败", MB_ICONERROR);
        return 1;
    }
    // best-effort cleanup of copies left over from previous sessions
    {
        wchar_t tmpDir[MAX_PATH], runDir[MAX_PATH];
        GetTempPathW(MAX_PATH, tmpDir);
        _snwprintf_s(runDir, _TRUNCATE, L"%sktc_run", tmpDir);
        wchar_t pat[MAX_PATH];
        _snwprintf_s(pat, _TRUNCATE, L"%s\\ktc_mod_*.dll", runDir);
        WIN32_FIND_DATAW fd{};
        HANDLE hf = FindFirstFileW(pat, &fd);
        if (hf != INVALID_HANDLE_VALUE) {
            do {
                wchar_t full[MAX_PATH];
                _snwprintf_s(full, _TRUNCATE, L"%s\\%s", runDir, fd.cFileName);
                DeleteFileW(full);          // fails while the game still has it mapped
            } while (FindNextFileW(hf, &fd));
            FindClose(hf);
        }
    }
    if (g_cfg->magic != KTC_CFG_MAGIC) {
        memset(g_cfg, 0, sizeof(KtcCfg));
        g_cfg->magic = KTC_CFG_MAGIC;
        g_cfg->version = KTC_CFG_VER;
        g_cfg->gameSpeed = 1.0f;
        g_cfg->runMultiplier = 2.2f;
        g_cfg->hotkeysEnabled = 1;
    }
    if (g_cfg) WideCharToMultiByte(CP_ACP, 0, g_exeDir, -1, g_cfg->rootPath, MAX_PATH, nullptr, nullptr);

    WNDCLASSW wc{};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = inst;
    wc.lpszClassName = L"KTCTrainerWnd";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    RegisterClassW(&wc);

    g_font = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         DEFAULT_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");
    g_mono = CreateFontW(-14, 0, 0, 0, FW_NORMAL, 0, 0, 0, DEFAULT_CHARSET,
                         OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY,
                         FIXED_PITCH | FF_DONTCARE, L"Microsoft YaHei UI");

    int W = 600, H = 724;
    int sx = (GetSystemMetrics(SM_CXSCREEN) - W) / 2;
    int sy = (GetSystemMetrics(SM_CYSCREEN) - H) / 2;
    g_hwnd = CreateWindowExW(WS_EX_TOPMOST, L"KTCTrainerWnd", L"王国：两位君主 — 内存修改器",
                             WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
                             sx, sy, W, H, nullptr, nullptr, inst, nullptr);
    if (!g_hwnd) return 1;
    if (g_mono && g_detail) SendMessageW(g_detail, WM_SETFONT, (WPARAM)g_mono, TRUE);
    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        if (!IsDialogMessageW(g_hwnd, &msg)) { TranslateMessage(&msg); DispatchMessageW(&msg); }
    }
    return 0;
}
