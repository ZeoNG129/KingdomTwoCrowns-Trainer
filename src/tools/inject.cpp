// inject.exe - injects a DLL into a running process (no files are written into the target's directory)
// usage: inject.exe --name <exe name> --dll <full dll path> [--wait <seconds>] [--pid <pid>]
#include <windows.h>
#include <tlhelp32.h>
#include <cstdio>
#include <cstring>
#include <cstdlib>

static bool EnableDebugPriv() {
    HANDLE tok = nullptr;
    if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &tok)) return false;
    TOKEN_PRIVILEGES tp{};
    tp.PrivilegeCount = 1;
    tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;
    bool ok = LookupPrivilegeValueA(nullptr, SE_DEBUG_NAME, &tp.Privileges[0].Luid) &&
              AdjustTokenPrivileges(tok, FALSE, &tp, sizeof(tp), nullptr, nullptr) &&
              GetLastError() == ERROR_SUCCESS;
    CloseHandle(tok);
    return ok;
}

static DWORD FindPidByName(const char* name) {
    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snap == INVALID_HANDLE_VALUE) return 0;
    PROCESSENTRY32W pe{};
    pe.dwSize = sizeof(pe);
    DWORD pid = 0;
    // convert to wide
    wchar_t wname[MAX_PATH]{};
    MultiByteToWideChar(CP_ACP, 0, name, -1, wname, MAX_PATH);
    if (Process32FirstW(snap, &pe)) {
        do {
            if (_wcsicmp(pe.szExeFile, wname) == 0) { pid = pe.th32ProcessID; break; }
        } while (Process32NextW(snap, &pe));
    }
    CloseHandle(snap);
    return pid;
}

static bool Inject(DWORD pid, const char* dllPath, DWORD* outErr) {
    HANDLE hp = OpenProcess(PROCESS_CREATE_THREAD | PROCESS_QUERY_INFORMATION | PROCESS_VM_OPERATION |
                            PROCESS_VM_WRITE | PROCESS_VM_READ, FALSE, pid);
    if (!hp) { *outErr = GetLastError(); return false; }

    wchar_t wpath[MAX_PATH * 2]{};
    MultiByteToWideChar(CP_ACP, 0, dllPath, -1, wpath, MAX_PATH * 2);
    SIZE_T bytes = (wcslen(wpath) + 1) * sizeof(wchar_t);

    void* remote = VirtualAllocEx(hp, nullptr, bytes, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
    if (!remote) { *outErr = GetLastError(); CloseHandle(hp); return false; }
    if (!WriteProcessMemory(hp, remote, wpath, bytes, nullptr)) {
        *outErr = GetLastError(); VirtualFreeEx(hp, remote, 0, MEM_RELEASE); CloseHandle(hp); return false;
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    auto loadLib = (LPTHREAD_START_ROUTINE)GetProcAddress(k32, "LoadLibraryW");
    HANDLE th = CreateRemoteThread(hp, nullptr, 0, loadLib, remote, 0, nullptr);
    if (!th) { *outErr = GetLastError(); VirtualFreeEx(hp, remote, 0, MEM_RELEASE); CloseHandle(hp); return false; }

    WaitForSingleObject(th, 20000);
    DWORD modBase = 0;
    GetExitCodeThread(th, &modBase);
    CloseHandle(th);
    VirtualFreeEx(hp, remote, 0, MEM_RELEASE);
    CloseHandle(hp);

    if (modBase == 0) { *outErr = ERROR_DLL_INIT_FAILED; return false; }
    *outErr = 0;
    printf("[+] injected into pid %lu, module base = 0x%lX\n", pid, modBase);
    return true;
}

int main(int argc, char** argv) {
    const char* name = nullptr;
    const char* dll = nullptr;
    int waitSec = 0;
    DWORD pidArg = 0;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--name") && i + 1 < argc) name = argv[++i];
        else if (!strcmp(argv[i], "--dll") && i + 1 < argc) dll = argv[++i];
        else if (!strcmp(argv[i], "--wait") && i + 1 < argc) waitSec = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--pid") && i + 1 < argc) pidArg = (DWORD)strtoul(argv[++i], nullptr, 10);
    }
    if (!dll || (!name && !pidArg)) {
        printf("usage: inject.exe --name <exe> | --pid <pid> --dll <full path> [--wait sec]\n");
        return 2;
    }
    if (GetFileAttributesA(dll) == INVALID_FILE_ATTRIBUTES) {
        printf("[-] dll not found: %s\n", dll);
        return 2;
    }
    EnableDebugPriv();

    DWORD pid = pidArg;
    if (!pid) {
        for (int t = 0; t <= waitSec; t++) {
            pid = FindPidByName(name);
            if (pid) break;
            if (t == waitSec) { printf("[-] process not found: %s\n", name); return 3; }
            Sleep(1000);
        }
    }
    printf("[*] target pid = %lu, dll = %s\n", pid, dll);
    DWORD err = 0;
    if (!Inject(pid, dll, &err)) {
        printf("[-] injection failed, GetLastError = %lu\n", err);
        return 4;
    }
    return 0;
}
