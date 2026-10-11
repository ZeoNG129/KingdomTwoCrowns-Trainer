// recon.dll - injected into KingdomTwoCrowns.exe
// Dumps the full IL2CPP class/field/method structure of the game into the trainer dump folder.
#include "../common/il2cpp_api.h"
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static HMODULE g_self = nullptr;
static char    g_dumpDir[MAX_PATH] = {0};

static void SelfDir(char* out, size_t n) {
    GetModuleFileNameA(g_self, out, (DWORD)n);
    char* p = strrchr(out, '\\');
    if (p) *p = 0;
}

static void EnsureDir(const char* d) { CreateDirectoryA(d, nullptr); }

// ------------------------------------------------------------------ helpers
static const char* SafeName(const char* s) { return s ? s : "?"; }

struct ClassInfo {
    Il2CppClass* klass;
    std::string  full;      // Namespace.Name
    std::string  parent;    // parent full name
    uint32_t     size;
    uint32_t     flags;
    int          fieldCount;
    int          methodCount;
    bool         isMB;      // MonoBehaviour
    bool         isSO;      // ScriptableObject
};

int main_dump();

// ------------------------------------------------------------------ dump
static void DumpAll() {
    char path[MAX_PATH];
    EnsureDir(g_dumpDir);

    // resolve UnityEngine base classes for tagging
    Il2CppClass* mbClass = nullptr;
    Il2CppClass* soClass = nullptr;
    {
        size_t n = 0;
        const Il2CppAssembly** asms = i2c_domain_get_assemblies(i2c_domain_get(), &n);
        for (size_t i = 0; i < n; i++) {
            Il2CppImage* img = i2c_assembly_get_image(asms[i]);
            const char* nm = i2c_image_get_name(img);
            if (nm && strcmp(nm, "UnityEngine.CoreModule.dll") == 0) {
                mbClass = i2c_class_from_name(img, "UnityEngine", "MonoBehaviour");
                soClass = i2c_class_from_name(img, "UnityEngine", "ScriptableObject");
                break;
            }
        }
    }

    size_t asmCount = 0;
    const Il2CppAssembly** asms = i2c_domain_get_assemblies(i2c_domain_get(), &asmCount);

    snprintf(path, sizeof(path), "%s\\images.txt", g_dumpDir);
    FILE* fimg = fopen(path, "wb");

    snprintf(path, sizeof(path), "%s\\dump.txt", g_dumpDir);
    FILE* f = fopen(path, "wb");
    if (!f) return;

    fprintf(f, "# Kingdom Two Crowns IL2CPP dump  (GameAssembly base=%p size=0x%zX)\n",
            (void*)i2c::g_base, i2c::g_size);
    fprintf(f, "# assemblies=%zu\n", asmCount);

    std::vector<ClassInfo> index;
    index.reserve(8192);

    for (size_t a = 0; a < asmCount; a++) {
        Il2CppImage* img = i2c_assembly_get_image(asms[a]);
        const char* imgName = SafeName(i2c_image_get_name(img));
        size_t cc = i2c_image_get_class_count(img);
        if (fimg) fprintf(fimg, "%s\t%zu\n", imgName, cc);

        fprintf(f, "\n########## IMAGE %s  classes=%zu\n", imgName, cc);

        for (size_t c = 0; c < cc; c++) {
            const Il2CppClass* ck = i2c_image_get_class(img, c);
            if (!ck) continue;
            Il2CppClass* k = (Il2CppClass*)ck;

            const char* ns = i2c_class_get_namespace(k);
            const char* nm = i2c_class_get_name(k);
            Il2CppClass* par = i2c_class_get_parent(k);

            std::string full = std::string(ns && *ns ? ns : "") + (ns && *ns ? "." : "") + SafeName(nm);
            std::string parent = "?";
            if (par) {
                const char* pns = i2c_class_get_namespace(par);
                const char* pnm = i2c_class_get_name(par);
                parent = std::string(pns && *pns ? pns : "") + (pns && *pns ? "." : "") + SafeName(pnm);
            }

            ClassInfo ci;
            ci.klass = k;
            ci.full = full;
            ci.parent = parent;
            ci.size = i2c_class_instance_size(k);
            ci.flags = i2c_class_get_flags(k);
            ci.isMB = mbClass ? i2c_class_is_subclass_of(k, mbClass, false) : false;
            ci.isSO = soClass ? i2c_class_is_subclass_of(k, soClass, false) : false;

            fprintf(f, "\n[CLASS] %s  :  %s   size=0x%X flags=0x%X%s%s\n",
                    full.c_str(), parent.c_str(), ci.size, ci.flags,
                    ci.isMB ? " [MonoBehaviour]" : "", ci.isSO ? " [ScriptableObject]" : "");

            // ---- fields
            int fc = 0;
            void* it = nullptr;
            while (FieldInfo* fi = i2c_class_get_fields(k, &it)) {
                const char* fn = i2c_field_get_name(fi);
                size_t off = i2c_field_get_offset(fi);
                uint32_t ff = i2c_field_get_flags(fi);
                const Il2CppType* ft = i2c_field_get_type(fi);
                char* tn = ft ? i2c_type_get_name(ft) : nullptr;
                bool isStatic = (ff & 0x10) != 0;   // FIELD_ATTRIBUTE_STATIC
                fprintf(f, "    F %s off=0x%04zX %-38s %s\n",
                        isStatic ? "S" : "i", off, SafeName(tn), SafeName(fn));
                if (tn) i2c_free(tn);
                fc++;
            }
            ci.fieldCount = fc;

            // ---- methods
            int mc = 0;
            it = nullptr;
            while (const MethodInfo* mi = i2c_class_get_methods(k, &it)) {
                MethodInfo* m = (MethodInfo*)mi;
                const char* mn = i2c_method_get_name(m);
                uint32_t pc = i2c_method_get_param_count(m);
                uint32_t iflags = 0;
                uint32_t mf = i2c_method_get_flags(m, &iflags);
                const Il2CppType* rt = i2c_method_get_return_type(m);
                char* rtn = rt ? i2c_type_get_name(rt) : nullptr;

                // candidate native entry point (first qword of MethodInfo)
                void* p0 = *(void**)m;
                void* p1 = *((void**)m + 1);

                fprintf(f, "    M %-34s ret=%-22s argc=%u flags=0x%X p0=%p%s p1=%p%s",
                        SafeName(mn), SafeName(rtn), pc, mf,
                        p0, i2c::InGameAssembly(p0) ? "(GA)" : "",
                        p1, i2c::InGameAssembly(p1) ? "(GA)" : "");
                if (pc > 0 && pc <= 6) {
                    fprintf(f, " args=(");
                    for (uint32_t i = 0; i < pc; i++) {
                        const Il2CppType* pt = i2c_method_get_param(m, i);
                        char* ptn = pt ? i2c_type_get_name(pt) : nullptr;
                        fprintf(f, "%s%s", i ? "," : "", SafeName(ptn));
                        if (ptn) i2c_free(ptn);
                    }
                    fprintf(f, ")");
                }
                fprintf(f, "\n");
                if (rtn) i2c_free(rtn);
                mc++;
            }
            ci.methodCount = mc;
            index.push_back(ci);
        }
    }
    fclose(f);
    if (fimg) fclose(fimg);

    // ---- compact index for fast grepping
    snprintf(path, sizeof(path), "%s\\index.txt", g_dumpDir);
    FILE* fi = fopen(path, "wb");
    if (fi) {
        for (auto& ci : index) {
            fprintf(fi, "%-58s parent=%-40s size=%-6u F=%-4d M=%-4d%s%s klass=%p\n",
                    ci.full.c_str(), ci.parent.c_str(), ci.size, ci.fieldCount, ci.methodCount,
                    ci.isMB ? " MB" : "", ci.isSO ? " SO" : "", (void*)ci.klass);
        }
        fclose(fi);
    }

    // ---- methodInfo layout probe: dump raw qwords of a few well-known methods
    snprintf(path, sizeof(path), "%s\\methodinfo_probe.txt", g_dumpDir);
    FILE* fp = fopen(path, "wb");
    if (fp) {
        size_t n2 = 0;
        const Il2CppAssembly** as2 = i2c_domain_get_assemblies(i2c_domain_get(), &n2);
        Il2CppImage* corlib = nullptr;
        Il2CppImage* game = nullptr;
        for (size_t i = 0; i < n2; i++) {
            Il2CppImage* im = i2c_assembly_get_image(as2[i]);
            const char* nm = i2c_image_get_name(im);
            if (nm && strcmp(nm, "mscorlib.dll") == 0) corlib = im;
            if (nm && strcmp(nm, "Assembly-CSharp.dll") == 0) game = im;
        }
        auto probe = [&](Il2CppImage* im, const char* ns, const char* cn, const char* mn, int argc) {
            if (!im) return;
            Il2CppClass* k = i2c_class_from_name(im, ns, cn);
            if (!k) { fprintf(fp, "class not found %s.%s\n", ns, cn); return; }
            MethodInfo* m = i2c_class_get_method_from_name(k, mn, argc);
            if (!m) { fprintf(fp, "method not found %s.%s::%s/%d\n", ns, cn, mn, argc); return; }
            fprintf(fp, "\n== %s.%s::%s argc=%d  MethodInfo=%p\n", ns, cn, mn, argc, (void*)m);
            for (int q = 0; q < 8; q++) {
                void* v = *((void**)m + q);
                fprintf(fp, "   [%d] %p %s\n", q * 8, v, i2c::InGameAssembly(v) ? "<-- in GameAssembly" : "");
            }
            void* mp = *(void**)m;
            if (i2c::InGameAssembly(mp)) {
                fprintf(fp, "   first 16 bytes at [0]: ");
                for (int b = 0; b < 16; b++) fprintf(fp, "%02X ", ((uint8_t*)mp)[b]);
                fprintf(fp, "\n");
            }
            void* mp1 = *((void**)m + 1);
            if (i2c::InGameAssembly(mp1)) {
                fprintf(fp, "   first 16 bytes at [8]: ");
                for (int b = 0; b < 16; b++) fprintf(fp, "%02X ", ((uint8_t*)mp1)[b]);
                fprintf(fp, "\n");
            }
        };
        probe(corlib, "System", "Object", "ToString", 0);
        probe(corlib, "System", "String", "get_Length", 0);
        probe(game, "UnityEngine", "Time", "get_deltaTime", 0);
        if (game) {
            Il2CppClass* tk = i2c_class_from_name(game, "UnityEngine", "Time");
            if (!tk) {
                // Time lives in CoreModule
                for (size_t i = 0; i < n2; i++) {
                    Il2CppImage* im = i2c_assembly_get_image(as2[i]);
                    const char* nm = i2c_image_get_name(im);
                    if (nm && strcmp(nm, "UnityEngine.CoreModule.dll") == 0) {
                        tk = i2c_class_from_name(im, "UnityEngine", "Time");
                        probe(im, "UnityEngine", "Time", "set_timeScale", 1);
                        break;
                    }
                }
            }
            if (tk) {
                void* sfd = i2c_class_get_static_field_data(tk);
                fprintf(fp, "\nUnityEngine.Time static_field_data = %p\n", sfd);
                void* it = nullptr;
                while (FieldInfo* fi = i2c_class_get_fields(tk, &it)) {
                    fprintf(fp, "   F %s off=0x%zX flags=0x%X\n", SafeName(i2c_field_get_name(fi)),
                            i2c_field_get_offset(fi), i2c_field_get_flags(fi));
                }
            }
        }
        fclose(fp);
    }
}

// ------------------------------------------------------------------ entry
static DWORD WINAPI Worker(LPVOID) {
    SelfDir(g_dumpDir, sizeof(g_dumpDir));
    strncat_s(g_dumpDir, "\\..\\dump", sizeof(g_dumpDir) - strlen(g_dumpDir) - 1);
    EnsureDir(g_dumpDir);

    // wait for IL2CPP runtime to come up
    for (int i = 0; i < 600; i++) {
        if (GetModuleHandleA("GameAssembly.dll") && i2c::Init()) {
            Il2CppDomain* d = i2c_domain_get();
            if (d) {
                i2c_thread_attach(d);
                size_t n = 0;
                const Il2CppAssembly** as = i2c_domain_get_assemblies(d, &n);
                if (as && n > 0) {
                    Sleep(200);
                    DumpAll();
                    char flag[MAX_PATH];
                    snprintf(flag, sizeof(flag), "%s\\DONE.txt", g_dumpDir);
                    FILE* ff = fopen(flag, "wb");
                    if (ff) { fprintf(ff, "ok\n"); fclose(ff); }
                    return 0;
                }
            }
        }
        Sleep(250);
    }
    return 1;
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
