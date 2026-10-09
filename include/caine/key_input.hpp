#pragma once
#include <windows.h>
#include <cstdint>
#include <string>

namespace caine {
// Names match the supported engine's native key table. The game backend also
// validates captured names against that live table before issuing bind commands.
inline std::string BindingKey(UINT message, WPARAM value, LPARAM data) {
    switch (message) {
    case WM_LBUTTONDOWN: case WM_LBUTTONDBLCLK: return "MOUSE1";
    case WM_RBUTTONDOWN: case WM_RBUTTONDBLCLK: return "MOUSE2";
    case WM_MBUTTONDOWN: case WM_MBUTTONDBLCLK: return "MOUSE3";
    case WM_XBUTTONDOWN: case WM_XBUTTONDBLCLK: return GET_XBUTTON_WPARAM(value)==XBUTTON1?"MOUSE4":"MOUSE5";
    case WM_MOUSEWHEEL: return GET_WHEEL_DELTA_WPARAM(value)>0?"MWHEELUP":GET_WHEEL_DELTA_WPARAM(value)<0?"MWHEELDOWN":"";
    default: break;
    }
    if (message!=WM_KEYDOWN && message!=WM_SYSKEYDOWN) return {};
    if ((static_cast<uintptr_t>(data)&(1u<<30)) || value==VK_ESCAPE) return {};
    // Keep operating-system Alt-Tab / Alt-F4 shortcuts outside capture.
    if (message==WM_SYSKEYDOWN && value!=VK_MENU && value!=VK_LMENU && value!=VK_RMENU) return {};
    if ((value>='A' && value<='Z') || (value>='0' && value<='9')) return std::string(1,static_cast<char>(value));
    if (value>=VK_F1 && value<=VK_F12) return "F"+std::to_string(value-VK_F1+1);
    if (value>=VK_NUMPAD0 && value<=VK_NUMPAD9) {
        const char* names[]{"KP_INS","KP_END","KP_DOWNARROW","KP_PGDN","KP_LEFTARROW","KP_5","KP_RIGHTARROW","KP_HOME","KP_UPARROW","KP_PGUP"};
        return names[value-VK_NUMPAD0];
    }
    const bool extended=(static_cast<uintptr_t>(data)&(1u<<24))!=0;
    switch(value) {
    case VK_RETURN: return extended?"KP_ENTER":"ENTER";
    case VK_SPACE: return "SPACE"; case VK_TAB: return "TAB"; case VK_BACK: return "BACKSPACE";
    case VK_SHIFT: case VK_LSHIFT: case VK_RSHIFT: return "SHIFT";
    case VK_CONTROL: case VK_LCONTROL: case VK_RCONTROL: return "CTRL";
    case VK_MENU: case VK_LMENU: case VK_RMENU: return "ALT";
    case VK_UP: return data && !extended?"KP_UPARROW":"UPARROW";
    case VK_DOWN: return data && !extended?"KP_DOWNARROW":"DOWNARROW";
    case VK_LEFT: return data && !extended?"KP_LEFTARROW":"LEFTARROW";
    case VK_RIGHT: return data && !extended?"KP_RIGHTARROW":"RIGHTARROW";
    case VK_INSERT: return data && !extended?"KP_INS":"INS";
    case VK_DELETE: return data && !extended?"KP_DEL":"DEL";
    case VK_HOME: return data && !extended?"KP_HOME":"HOME";
    case VK_END: return data && !extended?"KP_END":"END";
    case VK_PRIOR: return data && !extended?"KP_PGUP":"PGUP";
    case VK_NEXT: return data && !extended?"KP_PGDN":"PGDN";
    case VK_CLEAR: return "KP_5";
    case VK_ADD: return "KP_PLUS"; case VK_SUBTRACT: return "KP_MINUS";
    case VK_DIVIDE: return "KP_SLASH"; case VK_MULTIPLY: return "*"; case VK_DECIMAL: return "KP_DEL";
    case VK_CAPITAL: return "CAPSLOCK"; case VK_PAUSE: return "PAUSE";
    case VK_OEM_1: return "SEMICOLON"; case VK_OEM_PLUS: return "="; case VK_OEM_MINUS: return "-";
    case VK_OEM_COMMA: return ","; case VK_OEM_PERIOD: return "."; case VK_OEM_2: return "/";
    case VK_OEM_3: return "`"; case VK_OEM_4: return "["; case VK_OEM_5: return "\\";
    case VK_OEM_6: return "]"; case VK_OEM_7: return "'";
    default: return {};
    }
}
}
