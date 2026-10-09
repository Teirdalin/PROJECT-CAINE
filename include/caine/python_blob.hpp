#pragma once
#include <windows.h>
#include <cstdint>
#include <string>
#include <caine/native_types.hpp>
namespace caine::native {
constexpr size_t MaxSnapshot = 64 * 1024 * 1024;
constexpr size_t ChunkBytes = 48;
using Object=PythonObject;
// CPython 2.1 x86 API and bounded representation shared by runtime and regression host.
class SnapshotCodec {
public:
    explicit SnapshotCodec(std::string marker="CAINE.blob.hex.v1") : marker_(std::move(marker)) {}
    void Initialize(HMODULE python);
    Object* Encode(const std::string& value) const;
    std::string Decode(Object* value) const;
    void Release(Object* value) const;
    Object* String(const std::string& value) const;
    Object* Error(const char* message) const noexcept;
private:
    std::string marker_;
    Object* (__cdecl* string_)(const char*, int){};
    int (__cdecl* bytes_)(Object*, char**, int*){};
    Object* (__cdecl* list_)(int){};
    int (__cdecl* size_)(Object*){};
    Object* (__cdecl* get_)(Object*, int){};
    int (__cdecl* set_)(Object*, int, Object*){};
    void (__cdecl* error_)(Object*, const char*){};
    Object** exception_{};
    void *stringType_{}, *listType_{};
    std::string Bytes(Object* value, size_t maximum) const;
};
}
