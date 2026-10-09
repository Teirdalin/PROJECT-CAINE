#pragma once
#include <windows.h>
#include <functional>
#include <optional>

namespace caine {
// Capture removed messages before native queued/VGUI input dispatch. Directly
// sent messages still use the owner's window procedure and the same policy.
class WindowInput {
public:
    using Dispatch = std::function<std::optional<LRESULT>(HWND,UINT,WPARAM,LPARAM)>;
    WindowInput() = default;
    ~WindowInput();
    WindowInput(const WindowInput&) = delete;
    WindowInput& operator=(const WindowInput&) = delete;
    bool Attach(HWND window, Dispatch dispatch);
private:
    static LRESULT CALLBACK MessageHook(int code, WPARAM removed, LPARAM message);
    HWND window_{};
    HHOOK hook_{};
    Dispatch dispatch_;
    ULONGLONG logged_{};
    unsigned mouse_{}, keys_{}, text_{};
    static thread_local WindowInput* current_;
};
}
