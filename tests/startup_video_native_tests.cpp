#include <caine/startup_videos.hpp>
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
        unsigned skipped{};
        Check(caine::InstallStartupVideoSkip(image,[&](const auto& message){if(message.find("CAINE_STARTUP_SPLASH_SKIPPED")==0)++skipped;}),"production startup hook installation");
        using Play=void(__cdecl*)(const char*,HWND,int);
        const auto play=reinterpret_cast<Play>(module.base+0xfbd10);
        for(const char* name:{"vampire\\media\\activision.bik","vampire\\media\\whitewolf.bik","vampire\\media\\NvidiaBloodLogo.bik","vampire\\media\\troika.bik"}) {
            Check(caine::IsStartupSplash(name),"known startup logo not recognized");
            play(name,nullptr,0);play(name,nullptr,1);
        }
        Check(skipped==8,"guarded native calls did not skip all logo clips");
        for(const char* name:{"vampire/media/story.bik","troika.bik","mods/example/media/troika.bik","vampire/media/troika.bik.extra"})
            Check(!caine::IsStartupSplash(name),"non-startup video would be skipped");
        Check(!caine::IsStartupSplash(nullptr),"null filename recognized");
        // Keep the mapped image and process-lifetime hook until process exit.
        std::cout<<"CAINE_STARTUP_VIDEO_NATIVE_OK: actual engine entry, guarded production detour, all four logo clips, caller-cleanup ABI and narrow filename policy; live boot acceptance pending\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
