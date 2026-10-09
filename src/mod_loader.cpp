#include <caine/core.hpp>
#include <caine/mod_api.h>
#include <caine/mod_loader.hpp>
#include <caine/native_bridge.hpp>
#include <caine/python_bridge.hpp>
#include <caine/serialization.hpp>
#include <algorithm>
#include <map>
#include <memory>
#include <mutex>
namespace {
struct Mod {
    std::string id;
    caine::ModInfo info;
    std::wstring directory, game, config;
    CaineHostV1 host{};
    const CaineModV1* descriptor{};
    std::unique_ptr<caine::Hooks> hooks;
    std::map<HMODULE, caine::Module> modules;
    bool active{};
};
// Deliberately process-lifetime; no trampoline destruction under the loader lock.
auto& mods = *new std::vector<std::unique_ptr<Mod>>();
std::function<void(const std::string&)> logger;
DWORD controlThread{};
std::recursive_mutex catalogMutex;
bool Redirected(const std::filesystem::path& path) {
    const auto attr = GetFileAttributesW(path.c_str());
    return attr != INVALID_FILE_ATTRIBUTES && (attr & FILE_ATTRIBUTE_REPARSE_POINT) != 0;
}
void __cdecl Log(void* context, const char* message) noexcept {
    try { logger("[" + static_cast<Mod*>(context)->id + "] " + (message ? message : "")); } catch (...) {}
}
const caine::Module& Inspect(Mod& mod, HMODULE module) {
    if (!module) throw std::runtime_error("Missing module");
    auto found = mod.modules.find(module);
    if (found != mod.modules.end()) return found->second;
    HMODULE pinned{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                           reinterpret_cast<LPCWSTR>(module), &pinned))
        throw std::runtime_error("Cannot pin inspected module");
    return mod.modules.emplace(module, caine::Module::Inspect(module)).first->second;
}
int __cdecl InspectModule(void* context, HMODULE module, CaineModuleV1* output) noexcept {
    try {
        if (GetCurrentThreadId() != controlThread || !output || output->size < sizeof(CaineModuleV1)) return 0;
        const auto& info = Inspect(*static_cast<Mod*>(context), module);
        output->handle = info.handle; output->base = info.base;
        output->imageSize = static_cast<uint32_t>(info.size);
        strcpy_s(output->sha256, info.sha256.c_str());
        return 1;
    } catch (...) { return 0; }
}
int __cdecl InstallHooks(void* context, HMODULE module, const CaineHookV1* requests,
                         uint32_t count, char* output, uint32_t outputSize) noexcept {
    std::string error;
    bool result = false;
    try {
        if (GetCurrentThreadId() != controlThread) throw std::runtime_error("Hooks require the CAINE control worker");
        if (!requests || count == 0 || count > 128) throw std::runtime_error("Invalid hook batch");
        auto& mod = *static_cast<Mod*>(context);
        std::vector<caine::HookRequest> batch;
        for (uint32_t i = 0; i < count; ++i) {
            const auto& row = requests[i];
            if (row.size < sizeof(CaineHookV1) || !row.id || !row.moduleSha256 ||
                !row.expected || row.expectedSize < 8 || row.expectedSize > 256)
                throw std::runtime_error("Invalid hook descriptor");
            batch.push_back({{mod.id + ":" + row.id, row.moduleSha256, row.rva,
                std::vector<uint8_t>(row.expected, row.expected + row.expectedSize)}, row.detour, row.original});
        }
        if (!mod.hooks) mod.hooks = std::make_unique<caine::Hooks>();
        result = mod.hooks->InstallBatch(Inspect(mod, module), batch, error);
    } catch (const std::exception& failure) { error = failure.what(); }
      catch (...) { error = "Mod hook registration failed"; }
    if (output && outputSize) strncpy_s(output, outputSize, error.c_str(), _TRUNCATE);
    return result ? 1 : 0;
}
int __cdecl ReadDialogue(void*,CaineDialogueV1* output) noexcept {
    try { return output && output->size>=sizeof(CaineDialogueV1) && caine::ReadNativeDialogue(*output); } catch (...) { return 0; }
}
int __cdecl ClaimDialogue(void* context,uint64_t token,int enabled) noexcept {
    try { return caine::ClaimNativeDialogue(context,token,enabled!=0); } catch (...) { return 0; }
}
int __cdecl QueuePick(void* context,uint64_t token,int index) noexcept {
    try { return caine::QueueNativeDialoguePick(context,token,index); } catch (...) { return 0; }
}
int __cdecl ReadScalar(void*,uint32_t kind,const char* name,CaineScalarV1* output) noexcept {
    try { return output?caine::ReadScriptScalar(kind,name,*output):0; } catch (...) { return 0; }
}
uint32_t __cdecl Serialize(void* context,const char* input,uint32_t bytes,char* output,uint32_t capacity) noexcept {
    try {
        if (!input || !bytes || bytes>64u*1024u*1024u) return 0;
        const auto value=caine::SerializeOwned(std::string(input,bytes));
        const auto needed=static_cast<uint32_t>(value.size()+1);
        if (output && capacity>=needed) memcpy(output,value.c_str(),needed);
        return needed;
    } catch (const std::exception& error) { Log(context,error.what());return 0; }
      catch (...) { return 0; }
}
int __cdecl TypeInfo(void*,uint32_t index,CaineTypeV1* output) noexcept {
    if (!output || output->size<sizeof(CaineTypeV1)) return 0;
    static const std::pair<const char*,uint32_t> types[]={
        {"caine.scalar",CAINE_TYPE_SERIALIZABLE},{"caine.value",CAINE_TYPE_SERIALIZABLE},
        {"caine.entity",CAINE_TYPE_SERIALIZABLE},{"caine.dialogue-context",CAINE_TYPE_SERIALIZABLE},
        {"bloodlines.python-object.x86",CAINE_TYPE_NATIVE_VIEW|CAINE_TYPE_LIVE_ONLY},
        {"bloodlines.python-save-file.x86",CAINE_TYPE_NATIVE_VIEW|CAINE_TYPE_LIVE_ONLY},
        {"bloodlines.entity-handle.x86",CAINE_TYPE_NATIVE_VIEW|CAINE_TYPE_LIVE_ONLY},
        {"bloodlines.dialogue.x86",CAINE_TYPE_NATIVE_VIEW|CAINE_TYPE_LIVE_ONLY|CAINE_TYPE_PARTIAL},
        {"bloodlines.hud-dialogue.x86",CAINE_TYPE_NATIVE_VIEW|CAINE_TYPE_LIVE_ONLY|CAINE_TYPE_PARTIAL}
    };
    if (index>=std::size(types)) return 0;
    *output={};output->size=sizeof(*output);output->version=1;output->flags=types[index].second;
    strcpy_s(output->name,types[index].first);return 1;
}

}
namespace caine {
void LoadMods(const std::filesystem::path& directory, const std::filesystem::path& game,
              const std::function<void(const std::string&)>& log) {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    logger = log; controlThread = GetCurrentThreadId();
    if (!std::filesystem::exists(directory)) return;
    if (Redirected(directory) || !std::filesystem::is_directory(directory))
        throw std::runtime_error("Invalid mod directory");
    std::vector<std::filesystem::path> folders;
    for (const auto& entry : std::filesystem::directory_iterator(directory))
        if (entry.is_directory() && !Redirected(entry.path())) folders.push_back(entry.path());
    std::sort(folders.begin(), folders.end());
    for (const auto& folder : folders) {
        auto item = std::make_unique<Mod>();
        item->id = folder.filename().string();
        item->info.id = item->id; item->info.name = item->id; item->info.directory = folder;
        auto& mod = *item;
        mods.push_back(std::move(item));
        const auto& id = mod.id;
        try {
            mod.info = ReadModInfo(folder);
            mod.id = mod.info.id;
            for (const auto& other : mods)
                if (other.get() != &mod && other->id == mod.id) {
                    // Give the rejected entry a distinct catalog key so its menu cannot
                    // accidentally configure or toggle the already-loaded original.
                    mod.id = "invalid:" + folder.filename().string();
                    mod.info.id = mod.id; mod.info.config.clear();
                    throw std::runtime_error("Duplicate mod ID");
                }
            if (mod.info.minimumVersion > CAINE_FRAMEWORK_VERSION) throw std::runtime_error("Requires newer PROJECT CAINE");
            if (!mod.info.enabled) {
                mod.info.state = "Disabled"; log("CAINE_MOD_DISABLED: " + id); continue;
            }
            const auto library = LoadLibraryExW(mod.info.binary.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            if (!library) throw std::runtime_error("DLL load failed");
            mod.directory = folder.wstring(); mod.game = game.wstring(); mod.config = mod.info.config.wstring();
            auto query = reinterpret_cast<CaineModQuery>(GetProcAddress(library, "CaineMod_Query"));
            if (!query) throw std::runtime_error("Missing CaineMod_Query");
            mod.descriptor = query(CAINE_MOD_ABI_V1);
            const auto desc = mod.descriptor;
            if (!desc || desc->size < offsetof(CaineModV1,gameUI) || desc->abiVersion != CAINE_MOD_ABI_V1 ||
                desc->minimumFrameworkVersion > CAINE_FRAMEWORK_VERSION || !desc->id ||
                id != desc->id || !desc->name || !desc->version || !desc->start || mod.info.version != desc->version)
                throw std::runtime_error("Incompatible mod descriptor or framework version");
            mod.host = {sizeof(CaineHostV1), CAINE_MOD_ABI_V1, CAINE_FRAMEWORK_VERSION, &mod,
                        mod.game.c_str(), mod.directory.c_str(), mod.config.c_str(), Log, InspectModule, InstallHooks,ReadDialogue,ClaimDialogue,QueuePick,ReadScalar,Serialize,TypeInfo};
            if (!desc->start(&mod.host)) throw std::runtime_error("Mod start declined");
            mod.active = true; mod.info.active = true; mod.info.state = "Loaded";
            log("CAINE_MOD_LOADED: " + id + " " + desc->version);
        } catch (const std::exception& failure) {
            mod.info.state = std::string("Unavailable: ") + failure.what();
            log("CAINE_MOD_REJECTED: " + id + " " + failure.what());
        } catch (...) { mod.info.state = "Unavailable: callback exception"; log("CAINE_MOD_REJECTED: " + id + " callback exception"); }
    }
}
void TickMods() {
    // The catalog is immutable after startup. Never hold its UI lock across a mod's
    // control callback, which may be waiting for its own asynchronous service.
    for (auto& mod : mods) {
        void (__cdecl* tick)(){};
        {
            std::lock_guard<std::recursive_mutex> lock(catalogMutex);
            if (mod->active) tick = mod->descriptor->tick;
        }
        if (!tick) continue;
        try { tick(); }
        catch (...) {
            std::lock_guard<std::recursive_mutex> lock(catalogMutex);
            mod->active = false; mod->info.active = false; mod->info.state = "Failed: restart required";
            logger("CAINE_MOD_STOPPED: " + mod->id + " tick exception; restart required");
        }
    }
}
std::vector<ModInfo> ModCatalog() {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    std::vector<ModInfo> result;
    for (const auto& mod : mods) result.push_back(mod->info);
    return result;
}
bool SetModEnabled(const std::string& id, bool enabled, std::string& error) {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    for (auto& mod : mods) if (mod->id == id) {
        const auto& path = mod->info.config;
        if (path.empty() || Redirected(path) || Redirected(path.parent_path()) || Redirected(path.parent_path().parent_path())) {
            error = "Configuration path is unavailable"; return false;
        }
        if (!WritePrivateProfileStringW(L"Mod", L"Enabled", enabled ? L"1" : L"0", path.c_str())) {
            error = "Cannot write configuration"; return false;
        }
        mod->info.enabled = enabled;
        error.clear(); return true;
    }
    error = "Mod was not found"; return false;
}
bool ModMenu(const std::string& id, const CaineMenuV1* menu, uint32_t event, uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    for (auto& mod : mods) if (mod->id == id && mod->active && mod->descriptor->configure) {
        try { return mod->descriptor->configure(menu, event, value) != 0; }
        catch (...) { logger("CAINE_MOD_MENU_FAILED: " + id); return false; }
    }
    return false;
}
std::string BlockingMod() {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    for (auto& mod : mods) if (mod->active && mod->descriptor->canEnterGame) {
        try { if (!mod->descriptor->canEnterGame()) return mod->id; } catch (...) { return mod->id; }
    }
    return {};
}
void PulseModMenus() {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    for (auto& mod : mods) if (mod->active && mod->descriptor->menuFrame) {
        try { mod->descriptor->menuFrame(); } catch (...) { logger("CAINE_MOD_FRAME_FAILED: " + mod->id); }
    }
}
std::string ActiveGameUI() {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    for (auto& mod:mods) if (mod->active && mod->descriptor->size>=sizeof(CaineModV1) && mod->descriptor->gameUI) {
        try { if (mod->descriptor->gameUI(nullptr,CAINE_GAMEUI_POLL,0)) return mod->id; }
        catch (...) { logger("CAINE_MOD_GAME_UI_FAILED: "+mod->id); }
    }
    return {};
}
bool ModGameUI(const std::string& id,const CaineMenuV1* menu,uint32_t event,uint32_t value) {
    std::lock_guard<std::recursive_mutex> lock(catalogMutex);
    for (auto& mod:mods) if (mod->id==id && mod->active && mod->descriptor->size>=sizeof(CaineModV1) && mod->descriptor->gameUI) {
        try { return mod->descriptor->gameUI(menu,event,value)!=0; }
        catch (...) { logger("CAINE_MOD_GAME_UI_FAILED: "+id);return false; }
    }
    return false;
}
}
