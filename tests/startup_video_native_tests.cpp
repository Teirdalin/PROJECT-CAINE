#include <caine/startup_videos.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>

namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
unsigned storyCalls{};
int __cdecl Story(const char* filename,void* window,int flags) {
    Check(std::string(filename)=="vampire/media/story.bik" && window==reinterpret_cast<void*>(0x1234) && flags==17,"patched playback argument ABI");
    ++storyCalls;return 73;
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2 || argc==3,"usage: startup_video_native_tests engine.dll [widescreen_fix.vtm]");
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
        caine::Module patch;
        if (argc==3) {
            const auto patched=LoadLibraryExW(argv[2],nullptr,DONT_RESOLVE_DLL_REFERENCES);Check(patched!=nullptr,"map installed widescreen module");patch=caine::Module::Inspect(patched);
            Check(patch.sha256=="9cda62a89417d01f811141ba7e865fb404a7a45bd8ce605c7197467b1e976573","widescreen profile");
            *reinterpret_cast<int*>(patch.base+0x1e3e8)=0;
            Check(VirtualProtect(patch.base+0x5898,5,PAGE_EXECUTE_READWRITE,&access)!=0,"fixture story continuation");
            patch.base[0x5898]=0xe9;const auto delta=static_cast<int32_t>(reinterpret_cast<uint8_t*>(Story)-(patch.base+0x589d));memcpy(patch.base+0x5899,&delta,4);
            VirtualProtect(patch.base+0x5898,5,access,&access);FlushInstructionCache(GetCurrentProcess(),patch.base+0x5898,5);
        }
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
        if (patch.base) {
            using Play=int(__cdecl*)(const char*,void*,int);const auto play=reinterpret_cast<Play>(patch.base+0x5880);
            for (const char* name:{"media/activision.bik","Vampire/media/whitewolf.bik","vampire/media/NvidiaBloodLogo.bik","vampire/media/troika.bik"}) Check(play(name,nullptr,0)==0,"patched startup route did not skip known logo");
            Check(skipped==104 && !storyCalls,"startup route called story continuation");
            Check(play("vampire/media/story.bik",reinterpret_cast<void*>(0x1234),17)==73 && storyCalls==1,"story playback return value/forwarding");
        }
        for(const char* name:{"vampire/media/story.bik","troika.bik","mods/example/media/troika.bik","vampire/media/troika.bik.extra"})
            Check(!caine::IsStartupSplash(name),"non-startup video would be skipped");
        Check(!caine::IsStartupSplash(nullptr),"null filename recognized");
        // Keep the mapped image and process-lifetime hook until process exit.
        std::cout<<"CAINE_STARTUP_VIDEO_NATIVE_OK: actual startup caller, four-logo bypass, stack ABI and patched playback coexistence; live boot acceptance pending\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
