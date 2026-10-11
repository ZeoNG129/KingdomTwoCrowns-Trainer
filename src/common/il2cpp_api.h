// IL2CPP runtime API bindings (resolved dynamically from GameAssembly.dll)
// Kingdom Two Crowns - Unity 6000.0.66f2 IL2CPP x64
#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>

struct Il2CppClass;
struct Il2CppImage;
struct Il2CppAssembly;
struct FieldInfo;
struct MethodInfo;
struct Il2CppType;
struct Il2CppDomain;
struct Il2CppObject;
struct Il2CppArray;
struct Il2CppThread;

// X(name, return_type, (params))
#define I2C_FUNCS(X)                                                                        \
    X(domain_get,                 Il2CppDomain*,     ())                                     \
    X(thread_attach,              Il2CppThread*,     (Il2CppDomain* domain))                 \
    X(domain_get_assemblies,      const Il2CppAssembly**, (const Il2CppDomain* domain, size_t* size)) \
    X(assembly_get_image,         Il2CppImage*,      (const Il2CppAssembly* assembly))        \
    X(image_get_name,             const char*,       (const Il2CppImage* image))              \
    X(image_get_class_count,      size_t,            (const Il2CppImage* image))              \
    X(image_get_class,            const Il2CppClass*, (const Il2CppImage* image, size_t index)) \
    X(class_from_name,            Il2CppClass*,      (const Il2CppImage* image, const char* ns, const char* name)) \
    X(class_get_name,             const char*,       (Il2CppClass* klass))                    \
    X(class_get_namespace,        const char*,       (Il2CppClass* klass))                    \
    X(class_get_parent,           Il2CppClass*,      (Il2CppClass* klass))                    \
    X(class_get_flags,            uint32_t,          (Il2CppClass* klass))                    \
    X(class_get_fields,           FieldInfo*,        (Il2CppClass* klass, void** iter))       \
    X(class_get_field_from_name,  FieldInfo*,        (Il2CppClass* klass, const char* name))  \
    X(class_get_methods,          const MethodInfo*, (Il2CppClass* klass, void** iter))       \
    X(class_get_method_from_name, MethodInfo*,       (Il2CppClass* klass, const char* name, int argsCount)) \
    X(class_get_static_field_data,void*,             (Il2CppClass* klass))                    \
    X(class_instance_size,        uint32_t,          (Il2CppClass* klass))                    \
    X(class_is_enum,              bool,              (Il2CppClass* klass))                    \
    X(class_is_subclass_of,       bool,              (Il2CppClass* klass, Il2CppClass* parent, bool check_interfaces)) \
    X(class_is_valuetype,         bool,              (Il2CppClass* klass))                    \
    X(class_get_type,             const Il2CppType*, (Il2CppClass* klass))                    \
    X(class_get_image,            Il2CppImage*,      (Il2CppClass* klass))                    \
    X(class_get_declaring_type,   Il2CppClass*,      (Il2CppClass* klass))                    \
    X(field_get_name,             const char*,       (FieldInfo* field))                      \
    X(field_get_offset,           size_t,            (FieldInfo* field))                      \
    X(field_get_flags,            uint32_t,          (FieldInfo* field))                      \
    X(field_get_type,             const Il2CppType*, (FieldInfo* field))                      \
    X(field_get_value,            void,              (Il2CppObject* obj, FieldInfo* field, void* value)) \
    X(field_set_value,            void,              (Il2CppObject* obj, FieldInfo* field, void* value)) \
    X(field_static_get_value,     void,              (FieldInfo* field, void* value))         \
    X(field_static_set_value,     void,              (FieldInfo* field, void* value))         \
    X(method_get_name,            const char*,       (MethodInfo* method))                    \
    X(method_get_param_count,     uint32_t,          (const MethodInfo* method))              \
    X(method_get_flags,           uint32_t,          (const MethodInfo* method, uint32_t* iflags)) \
    X(method_get_return_type,     const Il2CppType*, (const MethodInfo* method))              \
    X(method_get_param,           const Il2CppType*, (const MethodInfo* method, uint32_t index)) \
    X(method_is_generic,          bool,              (const MethodInfo* method))              \
    X(type_get_name,              char*,             (const Il2CppType* type))                \
    X(type_get_object,            Il2CppObject*,     (const Il2CppType* type))                \
    X(runtime_invoke,             Il2CppObject*,     (const MethodInfo* method, void* obj, void** params, Il2CppObject** exc)) \
    X(object_new,                 Il2CppObject*,     (Il2CppClass* klass))                    \
    X(string_new,                 Il2CppObject*,     (const char* str))                       \
    X(array_new,                  Il2CppArray*,      (Il2CppClass* elementClass, size_t length)) \
    X(array_length,               size_t,            (Il2CppArray* array))                    \
    X(array_element_size,         int32_t,           (const Il2CppClass* klass))              \
    X(class_is_assignable_from,   bool,              (Il2CppClass* klass, Il2CppClass* klassc)) \
    X(free,                       void,              (void* ptr))                             \
    X(gc_disable,                 void,              ())                                      \
    X(gchandle_new,               int32_t,           (Il2CppObject* obj, bool pinned))        \
    X(gchandle_get_target,        Il2CppObject*,     (int32_t handle))                        \
    X(gchandle_free,              void,              (int32_t handle))                        \
    X(resolve_icall,              void*,             (const char* name))                      \
    X(value_box,                  Il2CppObject*,     (Il2CppClass* klass, void* data))        \
    X(object_unbox,               void*,             (Il2CppObject* obj))

#define I2C_DECL(name, ret, args) typedef ret (*i2c_##name##_t) args; inline i2c_##name##_t i2c_##name = nullptr;
I2C_FUNCS(I2C_DECL)
#undef I2C_DECL

namespace i2c {

inline HMODULE g_module = nullptr;
inline uintptr_t g_base = 0;
inline size_t g_size = 0;

inline bool Init() {
    HMODULE h = GetModuleHandleA("GameAssembly.dll");
    if (!h) return false;
    g_module = h;
    g_base = (uintptr_t)h;
    // SizeOfImage from PE headers
    auto dos = (IMAGE_DOS_HEADER*)h;
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) return false;
    auto nt = (IMAGE_NT_HEADERS64*)((uint8_t*)h + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) return false;
    g_size = nt->OptionalHeader.SizeOfImage;

    bool ok = true;
#define I2C_LOAD(name, ret, args)                                                    \
    i2c_##name = (i2c_##name##_t)GetProcAddress(h, "il2cpp_" #name);                 \
    if (!i2c_##name) { ok = false; }
    I2C_FUNCS(I2C_LOAD)
#undef I2C_LOAD
    return ok;
}

// Is a raw pointer inside GameAssembly.dll's image (i.e. real IL2CPP native code)?
inline bool InGameAssembly(const void* p) {
    uintptr_t v = (uintptr_t)p;
    return v >= g_base && v < g_base + g_size;
}

} // namespace i2c
