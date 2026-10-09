#include <caine/startup_videos.hpp>
#include <algorithm>
#include <cctype>
#include <cstring>

namespace caine {
namespace {
constexpr char EngineHash[]="9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5";
void* original{};
void* continuation{};
Hooks* hooks{};
std::function<void(const std::string&)> logger;
void __cdecl Skipped() noexcept {
    try { logger("CAINE_STARTUP_SPLASH_SKIPPED: four logo videos bypassed at the native startup caller"); } catch (...) {}
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
    return path=="vampire/media/activision.bik" || path=="vampire/media/whitewolf.bik" ||
           path=="vampire/media/nvidiabloodlogo.bik" || path=="vampire/media/troika.bik";
}
bool InstallStartupVideoSkip(HMODULE engine,const std::function<void(const std::string&)>& log) {
    if (hooks) return true;
    try {
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
        logger=log;continuation=module.base+0xfb55b;
        if (!owner->Install(module,{"startup.logo_block",EngineHash,0xfb524,expected},
                            reinterpret_cast<void*>(SkipLogos),&original,error)) {
            delete owner;log("CAINE_STARTUP_SPLASH_UNAVAILABLE: "+error);return false;
        }
        hooks=owner; // Process lifetime; never remove live detours under loader lock.
        log("CAINE_STARTUP_SPLASH_READY: guarded four-logo caller bypass; playback patches retained");
        return true;
    } catch (const std::exception& error) { log(std::string("CAINE_STARTUP_SPLASH_UNAVAILABLE: ")+error.what());return false; }
}
}
