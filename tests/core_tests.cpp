#include <caine/core.hpp>
#include <iostream>
#include <fstream>
#include <stdexcept>
#include <cstring>
#include <algorithm>

using Add = int (__cdecl*)(int);
Add original{};
int __cdecl Detour(int value) { return original(value) + 100; }
void Require(bool ok, const char* message) { if (!ok) throw std::runtime_error(message); }
int wmain(int argc, wchar_t** argv) {
    try {
        Require(argc == 2, "fixture path required");
        const auto hashFile = std::filesystem::temp_directory_path() / (L"caine-hash-" + std::to_wstring(GetCurrentProcessId()));
        { std::ofstream file(hashFile, std::ios::binary); file << "abc"; }
        const auto hash = caine::Sha256(hashFile);
        std::filesystem::remove(hashFile);
        Require(hash == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA256 known vector");
        const auto pattern = caine::ParsePattern("AA ?? cC");
        Require(caine::FindPattern({0xaa, 0, 0xcc, 0xaa, 1, 0xcc}, pattern) == std::vector<size_t>{0, 3}, "all pattern matches including final boundary");
        Require(caine::FindPattern({0xaa}, pattern).empty(), "short buffer");
        for (const auto text : {"", "?? ?", "A", "GG", "001"}) {
            bool rejected = false;
            try { caine::ParsePattern(text); } catch (const std::invalid_argument&) { rejected = true; }
            Require(rejected, "invalid pattern rejected");
        }
        HMODULE fixture = LoadLibraryW(argv[1]);
        Require(fixture != nullptr, "load fixture");
        const auto module = caine::Module::Inspect(fixture);
        auto add = reinterpret_cast<Add>(GetProcAddress(fixture, "FixtureAdd"));
        Require(add && add(5) == 22, "unhooked function");
        const auto bytes = reinterpret_cast<uint8_t*>(add);
        caine::HookSpec spec{"fixture.add", module.sha256, static_cast<size_t>(bytes - module.base), {bytes, bytes + 12}};
        caine::Hooks hooks;
        std::string error;
        auto bad = spec; bad.moduleSha256 = std::string(64, '0');
        Require(!hooks.Install(module, bad, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original), error), "unknown hash rejected");
        bad = spec; bad.expected[0] ^= 0xff;
        Require(!hooks.Install(module, bad, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original), error), "changed prologue rejected");
        bad = spec; bad.rva = module.size - 1;
        Require(!hooks.Install(module, bad, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original), error), "out of image rejected");
        bad = spec; bad.rva = 0;
        Require(!hooks.Install(module, bad, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original), error), "non executable header rejected");
        Require(add(5) == 22 && hooks.Count() == 0, "failed hooks leave function untouched");
        Require(hooks.Install(module, spec, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original), error), error.c_str());
        Require(add(5) == 122 && original(5) == 22, "real detour and trampoline");
        {
            caine::Hooks secondOwner;
            Require(add(5) == 122, "second hook owner preserves existing hooks");
        }
        Require(add(5) == 122 && original(5) == 22, "destroying second owner preserves first owner's trampoline");
        Require(!hooks.Install(module, spec, reinterpret_cast<void*>(Detour), reinterpret_cast<void**>(&original), error), "duplicate rejected");
        Require(add(5) == 122, "duplicate must not erase live trampoline");
        Require(hooks.RemoveAll(error), "remove hooks");
        Require(add(5) == 22 && std::memcmp(bytes, spec.expected.data(), spec.expected.size()) == 0, "original bytes restored");
        caine::Pattern entry; for (const auto byte : spec.expected) entry.push_back(byte);
        const auto hits = module.ScanExecutable(entry);
        Require(std::find(hits.begin(), hits.end(), spec.rva) != hits.end(), "scan executable image");
        FreeLibrary(fixture);
        std::cout << "CAINE_GUARDED_HOOKS_OK: x86 detour, trampoline, rollback, hash/byte/range guards, patterns, SHA256\n";
        return 0;
    } catch (const std::exception& e) { std::cerr << "FAIL: " << e.what() << '\n'; return 1; }
}
