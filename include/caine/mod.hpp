// Copyright (c) 2026 Teirdalin.
// SPDX-License-Identifier: LicenseRef-JDL-1
// Additional plugin-development permission: PLUGIN_API_PERMISSION.md.
#pragma once
#include <caine/mod_api.h>
#include <filesystem>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>
namespace caine::sdk {
// Compiled into each mod. CAINE retains all hook ownership.
inline const CaineHostV1* host{};
inline void Initialize(const CaineHostV1* value) {
    if (!value || value->size < sizeof(CaineHostV1) || value->abiVersion != CAINE_MOD_ABI_V1)
        throw std::runtime_error("Unsupported PROJECT CAINE host");
    host = value;
}
inline void Log(const std::string& message) { host->log(host->context, message.c_str()); }
struct Module {
    HMODULE handle{};
    uint8_t* base{};
    size_t size{};
    std::string sha256;
    static Module Inspect(HMODULE handle) {
        CaineModuleV1 info{}; info.size = sizeof(info);
        if (!host->inspectModule(host->context, handle, &info))
            throw std::runtime_error("CAINE module inspection failed");
        return {info.handle, info.base, info.imageSize, info.sha256};
    }
};
struct HookSpec { std::string id, moduleSha256; size_t rva{}; std::vector<uint8_t> expected; };
struct HookRequest { HookSpec spec; void* detour; void** original; };
class Hooks {
public:
    bool InstallBatch(const Module& module, const std::vector<HookRequest>& requests, std::string& error) {
        std::vector<CaineHookV1> batch;
        for (const auto& entry : requests) {
            if (entry.spec.rva > UINT32_MAX || entry.spec.expected.size() > UINT32_MAX) {
                error = "Hook exceeds x86 bounds"; return false;
            }
            batch.push_back({sizeof(CaineHookV1), entry.spec.id.c_str(), entry.spec.moduleSha256.c_str(),
                static_cast<uint32_t>(entry.spec.rva), entry.spec.expected.data(),
                static_cast<uint32_t>(entry.spec.expected.size()), entry.detour, entry.original});
        }
        char message[1024]{};
        const auto result = host->installHooks(host->context, module.handle, batch.data(),
            static_cast<uint32_t>(batch.size()), message, sizeof(message));
        error = message;
        return result != 0;
    }
};
}
