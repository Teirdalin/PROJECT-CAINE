#pragma once
#include <windows.h>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace caine {
// -1 is a wildcard. Empty/malformed patterns are rejected.
using Pattern = std::vector<int>;
Pattern ParsePattern(const std::string& text);
std::vector<size_t> FindPattern(const std::vector<uint8_t>& bytes, const Pattern& pattern);
std::string Sha256(const std::filesystem::path& file);
std::filesystem::path ModulePath(HMODULE module);

struct Module {
    HMODULE handle{};
    uint8_t* base{};
    size_t size{};
    std::filesystem::path path;
    std::string sha256;
    static Module Inspect(HMODULE handle);
    bool Executable(size_t rva, size_t count) const;
    std::vector<size_t> ScanExecutable(const Pattern& pattern) const;
};

struct HookSpec {
    std::string id;
    std::string moduleSha256;
    size_t rva{};
    // Exact bytes, no wildcards. Include at least eight reviewed bytes.
    std::vector<uint8_t> expected;
};
struct HookRequest { HookSpec spec; void* detour; void** original; };

// One owner per runtime. Must be used on a serialized initialization/control path.
// Never destroy/remove while detours are executing; restart the game to update CAINE.
class Hooks {
public:
    Hooks();
    ~Hooks();
    Hooks(const Hooks&) = delete;
    Hooks& operator=(const Hooks&) = delete;
    bool Install(const Module& module, const HookSpec& spec, void* detour, void** original, std::string& error);
    // Create/validate every trampoline before enabling any hook in this batch.
    bool InstallBatch(const Module& module, const std::vector<HookRequest>& requests, std::string& error);
    bool RemoveAll(std::string& error);
    size_t Count() const { return targets_.size(); }
private:
    struct Target { void* address; std::string id; };
    std::vector<Target> targets_;
};
}
