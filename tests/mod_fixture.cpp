#include <caine/mod.hpp>
#include <stdexcept>
namespace {
using Add = int(__cdecl*)(int);
Add original{};
bool failTick{}, failGameUI{}, failPoll{};
int __cdecl Detour(int value) { return original(value) + 100; }
int __cdecl Start(const CaineHostV1* host) {
    caine::sdk::Initialize(host);
    const char input[]=R"({"type":"caine.scalar","version":1,"future":true,"value":{"kind":"int32","value":7}})";
    const auto needed=host->serializeOwned(host->context,input,sizeof(input)-1,nullptr,0);
    char small='z';if (!needed || host->serializeOwned(host->context,input,sizeof(input)-1,&small,1)!=needed || small!='z') return 0;
    std::vector<char> encoded(needed);if (host->serializeOwned(host->context,input,sizeof(input)-1,encoded.data(),needed)!=needed || encoded.back()) return 0;
    CaineTypeV1 type{};type.size=sizeof(type);unsigned count{};while(host->typeInfo(host->context,count,&type))++count;
    if (count!=9) return 0;
    const auto module = caine::sdk::Module::Inspect(GetModuleHandleW(L"engine.dll"));
    const auto target = reinterpret_cast<uint8_t*>(GetProcAddress(module.handle, "FixtureAdd"));
    caine::sdk::HookSpec spec{"fixture.add",module.sha256,static_cast<size_t>(target-module.base),
                             std::vector<uint8_t>(target,target+8)};
    caine::sdk::Hooks hooks; std::string error;
    auto bad = spec; bad.moduleSha256 = std::string(64,'0');
    if (hooks.InstallBatch(module,{{bad,reinterpret_cast<void*>(Detour),reinterpret_cast<void**>(&original)}},error)) return 0;
    if (!hooks.InstallBatch(module,{{spec,reinterpret_cast<void*>(Detour),reinterpret_cast<void**>(&original)}},error)) return 0;
    if (hooks.InstallBatch(module,{{spec,reinterpret_cast<void*>(Detour),reinterpret_cast<void**>(&original)}},error)) return 0;
    caine::sdk::Log("FIXTURE_MOD_STARTED"); return 1;
}
void __cdecl Tick() {
    caine::sdk::Log("FIXTURE_MOD_TICKED");
    if (failTick) throw std::runtime_error("fixture tick failure");
}
int __cdecl GameUI(const CaineMenuV1*,uint32_t event,uint32_t) {
    caine::sdk::Log("FIXTURE_GAME_UI_CALLED");
    if (failGameUI && (event==CAINE_GAMEUI_POLL)==failPoll)
        throw std::runtime_error("fixture game UI failure");
    return 1;
}
int __cdecl Configure(const CaineMenuV1* menu,uint32_t event,uint32_t) {
    if (event==CAINE_MENU_BUILD) menu->addRow(menu->context,"Fixture settings",UINT32_MAX);
    return 1;
}
CaineModV1 desc{sizeof(CaineModV1),CAINE_MOD_ABI_V1,CAINE_FRAMEWORK_VERSION,"test-mod","Test Mod","1.0.0",Start,Tick,Configure,nullptr,nullptr,GameUI};
}
extern "C" const CaineModV1* __cdecl CaineMod_Query(uint32_t abi) {
    wchar_t bad[2]{};
    if (GetEnvironmentVariableW(L"CAINE_TEST_BAD_ABI",bad,2)) desc.abiVersion = 99;
    if (GetEnvironmentVariableW(L"CAINE_TEST_OLD_DESCRIPTOR",bad,2)) desc.size=offsetof(CaineModV1,gameUI);
    failTick = GetEnvironmentVariableW(L"CAINE_TEST_TICK_FAILURE",bad,2)!=0;
    failGameUI = GetEnvironmentVariableW(L"CAINE_TEST_GAME_UI_FAILURE",bad,2)!=0;
    failPoll = GetEnvironmentVariableW(L"CAINE_TEST_POLL_FAILURE",bad,2)!=0;
    return abi==CAINE_MOD_ABI_V1 ? &desc : nullptr;
}
