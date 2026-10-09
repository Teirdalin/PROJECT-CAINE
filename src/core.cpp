#include <caine/core.hpp>
#include <caine/logging.hpp>
#include <MinHook.h>
#include <bcrypt.h>
#include <psapi.h>
#include <algorithm>
#include <array>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <stdexcept>
#include <mutex>

namespace caine {
namespace {
std::mutex hookOwnerMutex;
size_t hookOwners{};
}
Pattern ParsePattern(const std::string& text) {
    Pattern result;
    std::istringstream stream(text);
    std::string token;
    bool concrete = false;
    while (stream >> token) {
        if (token == "?" || token == "??") { result.push_back(-1); continue; }
        if (token.size() != 2 || token.find_first_not_of("0123456789abcdefABCDEF") != std::string::npos)
            throw std::invalid_argument("pattern must contain hex byte pairs or ??");
        result.push_back(std::stoi(token, nullptr, 16));
        concrete = true;
    }
    if (!concrete) throw std::invalid_argument("pattern must contain a concrete byte");
    return result;
}

std::vector<size_t> FindPattern(const std::vector<uint8_t>& bytes, const Pattern& pattern) {
    std::vector<size_t> matches;
    if (pattern.empty() || bytes.size() < pattern.size()) return matches;
    for (size_t i = 0; i <= bytes.size() - pattern.size(); ++i) {
        bool match = true;
        for (size_t j = 0; j < pattern.size(); ++j)
            if (pattern[j] != -1 && pattern[j] != bytes[i + j]) { match = false; break; }
        if (match) matches.push_back(i);
    }
    return matches;
}

std::string Sha256(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("cannot open module for hashing");
    BCRYPT_ALG_HANDLE algorithm{};
    BCRYPT_HASH_HANDLE hash{};
    if (BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) < 0)
        throw std::runtime_error("BCryptOpenAlgorithmProvider failed");
    std::vector<uint8_t> object;
    struct Cleanup {
        BCRYPT_ALG_HANDLE a; BCRYPT_HASH_HANDLE& h;
        ~Cleanup() { if (h) BCryptDestroyHash(h); BCryptCloseAlgorithmProvider(a, 0); }
    } cleanup{algorithm, hash};
    DWORD objectLength{}, returned{};
    if (BCryptGetProperty(algorithm, BCRYPT_OBJECT_LENGTH, reinterpret_cast<PUCHAR>(&objectLength), sizeof(objectLength), &returned, 0) < 0)
        throw std::runtime_error("BCryptGetProperty failed");
    object.resize(objectLength);
    if (BCryptCreateHash(algorithm, &hash, object.data(), objectLength, nullptr, 0, 0) < 0)
        throw std::runtime_error("BCryptCreateHash failed");
    std::array<char, 65536> buffer{};
    while (input.read(buffer.data(), buffer.size()) || input.gcount()) {
        if (BCryptHashData(hash, reinterpret_cast<PUCHAR>(buffer.data()), static_cast<ULONG>(input.gcount()), 0) < 0)
            throw std::runtime_error("BCryptHashData failed");
    }
    if (!input.eof()) throw std::runtime_error("module read failed");
    std::array<uint8_t, 32> digest{};
    if (BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) < 0)
        throw std::runtime_error("BCryptFinishHash failed");
    std::ostringstream out;
    for (auto byte : digest) out << std::hex << std::setw(2) << std::setfill('0') << static_cast<int>(byte);
    return out.str();
}

std::filesystem::path ModulePath(HMODULE module) {
    std::vector<wchar_t> path(32768);
    const auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length >= path.size()) throw std::runtime_error("GetModuleFileNameW failed");
    return std::filesystem::path(std::wstring(path.data(), length));
}

Module Module::Inspect(HMODULE handle) {
    MODULEINFO info{};
    if (!handle || !GetModuleInformation(GetCurrentProcess(), handle, &info, sizeof(info)))
        throw std::runtime_error("module is not loaded");
    Module module{handle, static_cast<uint8_t*>(info.lpBaseOfDll), info.SizeOfImage, ModulePath(handle), {}};
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(module.base);
    if (module.size < sizeof(IMAGE_NT_HEADERS32) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 0 ||
        static_cast<size_t>(dos->e_lfanew) > module.size - sizeof(IMAGE_NT_HEADERS32))
        throw std::runtime_error("invalid DOS header");
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(module.base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386 || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC)
        throw std::runtime_error("module is not an x86 PE image");
    module.sha256 = Sha256(module.path);
    return module;
}

bool Module::Executable(size_t rva, size_t count) const {
    if (!count || rva >= size || count > size - rva) return false;
    size_t checked = 0;
    while (checked < count) {
        MEMORY_BASIC_INFORMATION region{};
        const auto p = base + rva + checked;
        if (!VirtualQuery(p, &region, sizeof(region)) || region.State != MEM_COMMIT || region.AllocationBase != base ||
            (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
        const DWORD access = region.Protect & 0xff;
        if (access != PAGE_EXECUTE_READ && access != PAGE_EXECUTE_READWRITE && access != PAGE_EXECUTE_WRITECOPY) return false;
        checked += std::min(count - checked, static_cast<size_t>(static_cast<uint8_t*>(region.BaseAddress) + region.RegionSize - p));
    }
    return true;
}

std::vector<size_t> Module::ScanExecutable(const Pattern& pattern) const {
    std::vector<size_t> result;
    const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base + dos->e_lfanew);
    const auto sections = IMAGE_FIRST_SECTION(nt);
    const auto sectionOffset = reinterpret_cast<const uint8_t*>(sections) - base;
    if (static_cast<size_t>(sectionOffset) > size || nt->FileHeader.NumberOfSections > (size - sectionOffset) / sizeof(IMAGE_SECTION_HEADER))
        throw std::runtime_error("invalid section table");
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        const auto& section = sections[i];
        if (!(section.Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const size_t start = section.VirtualAddress;
        const size_t length = section.Misc.VirtualSize;
        if (!Executable(start, length)) continue;
        std::vector<uint8_t> copy(base + start, base + start + length);
        for (const auto offset : FindPattern(copy, pattern)) result.push_back(start + offset);
    }
    return result;
}

Hooks::Hooks() {
    std::lock_guard<std::mutex> lock(hookOwnerMutex);
    if (!hookOwners) {
        const auto status = MH_Initialize();
        if (status != MH_OK) throw std::runtime_error(MH_StatusToString(status));
    }
    ++hookOwners;
}
Hooks::~Hooks() {
    // Callers must quiesce detours before destruction. Production keeps this owner for process lifetime.
    std::string error;
    if (RemoveAll(error)) {
        std::lock_guard<std::mutex> lock(hookOwnerMutex);
        if (--hookOwners == 0) MH_Uninitialize();
    }
}
bool Hooks::Install(const Module& module, const HookSpec& spec, void* detour, void** original, std::string& error) {
    return InstallBatch(module, {{spec, detour, original}}, error);
}
bool Hooks::InstallBatch(const Module& module, const std::vector<HookRequest>& requests, std::string& error) {
    // MinHook's enable queue is global, including across independent owners.
    std::lock_guard<std::mutex> ownerLock(hookOwnerMutex);
    error.clear();
    std::string checking;
    struct Audit {
        std::string& error;std::string& checking;
        ~Audit() noexcept { try { if (!error.empty()) TraceLog("CAINE_HOOK_REJECTED: id="+checking+" reason="+error); } catch (...) {} }
    } audit{error,checking};
    TraceLog("CAINE_HOOK_BATCH_BEGIN: count="+std::to_string(requests.size())+" module_sha256="+module.sha256);
    if (requests.empty()) { error = "empty hook batch"; return false; }
    if (Sha256(module.path)!=module.sha256) { error="module file changed after discovery";return false; }
    std::vector<Target> staged;
    // Finish all potentially throwing allocations/validation before patching or staging hooks.
    staged.reserve(requests.size());
    targets_.reserve(targets_.size() + requests.size());
    for (const auto& request : requests) {
    const auto& spec = request.spec;
    checking=spec.id;
    TraceLog("CAINE_HOOK_VALIDATE: id="+spec.id+" rva="+std::to_string(spec.rva)+" expected_bytes="+std::to_string(spec.expected.size()));
    auto detour = request.detour;
    auto original = request.original;
    if (!original) { error = "original trampoline output is required"; return false; }
    if (!detour || spec.id.empty() || spec.moduleSha256.size() != 64 || spec.moduleSha256 != module.sha256) {
        error = "unreviewed module identity or invalid hook arguments"; return false;
    }
    // Disk identity is checked once for the complete batch; live bytes are
    // independently checked for every target below.
    if (spec.expected.size() < 8 || !module.Executable(spec.rva, spec.expected.size())) {
        error = "hook target is out of range, unreadable, or not executable; eight exact bytes required"; return false;
    }
    void* target = module.base + spec.rva;
    for (const auto& installed : targets_) if (installed.address == target || installed.id == spec.id) {
        error = "duplicate hook"; return false;
    }
    for (const auto& entry : staged) if (entry.address == target || entry.id == spec.id) {
        error = "duplicate target in hook batch"; return false;
    }
    for (size_t i=0;i<staged.size();++i) if (requests[i].original==original) {
        error="each hook requires a distinct trampoline output";return false;
    }
    if (std::memcmp(target, spec.expected.data(), spec.expected.size()) != 0) {
        error = "target bytes differ (build mismatch or another mod owns this entry point)"; return false;
    }
    staged.push_back({target, spec.id});
    }
    size_t created = 0;
    MH_STATUS result = MH_OK;
    for (size_t i = 0; i < requests.size(); ++i) {
        result = MH_CreateHook(staged[i].address, requests[i].detour, requests[i].original);
        if (result != MH_OK) break;
        ++created;
        result = MH_QueueEnableHook(staged[i].address);
        if (result != MH_OK) break;
    }
    if (result != MH_OK) {
        // Nothing in this batch has run. Remove staged hooks and their queued
        // enable requests, so another owner cannot activate a rejected batch.
        for (size_t i=0;i<created;++i) {
            MH_QueueDisableHook(staged[i].address);
            MH_RemoveHook(staged[i].address);
            *requests[i].original=nullptr;
        }
        error=MH_StatusToString(result);return false;
    }
    result = MH_ApplyQueued();
    if (result != MH_OK) {
        // ApplyQueued can fail midway after activating some targets. Clear all
        // queued enables and disable these entries, retaining trampolines until
        // the caller can guarantee that running callbacks have quiesced.
        for (size_t i=0;i<created;++i) { MH_QueueDisableHook(staged[i].address);MH_DisableHook(staged[i].address); }
        for (size_t i = 0; i < created; ++i) targets_.push_back(std::move(staged[i]));
        error = MH_StatusToString(result);
        return false;
    }
    for (auto& entry : staged) { TraceLog("CAINE_HOOK_ENABLED: id="+entry.id);targets_.push_back(std::move(entry)); }
    return true;
}
bool Hooks::RemoveAll(std::string& error) {
    std::lock_guard<std::mutex> ownerLock(hookOwnerMutex);
    error.clear();
    while (!targets_.empty()) {
        const auto target = targets_.back().address;
        auto status = MH_DisableHook(target);
        if (status != MH_OK && status != MH_ERROR_DISABLED) { error = MH_StatusToString(status); return false; }
        status = MH_RemoveHook(target);
        if (status != MH_OK) { error = MH_StatusToString(status); return false; }
        targets_.pop_back();
        TraceLog("CAINE_HOOK_REMOVED: remaining="+std::to_string(targets_.size()));
    }
    return true;
}
bool Hooks::DisableAll(std::string& error) {
    std::lock_guard<std::mutex> ownerLock(hookOwnerMutex);
    error.clear();
    for (const auto& target:targets_) {
        MH_QueueDisableHook(target.address);
        const auto status=MH_DisableHook(target.address);
        if (status!=MH_OK && status!=MH_ERROR_DISABLED) error=MH_StatusToString(status);
    }
    return error.empty();
}
}
