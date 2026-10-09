#include <caine/window_input.hpp>
#include <caine/logging.hpp>

namespace caine {
thread_local WindowInput* WindowInput::current_{};
WindowInput::~WindowInput() {
    if (hook_) UnhookWindowsHookEx(hook_);
    if (current_==this) current_=nullptr;
}
bool WindowInput::Attach(HWND window,Dispatch dispatch) {
    DWORD process{};
    if (!window || GetWindowThreadProcessId(window,&process)!=GetCurrentThreadId() ||
        process!=GetCurrentProcessId() || !dispatch || (current_ && current_!=this)) return false;
    if (!hook_) {
        hook_=SetWindowsHookExW(WH_GETMESSAGE,MessageHook,nullptr,GetCurrentThreadId());
        if (!hook_) return false;
    }
    window_=window;dispatch_=std::move(dispatch);current_=this;
    TraceLog("CAINE_INPUT_QUEUE_READY: renderer window, thread="+std::to_string(GetCurrentThreadId()));
    return true;
}
LRESULT CALLBACK WindowInput::MessageHook(int code,WPARAM removed,LPARAM message) {
    auto self=current_;
    if (code>=0 && removed==PM_REMOVE && self && message) {
        auto& event=*reinterpret_cast<MSG*>(message);
        if (event.hwnd==self->window_) {
            try {
                if (self->dispatch_(event.hwnd,event.message,event.wParam,event.lParam)) {
                    // Bloodlines can bypass TranslateMessage in its internal
                    // dispatch. Translate captured key-downs exactly once here
                    // so text retains the Windows keyboard layout and Unicode.
                    if (event.message==WM_KEYDOWN || event.message==WM_SYSKEYDOWN) TranslateMessage(&event);
                    self->mouse_+=event.message>=WM_MOUSEFIRST && event.message<=WM_MOUSELAST;
                    self->keys_+=event.message==WM_KEYDOWN || event.message==WM_KEYUP;
                    self->text_+=event.message==WM_CHAR;
                    const auto now=GetTickCount64();
                    if (!self->logged_ || now-self->logged_>=5000) {
                        self->logged_=now;
                        TraceLog("CAINE_INPUT_QUEUE_CAPTURED: mouse_events="+std::to_string(self->mouse_)+
                            " key_events="+std::to_string(self->keys_)+" text_events="+std::to_string(self->text_));
                        self->mouse_=self->keys_=self->text_=0;
                    }
                    // Keep HWND/time/point intact for other thread hooks. The
                    // native loop may dispatch WM_NULL, which carries no input.
                    event.message=WM_NULL;event.wParam=0;event.lParam=0;
                }
            } catch (...) { WriteLog("CAINE_INPUT_QUEUE_FAILED: native dispatch retained"); }
        }
    }
    return CallNextHookEx(self?self->hook_:nullptr,code,removed,message);
}
}
