#pragma once
#include <cstdint>
#include <string>
#include <filesystem>
#include <functional>
#include <windows.h>
#include <d3d9.h>

namespace caine {
// No game calls: the same hold rules are exercised with a synthetic clock.
class IntroHold {
public:
    explicit IntroHold(uint32_t milliseconds=1500);
    bool Update(bool opening, bool eligible, bool down, uint64_t now);
    float Progress() const { return progress_; }
    bool Requested() const { return requested_; }
private:
    uint32_t milliseconds_;
    uint64_t started_{}, last_{};
    float progress_{};
    bool armed_{}, holding_{}, requested_{}, sampled_{};
};
bool IsOpeningLevel(const std::string& name);
bool IntroMapSupported(const std::filesystem::path& path);
constexpr char IntroSkipCommands[]="vskip_intro\nent_fire embrace_o_matic Start\n";
void InitializeIntroSkip(const std::filesystem::path& config,
                         const std::function<void(const std::string&)>& log);
// Called only on the verified window/presentation thread; commands are queued.
void PaintIntroSkip(IDirect3DDevice9* device, HWND window, bool nativeMenuVisible);
bool CaptureIntroEscape(HWND window, UINT message, WPARAM value);
}
