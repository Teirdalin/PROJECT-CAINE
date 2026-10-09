#pragma once
#include <caine/core.hpp>
#include <caine/mod_api.h>
#include <functional>
namespace caine {
bool InstallNativeBridge(const std::function<void(const std::string&)>& log);
bool ReadNativeDialogue(CaineDialogueV1& output);
bool ClaimNativeDialogue(void* owner,uint64_t token,bool enabled);
bool QueueNativeDialoguePick(void* owner,uint64_t token,int index);
void InvalidateNativeDialogue();
// Called only on the game window's thread at the render boundary.
void PulseNativeBridge(HWND window,const std::function<void(const std::string&)>& command={});
double NativeFieldOfView();
bool SetNativeFieldOfView(double value);
}
