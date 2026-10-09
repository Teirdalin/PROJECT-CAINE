#include <caine/intro_skip.hpp>
#include <caine/preferences.hpp>
#include <caine/core.hpp>
#include <caine/menu_view.hpp>
#include <caine/logging.hpp>
#include <shellapi.h>
#include <mutex>
#include <memory>
#include <cstring>
#include <algorithm>
#include <atomic>

namespace caine {
namespace {
std::recursive_mutex mutex;
std::function<void(const std::string&)> logger;
HMODULE engineModule{};
uint8_t* engineBase{};
void* engineClient{};
bool ready{}, checked{}, lastVisible{}, capturedEscape{}, announced{}, wasOpening{}, menuVisible{};
HWND gameWindow{};
uint8_t* nativeSkipFlag{};
void(__cdecl* nativeSkipOriginal)(){};
std::atomic<bool> queuedSkip{};
bool flagOwned{};
uint64_t lastFrame{};
IntroHold hold;
std::unique_ptr<MenuRenderer> renderer;
template<class T> T Method(size_t slot) { return reinterpret_cast<T>((*static_cast<void***>(engineClient))[slot]); }
bool Connect() {
    if (checked) return engineClient!=nullptr;
    checked=true;
    using Factory=void*(__cdecl*)(const char*,int*);
    auto factory=reinterpret_cast<Factory>(GetProcAddress(engineModule,"CreateInterface"));
    if (!factory) return false;
    auto object=factory("VEngineClient006",nullptr);if (!object) return false;
    const auto table=*static_cast<uint8_t***>(object);
    // Inspected 2004 CEngineClient: console visibility, queued commands,
    // active client+server state, pause state and the current level name.
    if (table[6]!=engineBase+0x1a1b0 || table[28]!=engineBase+0x1a570 ||
        table[57]!=engineBase+0x1a9e0 || table[59]!=engineBase+0x1aa20 ||
        table[106]!=engineBase+0x1b800) {
        logger("CAINE_INTRO_SKIP_UNAVAILABLE: engine interface address mismatch");return false;
    }
    engineClient=object;
    logger("CAINE_INTRO_SKIP_READY: opening cinematic only; configured hold duration; native story/landmark transition queued");
    return true;
}
struct Status { bool opening{}, eligible{}; };
Status Scene(HWND window) {
    if (!engineClient) return {};
    using Test=bool(__thiscall*)(void*);
    using Name=const char*(__thiscall*)(void*);
    const bool active=Method<Test>(57)(engineClient),paused=active && Method<Test>(59)(engineClient),console=active && Method<Test>(6)(engineClient);
    const auto name=active?Method<Name>(106)(engineClient):nullptr;
    const auto length=name?strnlen_s(name,261):0;
    const auto level=length && length<=260?std::string(name,length):std::string{};
    static std::string previous;static bool observed{},lastActive{},lastPaused{},lastConsole{};
    if (!observed || level!=previous || active!=lastActive || paused!=lastPaused || console!=lastConsole) {
        TraceLog("CAINE_ENGINE_STATE: active="+std::to_string(active)+" paused="+std::to_string(paused)+" console="+std::to_string(console)+" level="+level);
        observed=true;previous=level;lastActive=active;lastPaused=paused;lastConsole=console;
    }
    if (!active) return {};
    const bool opening=!level.empty() && IsOpeningLevel(level);
    const bool eligible=opening && window && GetForegroundWindow()==window &&
        !menuVisible && !paused && !console;
    return {opening,eligible};
}
void __cdecl NativeSkipCommand() noexcept {
    // Native character-creation/console skip requests retain their own behavior.
    if (!queuedSkip.exchange(false)) { nativeSkipOriginal();return; }
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (!ready || !gameWindow || GetWindowThreadProcessId(gameWindow,nullptr)!=GetCurrentThreadId() || !Scene(gameWindow).opening) {
            logger("CAINE_INTRO_SKIP_CANCELLED: queued request reached a different/loading level");return;
        }
        flagOwned=*nativeSkipFlag==0;
        nativeSkipOriginal();
        logger("CAINE_INTRO_SKIP_COMMAND_ACCEPTED: native skip flag set on command thread");
    } catch (...) { OutputDebugStringA("CAINE intro skip command guard failed\n"); }
}
}
void InitializeIntroSkip(const std::filesystem::path& config,const std::function<void(const std::string&)>& log) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    logger=log;
    if (!GetPrivateProfileIntW(L"Intro",L"Enabled",1,config.c_str())) return;
    const auto game=Module::Inspect(GetModuleHandleW(L"vampire.dll"));
    const auto engine=Module::Inspect(GetModuleHandleW(L"engine.dll"));
    const auto client=Module::Inspect(GetModuleHandleW(L"client.dll"));
    if (game.sha256!="996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8" ||
        engine.sha256!="9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5" ||
        client.sha256!="9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01") {
        log("CAINE_INTRO_SKIP_UNAVAILABLE: native game/engine/client profile mismatch");return;
    }
    const auto root=ModulePath(nullptr).parent_path(),active=ActiveGameFolder(root);
    const auto loose=active/L"maps"/L"sp_theatre.bsp";
    const auto map=std::filesystem::exists(loose)?loose:root/L"Vampire"/L"maps"/L"sp_theatre.bsp";
    if (!IntroMapSupported(map)) { log("CAINE_INTRO_SKIP_UNAVAILABLE: opening map entity/transition contract absent");return; }
    const auto duration=std::clamp(GetPrivateProfileIntW(L"Intro",L"HoldMilliseconds",1500,config.c_str()),500u,5000u);
    nativeSkipFlag=game.base+0x6e7e91;
    // mov byte ptr [native flag],1; ret. Account for the loader's relocation.
    std::vector<uint8_t> expected{0xc6,0x05,0,0,0,0,0x01,0xc3};
    const auto address=reinterpret_cast<uint32_t>(nativeSkipFlag);
    std::memcpy(expected.data()+2,&address,sizeof(address));
    auto hooks=new Hooks();std::string error;
    if (!hooks->Install(game,{"intro.skip_command",game.sha256,0xdb3f0,expected},
        reinterpret_cast<void*>(NativeSkipCommand),reinterpret_cast<void**>(&nativeSkipOriginal),error)) {
        log("CAINE_INTRO_SKIP_UNAVAILABLE: "+error);return;
    }
    hold=IntroHold(duration);engineModule=engine.handle;engineBase=engine.base;ready=true;
    log("CAINE_INTRO_SKIP_PROFILE: verified native modules and opening map; hold="+std::to_string(duration)+"ms");
}
void PaintIntroSkip(IDirect3DDevice9* device,HWND window,bool nativeMenuVisible) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    lastVisible=false;
    if (!ready || !window || GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId() || !Connect()) return;
    gameWindow=window;
    menuVisible=nativeMenuVisible;
    const auto now=GetTickCount64();lastFrame=now;
    const auto state=Scene(window);const bool down=(GetAsyncKeyState(VK_ESCAPE)&0x8000)!=0;
    if (flagOwned) {
        if (!state.opening && *nativeSkipFlag) {
            *nativeSkipFlag=0;
            logger("CAINE_INTRO_SKIP_FLAG_CLEARED: left opening before native Start consumed the request");
        }
        if (!*nativeSkipFlag) flagOwned=false;
    }
    if (!down) capturedEscape=false;
    if (state.opening && !wasOpening) logger("CAINE_INTRO_ENTERED: sp_theatre");
    if (!state.opening && wasOpening) { logger("CAINE_INTRO_LEFT: skip prompt/input released");announced=false; }
    wasOpening=state.opening;
    if (!state.eligible || !device) { hold.Update(state.opening,false,down,now);return; }
    if (!renderer) renderer=std::make_unique<MenuRenderer>();
    if (!renderer->Prepare(device)) { hold.Update(state.opening,false,down,now);return; }
    const bool skip=hold.Update(state.opening,true,down,now);
    MenuView view;view.intro=true;view.skipProgress=hold.Progress();view.skipping=hold.Requested();
    std::vector<MenuAction> ignored;
    lastVisible=renderer->Render(window,view,ignored);
    if (lastVisible && !announced) { announced=true;logger("CAINE_INTRO_PROMPT_SHOWN"); }
    if (skip) {
        // ClientCmd appends to the engine command buffer. Never change levels or
        // call server/Python entity code inside Direct3D's EndScene boundary.
        using Command=void(__thiscall*)(void*,const char*);
        logger("CAINE_INTRO_SKIP_REQUESTED: vskip_intro + embrace_o_matic Start -> sp_tutorial_1 / tutorial");
        queuedSkip.store(true);
        Method<Command>(28)(engineClient,IntroSkipCommands);
    }
}
bool CaptureIntroEscape(HWND window,UINT message,WPARAM value) {
    if ((message!=WM_KEYDOWN && message!=WM_KEYUP && message!=WM_CHAR) || value!=VK_ESCAPE) return false;
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!ready || GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId()) return false;
    // Keep the tail of this key press out of gameplay after the level changes.
    if (capturedEscape) { if (message==WM_KEYUP) capturedEscape=false;return true; }
    if (!lastVisible || GetTickCount64()-lastFrame>=250 || !Scene(window).eligible) return false;
    if (message==WM_KEYDOWN) capturedEscape=true;
    return true;
}
}
