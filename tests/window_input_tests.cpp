#include <caine/window_input.hpp>
#include <iostream>
#include <stdexcept>

namespace {
unsigned nativeInput{},nativeFocus{};
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
LRESULT CALLBACK Native(HWND window,UINT message,WPARAM value,LPARAM data) {
    if ((message>=WM_MOUSEFIRST && message<=WM_MOUSELAST) || (message>=WM_KEYFIRST && message<=WM_KEYLAST)) ++nativeInput;
    if (message==WM_ACTIVATEAPP) ++nativeFocus;
    return DefWindowProcW(window,message,value,data);
}
void Drain() {
    MSG message{};
    while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message);DispatchMessageW(&message); }
}
}
int main() {
    try {
        const auto window=CreateWindowExW(0,L"STATIC",L"CAINE input queue fixture",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        const auto other=CreateWindowExW(0,L"STATIC",L"Other fixture",WS_OVERLAPPEDWINDOW,0,0,640,480,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(window && other,"hidden owned windows");
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Native));
        SetWindowLongPtrW(other,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Native));
        Drain();nativeInput=nativeFocus=0;
        unsigned captured{},unicode{},keys{};bool modal=true;
        caine::WindowInput input;
        Check(!input.Attach(nullptr,{}),"invalid window attached");
        Check(input.Attach(window,[&](HWND,UINT message,WPARAM value,LPARAM)->std::optional<LRESULT> {
            if (!modal || !((message>=WM_MOUSEFIRST && message<=WM_MOUSELAST) || message==WM_CHAR || message==WM_KEYDOWN || message==WM_KEYUP)) return {};
            ++captured;unicode+=message==WM_CHAR && value==0x00e9;keys+=message==WM_KEYDOWN;return 0;
        }),"actual thread message hook");
        Check(PostMessageW(window,WM_LBUTTONDOWN,MK_LBUTTON,MAKELPARAM(100,100))!=FALSE,"queue click");
        MSG peek{};Check(PeekMessageW(&peek,window,WM_LBUTTONDOWN,WM_LBUTTONDOWN,PM_NOREMOVE)!=FALSE,"peek click");
        Check(peek.message==WM_LBUTTONDOWN && captured==0,"non-removing peek consumed input");
        Drain();Check(captured==1 && nativeInput==0,"native handler received captured click");
        // A native panel can replace the WNDPROC after CAINE starts. Capture
        // must still precede that handler, without repeatedly subclassing it.
        SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Native));
        PostMessageW(window,WM_CHAR,0x00e9,0);PostMessageW(window,WM_KEYDOWN,'F',1);PostMessageW(window,WM_KEYUP,'F',1);
        Drain();Check(unicode==1 && keys==1 && captured>=4 && nativeInput==0,"Unicode/key queue lost to native handler");
        const auto before=captured;
        PostMessageW(window,WM_SYSKEYUP,VK_F4,0);PostMessageW(window,WM_ACTIVATEAPP,FALSE,0);
        PostMessageW(other,WM_CHAR,'Z',0);Drain();
        Check(captured==before && nativeFocus==1 && nativeInput==2,"system/focus/other-window native dispatch changed");
        modal=false;PostMessageW(window,WM_CHAR,'Z',0);Drain();
        Check(captured==before && nativeInput==3,"non-modal gameplay input was captured");
        DestroyWindow(other);DestroyWindow(window);
        std::cout<<"CAINE_WINDOW_INPUT_OK: removed messages, changed native WNDPROC, clicks, Unicode, keys, system shortcuts, focus, other windows and non-modal passthrough\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
