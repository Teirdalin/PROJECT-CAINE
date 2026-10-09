#include <caine/startup_videos.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2,"usage: startup_video_native_tests engine.dll");
        const auto image=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        Check(image!=nullptr,"map installed engine without import startup");
        const auto module=caine::Module::Inspect(image);
        Check(module.sha256=="9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5","unsupported engine image");
        Check(module.base[0xfbf4a]==0xc3,"native video helper caller-cleanup return changed");
        // Simulate another patch owning playback. CAINE must bypass the four
        // startup calls without touching this entry or reaching it.
        DWORD access{};Check(VirtualProtect(module.base+0xfbd10,16,PAGE_EXECUTE_READWRITE,&access)!=0,"fixture playback protection");
        const std::array<uint8_t,8> otherPatch{0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc,0xcc};
        memcpy(module.base+0xfbd10,otherPatch.data(),otherPatch.size());VirtualProtect(module.base+0xfbd10,16,access,&access);
        unsigned skipped{};
        Check(caine::InstallStartupVideoSkip(image,[&](const auto& message){if(message.find("CAINE_STARTUP_SPLASH_SKIPPED")==0)++skipped;}),"production startup hook installation");
        Check(memcmp(module.base+0xfbd10,otherPatch.data(),otherPatch.size())==0,"playback patch was overwritten");
        // Isolate the native startup continuation from unrelated engine init.
        Check(VirtualProtect(module.base+0xfb55b,1,PAGE_EXECUTE_READWRITE,&access)!=0,"fixture continuation protection");
        module.base[0xfb55b]=0xc3;VirtualProtect(module.base+0xfb55b,1,access,&access);FlushInstructionCache(GetCurrentProcess(),module.base+0xfb55b,1);
        using Startup=void(__cdecl*)();const auto startup=reinterpret_cast<Startup>(module.base+0xfb524);
        for (unsigned i=0;i<100;++i) startup();
        for(const char* name:{"vampire\\media\\activision.bik","vampire\\media\\whitewolf.bik","vampire\\media\\NvidiaBloodLogo.bik","vampire\\media\\troika.bik"}) {
            Check(caine::IsStartupSplash(name),"known startup logo not recognized");
        }
        Check(skipped==100,"guarded native block did not skip all logo clips or preserve stack ABI");
        for(const char* name:{"vampire/media/story.bik","troika.bik","mods/example/media/troika.bik","vampire/media/troika.bik.extra"})
            Check(!caine::IsStartupSplash(name),"non-startup video would be skipped");
        Check(!caine::IsStartupSplash(nullptr),"null filename recognized");
        // Keep the mapped image and process-lifetime hook until process exit.
        std::cout<<"CAINE_STARTUP_VIDEO_NATIVE_OK: actual startup caller, four-logo bypass, stack ABI and patched playback coexistence; live boot acceptance pending\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
