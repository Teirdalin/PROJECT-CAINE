#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
namespace caine {
// Verified partial fields of the supported client's main/pause menu.
// The caller must first validate the client profile and native object lifetime.
struct NativeMenuState {
    static bool ConfirmQuit(int action,const int* nativeItems,int count) {
        if (action==9 || action==13) return true; // Pause: quit to main menu / CAINE desktop exit.
        if (action!=10) return false;
        if (!nativeItems || count<0 || count>12) return true; // Unknown state retains the existing guard.
        for (int i=0;i<count;++i) {
            // Native Save, Quit to Main Menu and Resume exist only with an active game.
            if (nativeItems[i]==3 || nativeItems[i]==9 || nativeItems[i]==11) return true;
        }
        return false;
    }
    static int Pending(const void* object) { int value{};memcpy(&value,static_cast<const uint8_t*>(object)+0x2e8,4);return value; }
    static bool ChildBusy(bool nativeBusy,const void* object) { return nativeBusy && Pending(object)==-1; }
    static void CompleteTransition(void* object) {
        auto bytes=static_cast<uint8_t*>(object);float target{};memcpy(&target,bytes+0x2dc,4);
        if (std::isfinite(target) && target>=0 && target<=255) { memcpy(bytes+0x2d8,&target,4);memcpy(bytes+0x2e0,&target,4); }
    }
};
}
