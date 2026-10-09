#pragma once
#include <caine/menu_view.hpp>
#include <caine/core.hpp>
#include <functional>
#include <map>
#include <optional>
#include <array>

namespace caine {
struct VideoMode { int width{}, height{}, depth{}; };
struct KeyBinding { std::string key, command; };
struct GameMenuBackend {
    std::function<std::optional<double>(const char*)> read;
    std::function<void(const std::string&)> command;
    std::function<std::vector<VideoMode>()> modes;
    std::function<VideoMode()> currentMode;
    std::function<std::vector<KeyBinding>()> bindings;
    std::function<std::vector<std::string>()> keyNames;
    std::function<double()> fieldOfView;
    std::function<bool(double)> setFieldOfView;
    std::function<bool()> inGame;
};
std::optional<GameMenuBackend> NativeGameMenuBackend(const Module& client);
// Read an effective loose/VPK game resource; no game content is redistributed.
std::string ReadGameMenuResource(const std::filesystem::path& root,
                                 const std::filesystem::path& active, const std::string& resource);
enum class GameMenuPage { Settings, Credits, Load, Save };
class GameMenus {
public:
    GameMenus(GameMenuBackend backend, std::filesystem::path root, std::filesystem::path active);
    void Open(GameMenuPage page);
    void Build(MenuView& view);
    bool Action(uint32_t id, const std::string& text, double number); // true: return to main menu
    bool HasContinueSave(); // Cached listing; only the active profile's saves.
    bool ContinueLatest();  // Revalidates the file at click time.
private:
    GameMenuBackend backend_;
    std::filesystem::path root_, active_;
    std::filesystem::path config_;
    GameMenuPage page_{};
    std::string tab_{"Audio"}, message_, search_, selectedSave_, saveName_, confirmSave_;
    std::map<std::string,std::array<std::string,2>> bindingSlots_;
    void RefreshBindings();
    std::map<std::string,double> pending_;
    std::optional<VideoMode> pendingMode_;
    std::vector<std::pair<std::string,std::string>> actionsList_;
    std::vector<std::filesystem::path> saves_;
    std::optional<std::filesystem::path> continueSave_;
    ULONGLONG continueChecked_{};
    std::vector<KeyBinding> bindings_;
    ULONGLONG bindingsChecked_{};
    bool SubmitSave(bool saving, const std::string& name, bool confirmed);
    std::string credits_;
    std::map<uint32_t,std::function<bool(const std::string&,double)>> actions_;
};
}
