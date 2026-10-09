#include <caine/overlay_input.hpp>
#include <array>
#include <iostream>
#include <stdexcept>
namespace caine { void OverlayInputTestState(HWND foreground,ULONGLONG now); }
namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
unsigned recentered{};
void __fastcall Reset(void*,void*) { ++recentered; }
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==2,"usage: overlay_input_native_tests client.dll");
        const auto image=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        Check(image!=nullptr,"map installed client");const auto module=caine::Module::Inspect(image);
        auto input=module.base+0x2ea6c8;
        *reinterpret_cast<uint8_t**>(input)=module.base+0x224d4c; // fixture skips native DLL constructors
        Check(caine::InstallOverlayInput(module,[](const auto&){}),"install exact production CInput hooks");
        std::array<void*,26> vtable{};vtable[25]=reinterpret_cast<void*>(Reset);
        *reinterpret_cast<void***>(input)=vtable.data();
        auto word=[&](size_t offset)->int& { return *reinterpret_cast<int*>(input+offset); };
        using Fn=void(__thiscall*)(void*);using Move=void(__thiscall*)(void*,void*);using Buttons=void(__thiscall*)(void*,int,int);
        const auto activate=reinterpret_cast<Fn>(module.base+0x106240),deactivate=reinterpret_cast<Fn>(module.base+0x106290);
        const auto window=reinterpret_cast<HWND>(1);caine::OverlayInputTestState(window,1000);
        word(0x20)=1;word(0x28)=1;word(0x54)=13;word(0x58)=27;
        caine::CaptureOverlayInput(window,true);
        Check(word(0x28)==0 && word(0x54)==0 && word(0x58)==0,"native deactivation must release accumulated deltas");
        activate(input);Check(word(0x28)==0,"game reactivation must be blocked during capture");
        // The uninitialized imported interfaces would fault if these original
        // native polling/camera/recenter routines ran during an overlay.
        for (unsigned i=0;i<100;++i) {
            reinterpret_cast<Fn>(module.base+0x1067d0)(input);
            reinterpret_cast<Fn>(module.base+0x106310)(input);
            reinterpret_cast<Move>(module.base+0x1068a0)(input,nullptr);
            reinterpret_cast<Buttons>(module.base+0x106340)(input,3,1);
        }
        Check(recentered==0,"captured mouse was recentered");
        caine::CaptureOverlayInput(window,false);
        Check(word(0x28)==1 && recentered==1,"release restores prior active mouse through original native routine");
        caine::CaptureOverlayInput(window,true);caine::OverlayInputTestState(nullptr,1100);
        caine::CaptureOverlayInput(window,false);Check(word(0x28)==0,"focus loss must not activate background mouse");
        caine::OverlayInputTestState(window,1200);activate(input);Check(word(0x28)==1,"focus regain restores gameplay");
        caine::CaptureOverlayInput(window,true);deactivate(input);caine::CaptureOverlayInput(window,false);
        Check(word(0x28)==0,"native pause/deactivation during capture must remain authoritative");
        activate(input);caine::CaptureOverlayInput(window,true);caine::OverlayInputTestState(window,1800);activate(input);
        Check(word(0x28)==1,"expired renderer lease must restore mouse");
        word(0x28)=0;caine::CaptureOverlayInput(window,true);caine::CaptureOverlayInput(window,false);
        Check(word(0x28)==0,"capture must preserve previously inactive native mouse");
        std::cout<<"CAINE_OVERLAY_INPUT_NATIVE_OK: actual CInput detours, activation, deltas, camera, buttons, recenter, focus, pause, restoration and stale-frame release; live acceptance pending\n";return 0;
    } catch(const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
