#pragma once
#include <caine/mod_loader.hpp>
#include <caine/update.hpp>
#include <d3d9.h>
#include <memory>

namespace caine {
struct MenuRow { std::string label; bool actionable{}; };
// Internal renderer control; the public CaineControlV1 plugin ABI is unchanged.
constexpr uint32_t MenuControlDropdown = 0x10000;
constexpr uint32_t MenuControlBindings = 0x10001;
struct MenuControl {
    uint32_t kind{}, id{}, flags{}, maxBytes{};
    std::string label, text, hint;
    double number{}, minimum{}, maximum{};
    std::vector<std::string> options;
};
struct NativeMenuItem { int id; std::string label; };
struct MenuView {
    std::vector<ModInfo> mods;
    std::string selected, message, pageTitle;
    std::vector<MenuRow> rows;
    std::vector<MenuControl> controls;
    std::vector<NativeMenuItem> nativeItems;
    std::filesystem::path background;
    bool home{};
    bool configure{}, wantsText{};
    bool overlay{};
    bool intro{}, skipping{};
    float skipProgress{};
    UpdateSnapshot update;
    bool updateOpen{};
};
enum class MenuActionKind { Select, Toggle, Configure, Row, Close, Details, Character, Control, Native, Update, UpdateInstall, UpdateClose };
struct MenuAction {
    MenuActionKind kind;
    std::string id;
    uint32_t value{};
    std::string text;
    double number{};
};
// All calls are serialized by the menu owner. Only Render touches Direct3D/ImGui.
class MenuRenderer {
public:
    MenuRenderer();
    ~MenuRenderer();
    bool Prepare(IDirect3DDevice9* device);
    bool Render(HWND window, const MenuView& view, std::vector<MenuAction>& actions);
    void Input(UINT message, WPARAM value, LPARAM data = 0);
    bool CapturingKey() const;
    void ClearInput();
private:
    struct State;
    std::unique_ptr<State> state_;
};
bool InstallMenuRenderer(const std::function<void(const std::string&)>& log);
void ConfigureMenuRenderer(bool modern);
void PaintModernMenu(IDirect3DDevice9* device);
}
