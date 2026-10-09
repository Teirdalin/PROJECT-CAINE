#include <caine/startup_videos.hpp>
#include <caine/preferences.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace caine {
namespace {
constexpr char EngineHash[]="9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5";
void* original{};
void* continuation{};
Hooks* hooks{};
Hooks* patchedPlaybackHooks{};
bool patchedPlaybackAttempted{};
bool engineAttempted{};
using Playback=int(__cdecl*)(const char*,void*,int);
Playback patchedPlaybackOriginal{};
std::function<void(const std::string&)> logger;
void __cdecl Skipped() noexcept {
    try { logger("CAINE_STARTUP_SPLASH_SKIPPED: four logo videos bypassed at the native startup caller"); } catch (...) {}
}
int __cdecl PatchedPlayback(const char* filename,void* window,int flags) {
    try {
        if (IsStartupSplash(filename)) {
            try { logger("CAINE_STARTUP_SPLASH_SKIPPED: recognized logo on profiled widescreen playback route"); } catch (...) {}
            return 0;
        }
    } catch (...) {} // Recognition failures retain the existing playback path.
    return patchedPlaybackOriginal(filename,window,flags);
}
void InstallPatchedPlayback(const std::function<void(const std::string&)>& log) {
    const auto handle=GetModuleHandleW(L"widescreen_fix.vtm");
    if (!handle || patchedPlaybackAttempted) return;
    patchedPlaybackAttempted=true;
    const auto module=Module::Inspect(handle);
    constexpr char hash[]="9cda62a89417d01f811141ba7e865fb404a7a45bd8ce605c7197467b1e976573";
    if (module.sha256!=hash) { log("CAINE_STARTUP_PLAYBACK_UNAVAILABLE: unknown widescreen profile; playback retained");return; }
    std::vector<uint8_t> bytes{0x83,0x3d,0,0,0,0,0,0x74,0x0f,0xc7,0x05};
    const auto variable=reinterpret_cast<uint32_t>(module.base+0x1e3e8);memcpy(bytes.data()+2,&variable,4);
    auto owner=new Hooks();std::string error;
    if (!owner->Install(module,{"startup.widescreen_logos",hash,0x5880,bytes},reinterpret_cast<void*>(PatchedPlayback),reinterpret_cast<void**>(&patchedPlaybackOriginal),error)) {
        if (!owner->Count()) delete owner;
        log("CAINE_STARTUP_PLAYBACK_UNAVAILABLE: "+error);return;
    }
    patchedPlaybackHooks=owner;
    log("CAINE_STARTUP_PLAYBACK_READY: exact widescreen profile, four recognized logos only; story playback delegates to existing patch");
}
// Skip before the first argument push, landing after all four calls and the
// caller's add esp,0x30. Playback's entry can remain owned by another patch.
__declspec(naked) void SkipLogos() {
    __asm {
        pushfd
        pushad
        call Skipped
        popad
        popfd
        jmp dword ptr [continuation]
    }
}
}
bool IsStartupSplash(const char* filename) {
    if (!filename) return false;
    const auto length=strnlen_s(filename,260);
    if (!length || length==260) return false;
    std::string path(filename,length);
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return c=='\\'?'/':static_cast<char>(std::tolower(c));});
    const auto folder=path.rfind("media/");
    if (folder==std::string::npos || (folder && path[folder-1]!='/') || path.find("..")!=std::string::npos) return false;
    const auto prefix=path.substr(0,folder);
    if (!prefix.empty() && prefix!="vampire/") {
        const auto active=ActiveGameFolder(ModulePath(nullptr).parent_path());
        auto relative=active.filename().u8string()+"/",absolute=active.u8string()+"/";
        for (auto* text:{&relative,&absolute}) std::transform(text->begin(),text->end(),text->begin(),[](unsigned char c){return c=='\\'?'/':static_cast<char>(std::tolower(c));});
        if (prefix!=relative && prefix!=absolute) return false;
    }
    const auto name=path.substr(folder);
    return name=="media/activision.bik" || name=="media/whitewolf.bik" ||
           name=="media/nvidiabloodlogo.bik" || name=="media/troika.bik";
}
bool InstallStartupVideoSkip(HMODULE engine,const std::function<void(const std::string&)>& log) {
    try {
        // The worker may revisit this while playback is executing on the game
        // thread. Publish the logger once before enabling any hook, then leave it
        // immutable instead of racing a std::function assignment against reads.
        if (!logger) logger=log;
        InstallPatchedPlayback(log);
        if (hooks) return true;
        if (engineAttempted) return patchedPlaybackHooks!=nullptr;
        engineAttempted=true;
        const auto module=Module::Inspect(engine);
        if (module.sha256!=EngineHash) { log("CAINE_STARTUP_SPLASH_UNAVAILABLE: unsupported engine profile");return false; }
        std::vector<uint8_t> expected;
        const uint32_t names[]={0x1ad174,0x1ad158,0x1ad134,0x1ad118};
        for (size_t i=0;i<4;++i) {
            expected.insert(expected.end(),{0x6a,static_cast<uint8_t>(i<2?1:0),0x56,0x68});
            const auto name=reinterpret_cast<uint32_t>(module.base+names[i]);
            const auto bytes=reinterpret_cast<const uint8_t*>(&name);expected.insert(expected.end(),bytes,bytes+4);
            expected.push_back(0xe8);
            const int32_t displacement=static_cast<int32_t>(0xfbd10-(0xfb524+i*13+13));
            const auto relative=reinterpret_cast<const uint8_t*>(&displacement);expected.insert(expected.end(),relative,relative+4);
            if (!IsStartupSplash(reinterpret_cast<const char*>(module.base+names[i]))) {
                log("CAINE_STARTUP_SPLASH_UNAVAILABLE: native startup filename profile mismatch");return false;
            }
        }
        expected.insert(expected.end(),{0x83,0xc4,0x30});
        auto owner=new Hooks();std::string error;
        continuation=module.base+0xfb55b;
        if (!owner->Install(module,{"startup.logo_block",EngineHash,0xfb524,expected},
                            reinterpret_cast<void*>(SkipLogos),&original,error)) {
            if (!owner->Count()) delete owner;
            log("CAINE_STARTUP_SPLASH_UNAVAILABLE: "+error);return false;
        }
        hooks=owner; // Process lifetime; never remove live detours under loader lock.
        log("CAINE_STARTUP_SPLASH_READY: guarded four-logo caller bypass; playback patches retained");
        return true;
    } catch (const std::exception& error) { log(std::string("CAINE_STARTUP_SPLASH_UNAVAILABLE: ")+error.what());return false; }
}
}
