#include <caine/startup_videos.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace caine {
namespace {
constexpr char EngineHash[]="9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5";
// Installed native caller at fb526..fb553 pushes fullscreen, HWND, filename;
// the helper ends with plain ret at fbf4a (caller cleans all 12 stack bytes).
using PlayVideo=void(__cdecl*)(const char*,HWND,int);
PlayVideo original{};
Hooks* hooks{};
std::function<void(const std::string&)> logger;
void __cdecl Play(const char* filename,HWND window,int fullscreen) {
    if (IsStartupSplash(filename)) {
        try { logger(std::string("CAINE_STARTUP_SPLASH_SKIPPED: ")+filename); } catch (...) {}
        return;
    }
    original(filename,window,fullscreen);
}
}
bool IsStartupSplash(const char* filename) {
    if (!filename) return false;
    const auto length=strnlen_s(filename,260);
    if (!length || length==260) return false;
    std::string path(filename,length);
    std::transform(path.begin(),path.end(),path.begin(),[](unsigned char c){return c=='\\'?'/':static_cast<char>(std::tolower(c));});
    return path=="vampire/media/activision.bik" || path=="vampire/media/whitewolf.bik" ||
           path=="vampire/media/nvidiabloodlogo.bik" || path=="vampire/media/troika.bik";
}
bool InstallStartupVideoSkip(HMODULE engine,const std::function<void(const std::string&)>& log) {
    if (hooks) return original!=nullptr;
    try {
        const auto module=Module::Inspect(engine);
        if (module.sha256!=EngineHash) { log("CAINE_STARTUP_SPLASH_UNAVAILABLE: unsupported engine profile");return false; }
        std::vector<uint8_t> expected{0x83,0xec,0x34,0x56,0x57,0x8b,0x3d,0,0,0,0};
        const auto operand=reinterpret_cast<uint32_t>(module.base+0x173208);memcpy(expected.data()+7,&operand,4);
        auto owner=new Hooks();std::string error;
        logger=log;
        if (!owner->Install(module,{"startup.logo_videos",EngineHash,0xfbd10,expected},
                            reinterpret_cast<void*>(Play),reinterpret_cast<void**>(&original),error)) {
            delete owner;log("CAINE_STARTUP_SPLASH_UNAVAILABLE: "+error);return false;
        }
        hooks=owner; // Process lifetime; never remove live detours under loader lock.
        log("CAINE_STARTUP_SPLASH_READY: guarded native playback hook; four startup logos only");
        return true;
    } catch (const std::exception& error) { log(std::string("CAINE_STARTUP_SPLASH_UNAVAILABLE: ")+error.what());return false; }
}
}
