#pragma once
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace caine {
struct FrameworkOption {
    const char* id;
    const wchar_t* section;
    const wchar_t* key;
    const char* label;
    const char* hint;
    double initial, minimum, maximum;
    bool toggle, live;
};
const std::vector<FrameworkOption>& FrameworkOptions();
const std::vector<FrameworkOption>& GameplayOptions();
bool FirstPersonMeleeEnabled();
bool MeleeBodyCamera();
double ReadFrameworkOption(const std::filesystem::path& config, const FrameworkOption& option);
bool WriteFrameworkOption(const std::filesystem::path& config, const FrameworkOption& option, double value);
void InitializeFrameworkPreferences(const std::filesystem::path& config);
float MenuScale();
// One resolver for menus, cinematics and profile-specific graphics persistence.
std::filesystem::path ActiveGameFolder(const std::filesystem::path& root);
bool UnofficialPatchInstalled(const std::filesystem::path& root);
}
