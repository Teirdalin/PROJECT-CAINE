#pragma once
#include <cmath>
#include <cstdint>
#include <cstring>
namespace caine {
// Verified partial fields of the supported client's main/pause menu.
// The caller must first validate the client profile and native object lifetime.
struct NativeMenuState {
    static int Pending(const void* object) { int value{};memcpy(&value,static_cast<const uint8_t*>(object)+0x2e8,4);return value; }
    static bool ChildBusy(bool nativeBusy,const void* object) { return nativeBusy && Pending(object)==-1; }
    static void CompleteTransition(void* object) {
        auto bytes=static_cast<uint8_t*>(object);float target{};memcpy(&target,bytes+0x2dc,4);
        if (std::isfinite(target) && target>=0 && target<=255) { memcpy(bytes+0x2d8,&target,4);memcpy(bytes+0x2e0,&target,4); }
    }
};
}
