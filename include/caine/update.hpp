#pragma once
#include <caine/core.hpp>
#include <functional>

namespace caine {
inline constexpr char UpdateVersion[] = "0.3.18-framework-dev";
inline constexpr char UpdateRepository[] = "Teirdalin/PROJECT-CAINE";
inline constexpr char UpdateAsset[] = "PROJECT-CAINE-update.zip";
enum class UpdatePhase { Idle, Checking, Available, Downloading, Verifying, Staging, RestartReady, Error };
struct ReleaseUpdate {
    std::string version, url, sha256;
    uint64_t size{};
};
struct UpdateSnapshot {
    UpdatePhase phase{};
    bool available{};
    std::string version, message;
    float progress{};
    bool Busy() const { return phase >= UpdatePhase::Downloading && phase <= UpdatePhase::RestartReady; }
};
// Strict, bounded release protocol, also used by the helper and regression tests.
std::wstring UpdateWide(const std::string& text);
std::string UpdateUtf8(const std::wstring& text);
std::wstring QuoteUpdateArgument(const std::wstring& text);
bool NewerUpdateVersion(const std::string& candidate, const std::string& current);
bool TrustedUpdateUrl(const std::string& url, bool api = false);
ReleaseUpdate SelectReleaseUpdate(const std::string& json, const std::string& current, bool preview);
void InitializeUpdates(const std::filesystem::path& game, const std::filesystem::path& config,
                       const std::function<void(const std::string&)>& log);
UpdateSnapshot ReadUpdate();
void RequestUpdateInstall();
bool ClaimUpdateRestart();
}
