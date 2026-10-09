#pragma once
#include <caine/core.hpp>
#include <functional>

namespace caine {
bool InstallOverlayInput(const Module& client, const std::function<void(const std::string&)>& log);
// A frame lease prevents a failed renderer or abandoned overlay retaining input.
void CaptureOverlayInput(HWND window, bool capture);
}
