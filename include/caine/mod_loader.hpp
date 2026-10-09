#pragma once
#include <filesystem>
#include <functional>
#include <string>
#include <vector>
#include <caine/mod_api.h>
namespace caine {
struct ModInfo {
    std::string id, name, version, author, description, state;
    std::filesystem::path directory, binary, config, logo;
    uint32_t minimumVersion{};
    bool enabled{}, active{};
};
ModInfo ReadModInfo(const std::filesystem::path& folder);
void LoadMods(const std::filesystem::path& directory, const std::filesystem::path& game,
              const std::function<void(const std::string&)>& log);
void TickMods();
std::vector<ModInfo> ModCatalog();
bool SetModEnabled(const std::string& id, bool enabled, std::string& error);
bool ModMenu(const std::string& id, const CaineMenuV1* menu, uint32_t event, uint32_t value);
std::string BlockingMod();
void PulseModMenus();
std::string ActiveGameUI();
bool ModGameUI(const std::string& id, const CaineMenuV1* menu, uint32_t event, uint32_t value);
bool InstallModsMenu(const std::function<void(const std::string&)>& log);
}

