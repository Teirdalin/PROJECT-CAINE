#include <caine/overlay_input.hpp>
#include <caine/logging.hpp>
#include <atomic>
#include <mutex>

namespace caine {
namespace {
constexpr char ClientHash[]="9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01";
using MouseFn=void(__thiscall*)(void*);
using MoveFn=void(__thiscall*)(void*,void*);
using EventFn=void(__thiscall*)(void*,int,int);
MouseFn activate{},accumulate{},reset{},deactivate{};
MoveFn move{};
EventFn buttons{};
Hooks* hooks{};
uint8_t* input{};
std::atomic<HWND> ownerWindow{};
std::atomic<ULONGLONG> lease{};
std::recursive_mutex stateMutex;
bool held{},restoreActive{};
#ifndef CAINE_OVERLAY_INPUT_TEST
RECT previousClip{};
bool restoreClip{};
#endif
#ifdef CAINE_OVERLAY_INPUT_TEST
HWND testForeground{};
ULONGLONG testTime{1000};
HWND Foreground() { return testForeground; }
ULONGLONG Now() { return testTime; }
#else
HWND Foreground() { return GetForegroundWindow(); }
ULONGLONG Now() { return GetTickCount64(); }
#endif
bool Captured() {
    const auto until=lease.load();
    return until && Now()<until && Foreground()==ownerWindow.load();
}
void Release() {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (held) { held=false;TraceLog("CAINE_OVERLAY_MOUSE_RELEASED"); }
    // Native focus handling may have already reactivated the mouse. Restore
    // only the mode which was active before capture, on the foreground game.
    if (restoreActive && Foreground()==ownerWindow.load()) {
        restoreActive=false;
        if (!*reinterpret_cast<int*>(input+0x28)) activate(input);
#ifndef CAINE_OVERLAY_INPUT_TEST
        if (restoreClip) { ClipCursor(&previousClip);restoreClip=false; }
#endif
    }
}
bool Block() { if (Captured()) return true;Release();return false; }
void __fastcall Activate(void* self,void*) {
    if (!Block()) activate(self);
    else { std::lock_guard<std::recursive_mutex> lock(stateMutex);restoreActive=true; }
}
void __fastcall Deactivate(void* self,void*) {
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (held) restoreActive=false;
#ifndef CAINE_OVERLAY_INPUT_TEST
    if (held) restoreClip=false; // a native panel/focus transition now owns clipping
#endif
    deactivate(self);
}
void __fastcall Accumulate(void* self,void*) { if (!Block()) accumulate(self); }
void __fastcall Reset(void* self,void*) { if (!Block()) reset(self); }
void __fastcall Move(void* self,void*,void* command) { if (!Block()) move(self,command); }
void __fastcall Buttons(void* self,void*,int mask,int state) { if (!Block()) buttons(self,mask,state); }
}
bool InstallOverlayInput(const Module& client,const std::function<void(const std::string&)>& log) {
    if (hooks) return true;
    if (client.sha256!=ClientHash) return false;
    // Independently reconstructed installed CInput object and vtable. These
    // slots/entry bytes are from this 2004 binary, not a modern Source layout.
    input=client.base+0x2ea6c8;
    if (*reinterpret_cast<uint8_t**>(input)!=client.base+0x224d4c) {
        log("CAINE_OVERLAY_INPUT_UNAVAILABLE: native CInput object is not initialized");return false;
    }
    std::vector<uint8_t> accumulateBytes{0x83,0xec,0x08,0x56,0x8b,0xf1,0x8b,0x0d,0,0,0,0};
    const auto operand=reinterpret_cast<uint32_t>(client.base+0x4d400c);
    memcpy(accumulateBytes.data()+8,&operand,4);
    std::vector<uint8_t> resetBytes{0x56,0x8b,0xf1,0x8b,0x0d,0,0,0,0};
    const auto resetOperand=reinterpret_cast<uint32_t>(client.base+0x4a57d4);
    memcpy(resetBytes.data()+5,&resetOperand,4);
    auto owner=new Hooks();std::string error;
    const std::vector<uint8_t> deactivateBytes{0x56,0x8b,0xf1,0x8b,0x46,0x28,0x85,0xc0};
    if (!client.Executable(0x106290,deactivateBytes.size()) || memcmp(client.base+0x106290,deactivateBytes.data(),deactivateBytes.size())!=0 ||
        !owner->InstallBatch(client,{
        {{"input.overlay.activate",ClientHash,0x106240,{0x56,0x8b,0xf1,0x83,0x7e,0x28,0x01,0x74,0x3d}},reinterpret_cast<void*>(Activate),reinterpret_cast<void**>(&activate)},
        {{"input.overlay.deactivate",ClientHash,0x106290,deactivateBytes},reinterpret_cast<void*>(Deactivate),reinterpret_cast<void**>(&deactivate)},
        {{"input.overlay.accumulate",ClientHash,0x1067d0,accumulateBytes},reinterpret_cast<void*>(Accumulate),reinterpret_cast<void**>(&accumulate)},
        {{"input.overlay.recenter",ClientHash,0x106310,resetBytes},reinterpret_cast<void*>(Reset),reinterpret_cast<void**>(&reset)},
        {{"input.overlay.move",ClientHash,0x1068a0,{0x83,0xec,0x1c,0x8d,0x54,0x24,0x10,0x56,0x8b,0xf1}},reinterpret_cast<void*>(Move),reinterpret_cast<void**>(&move)},
        {{"input.overlay.buttons",ClientHash,0x106340,{0x55,0x8b,0xe9,0x57,0x33,0xff,0x8b,0x45,0x4c}},reinterpret_cast<void*>(Buttons),reinterpret_cast<void**>(&buttons)}},error)) {
        delete owner;input=nullptr;log("CAINE_OVERLAY_INPUT_UNAVAILABLE: "+error);return false;
    }
    hooks=owner;
    log("CAINE_OVERLAY_INPUT_READY: guarded mouse activation, polling, camera, recenter and button capture");return true;
}
void CaptureOverlayInput(HWND window,bool capture) {
    if (!hooks) return;
    std::lock_guard<std::recursive_mutex> lock(stateMutex);
    if (window) ownerWindow.store(window);
    if (capture && window && Foreground()==window) {
        lease.store(Now()+500);
        if (!held) {
            held=true;restoreActive=*reinterpret_cast<int*>(input+0x28)!=0;
            deactivate(input);
#ifndef CAINE_OVERLAY_INPUT_TEST
            restoreClip=restoreActive && GetClipCursor(&previousClip)!=FALSE;
            ClipCursor(nullptr);
#endif
            TraceLog("CAINE_OVERLAY_MOUSE_CAPTURED: previous_active="+std::to_string(restoreActive));
        }
    } else { lease.store(0);Release(); }
}
#ifdef CAINE_OVERLAY_INPUT_TEST
void OverlayInputTestState(HWND foreground,ULONGLONG now) { testForeground=foreground;testTime=now; }
#endif
}
