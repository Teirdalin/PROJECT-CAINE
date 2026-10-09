// Executes the installed native skip flag/Start branch in an isolated process.
// Python and change-level helpers are intercepted: no game world is created.
#include <caine/core.hpp>
#include <iostream>
#include <cstring>
#include <stdexcept>

namespace {
void(__cdecl* originalSkip)(){};
bool allow=true;
int storyCalls{},levelCalls{},scriptFlags{};
std::string script,level,landmark;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void __cdecl SkipGuard() { if (allow) originalSkip(); }
int __cdecl CaptureStory(const char* source,int flags) { script=source;scriptFlags=flags;++storyCalls;return 0; }
void __cdecl CaptureLevel(const char* map,const char* mark) { level=map;landmark=mark;++levelCalls; }
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2,"usage: caine_intro_native_tests vampire.dll");
        const auto image=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        Check(image!=nullptr,"map installed game image without DllMain/import initialization");
        const auto module=caine::Module::Inspect(image);
        const std::string hash="996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8";
        Check(module.sha256==hash,"unsupported game profile");
        auto flag=module.base+0x6e7e91;
        std::string error;
        {
            caine::Hooks hooks;
            std::vector<uint8_t> skip{0xc6,0x05,0,0,0,0,1,0xc3};
            const auto operand=reinterpret_cast<uint32_t>(flag);memcpy(skip.data()+2,&operand,4);
            Check(hooks.Install(module,{"test.intro.command",hash,0xdb3f0,skip},reinterpret_cast<void*>(SkipGuard),reinterpret_cast<void**>(&originalSkip),error),error.c_str());
            void* storyOriginal{};void* levelOriginal{};
            Check(hooks.Install(module,{"test.intro.story",hash,0x1d2850,{0x56,0x8b,0x74,0x24,0x08,0x85,0xf6,0x57}},reinterpret_cast<void*>(CaptureStory),&storyOriginal,error),error.c_str());
            std::vector<uint8_t> change{0x8b,0x0d,0,0,0,0,0x53,0x56};
            const auto changeOperand=reinterpret_cast<uint32_t>(module.base+0x70ba0c);memcpy(change.data()+2,&changeOperand,4);
            Check(hooks.Install(module,{"test.intro.changelevel",hash,0x1c7b80,change},reinterpret_cast<void*>(CaptureLevel),&levelOriginal,error),error.c_str());
            const auto command=reinterpret_cast<void(__cdecl*)()>(module.base+0xdb3f0);
            const auto start=reinterpret_cast<void(__thiscall*)(void*,void*)>(module.base+0x81e00);
            *flag=0;allow=false;command();Check(*flag==0,"command detour could not reject a late request");
            allow=true;
            for (int i=0;i<2;++i) {
                command();Check(*flag==1,"native skip callback/trampoline did not set its relocated flag");
                // The skip branch must not need an entity or animation state.
                // A null self would fail immediately if normal playback ran.
                start(nullptr,nullptr);
                Check(*flag==0,"Start did not consume/clear the skip flag");
                Check(script=="__main__.G.Story_State=-3" && scriptFlags==0x100,"native starting story state changed");
                Check(level=="sp_tutorial_1" && landmark=="tutorial","native tutorial level/landmark changed");
                Check(storyCalls==i+1 && levelCalls==i+1,"native Start branch executed wrong number of transitions");
            }
            Check(hooks.RemoveAll(error),error.c_str());
            *flag=0;command();Check(*flag==1,"native skip callback was not restored");*flag=0;
        }
        FreeLibrary(image);
        std::cout<<"CAINE_INTRO_NATIVE_OK: actual relocated skip callback, exact-byte detour/trampoline, rejection, Start flag consumption, story state and tutorial arguments passed; transition/Python helpers stubbed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
