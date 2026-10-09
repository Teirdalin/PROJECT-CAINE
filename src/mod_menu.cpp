#include <caine/core.hpp>
#include <caine/mod_loader.hpp>
#include <caine/menu_view.hpp>
#include <caine/game_menu.hpp>
#include <caine/intro_skip.hpp>
#include <caine/native_menu_state.hpp>
#include <shellapi.h>
#include <algorithm>
#include <array>
#include <atomic>
#include <deque>
#include <mutex>
#include <cmath>
#include <set>
namespace {
constexpr char ClientHash[] = "9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01";
using ItemsFn = const int* (__thiscall*)(void*, int*);
using LabelFn = const char* (__thiscall*)(void*, int);
using ClickFn = void (__thiscall*)(void*, int);
using PaintFn = void (__thiscall*)(void*);
using VisibleFn = bool (__thiscall*)(void*);
ItemsFn itemsOriginal{};
LabelFn labelOriginal{};
ClickFn clickOriginal{};
PaintFn paintOriginal{};
uint8_t* clientBase{};
caine::Hooks* hooks{};
std::atomic<bool> installed{};
std::function<void(const std::string&)> logger;
std::recursive_mutex mutex;
enum class Page { Home, Mods, Details, Configure, Settings, Credits, Load, Save, Confirm };
Page page = Page::Home;
struct Row { std::string text; std::function<void()> action; };
std::vector<Row> rows;
std::deque<std::function<void()>> actions;
std::array<int, 16> items{};
std::string selected, message;
size_t offset{};
bool built{}, dirty = true;
bool builtModern{};
WNDPROC previousProcedure{};
HWND gameWindow{};
caine::MenuRenderer* renderer{};
bool modernReady{}, menuPainted{}, modernShown{}, nativeBusy{true};
bool modernEnabled{true};
void* nativeMenu{};
ULONGLONG lastModernFrame{};
std::vector<caine::MenuControl> controls;
std::vector<caine::NativeMenuItem> nativeItems;
std::unique_ptr<caine::GameMenus> gameMenus;
std::string gamePageTitle;
bool gameBridgeChecked{};
DWORD menuThread{};
bool overlayShown{};
std::string overlayMod;
std::deque<std::function<void()>> overlayActions;
int confirmAction{-1};
bool lastBusy{true};
bool updateOpen{};
// The supported native client treats a pending menu action as "busy" too.
// That action needs an immediate zero-alpha Paint to complete its lifecycle.
int PendingAction(void* self) { return caine::NativeMenuState::Pending(self); }
void CompleteFade(void* self) { caine::NativeMenuState::CompleteTransition(self); }

void Go(Page target) {
    if (target != page && logger) logger("CAINE_MENU_PAGE: " + std::to_string(static_cast<int>(target)));
    page = target; dirty = true; if (renderer) renderer->ClearInput();
}
void Add(std::string text, std::function<void()> action = {}) {
    const size_t limit = modernReady ? 8192 : 70;
    if (text.size() > limit) text = text.substr(0,limit-3) + "...";
    if (rows.size() < (modernReady ? 128u : 8u)) rows.push_back({std::move(text), std::move(action)});
}
void __cdecl AddModRow(void*, const char* label, uint32_t action) {
    Add(label ? label : "", action == UINT32_MAX ? std::function<void()>{} : [action] {
        const CaineMenuV1 menu{sizeof(CaineMenuV1),nullptr,AddModRow,nullptr,nullptr};
        if (!caine::ModMenu(selected, &menu, CAINE_MENU_ACTION, action)) Go(Page::Details);
    });
}
void __cdecl AddModControl(void*,const CaineControlV1* control) {
    if (!modernReady || !control || control->size<sizeof(CaineControlV1) || controls.size()>=4096 || control->kind>CAINE_CONTROL_TAB) return;
    auto copy=[](const char* value) {
        if (!value) return std::string{};
        const size_t length=strnlen_s(value,65537);
        if (length>65536) throw std::runtime_error("GUI field exceeds 64 KiB");
        return std::string(value,length);
    };
    if (!std::isfinite(control->number) || !std::isfinite(control->minimum) || !std::isfinite(control->maximum)) return;
    caine::MenuControl item{control->kind,control->id,control->flags,control->maxBytes,copy(control->label),copy(control->text),copy(control->hint),control->number,control->minimum,control->maximum};
    if (item.maxBytes>65536 || (item.kind==CAINE_CONTROL_INPUT && item.text.size()>(item.maxBytes?item.maxBytes:4096)))
        throw std::runtime_error("GUI input exceeds its declared capacity");
    controls.push_back(std::move(item));
}
CaineMenuV1 View(const CaineValueV1* value=nullptr) { return {sizeof(CaineMenuV1),nullptr,AddModRow,modernReady?AddModControl:nullptr,value}; }
void OpenConfig() {
    const auto menu=View();
    if (caine::ModMenu(selected, &menu, CAINE_MENU_OPEN, 0)) Go(Page::Configure);
    else { message = "Configuration is available after this mod starts"; Go(Page::Details); }
}
bool OpenGameMenu(Page target) {
    if (!gameMenus) return false;
    const auto kind=target==Page::Settings?caine::GameMenuPage::Settings:target==Page::Credits?caine::GameMenuPage::Credits:target==Page::Load?caine::GameMenuPage::Load:caine::GameMenuPage::Save;
    gameMenus->Open(kind);Go(target);return true;
}
bool IsGamePage() { return page==Page::Settings || page==Page::Credits || page==Page::Load || page==Page::Save; }
void PrepareGameBridge() {
    if (gameBridgeChecked) return;
    gameBridgeChecked=true;
    const auto backend=caine::NativeGameMenuBackend(caine::Module::Inspect(GetModuleHandleW(L"client.dll")));
    if (!backend) { logger("CAINE_GAME_MENUS_UNAVAILABLE: engine interface profile mismatch; native dialogs retained");return; }
    const auto root=caine::ModulePath(nullptr).parent_path();auto active=root/L"Vampire";
    int count{};const auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if (args) {
        for (int i=1;i+1<count;++i) if (_wcsicmp(args[i],L"-game")==0) {
            const std::wstring name=args[i+1];
            if (!name.empty() && name!=L"." && name!=L".." && name.find_first_of(L"\\/:")==std::wstring::npos) active=root/name;
            break;
        }
        LocalFree(args);
    }
    gameMenus=std::make_unique<caine::GameMenus>(*backend,root,active);
    logger("CAINE_GAME_MENUS_READY: guarded engine settings, bindings, video modes and save/load actions");
}
void BuildRows() {
    auto previous = std::move(rows); rows.clear(); controls.clear();gamePageTitle.clear();
    if (IsGamePage() && gameMenus) { caine::MenuView view;view.mods=caine::ModCatalog();gameMenus->Build(view);gamePageTitle=view.pageTitle;controls=std::move(view.controls);message=view.message; }
    if (page == Page::Mods) {
        const auto mods = caine::ModCatalog();
        Add("PROJECT CAINE - Mods");
        if (mods.empty()) Add("No mods installed in the game's mods folder");
        for (size_t i = offset; i < mods.size() && i < offset + 4; ++i) {
            const auto id = mods[i].id;
            Add(mods[i].name + (mods[i].active ? " [loaded]" : " [" + mods[i].state + "]"),
                [id] { selected = id; message.clear(); Go(Page::Details); });
        }
        if (mods.size() > 4) Add("Next mods", [mods] { offset = (offset + 4) % mods.size(); dirty = true; });
        Add("Back to main menu", [] { Go(Page::Home); });
    } else if (page == Page::Details) {
        for (const auto& mod : caine::ModCatalog()) if (mod.id == selected) {
            Add(mod.name + " " + mod.version);
            Add(mod.author.empty() ? mod.id : "By " + mod.author);
            Add(mod.state + " / next launch: " + (mod.enabled ? "enabled" : "disabled"));
            Add(mod.description);
            if (!mod.config.empty()) Add(mod.enabled ? "Disable (restart required)" : "Enable (restart required)", [mod] {
                if (caine::SetModEnabled(mod.id, !mod.enabled, message)) message = "Saved. Restart Bloodlines to apply.";
                dirty = true;
            });
            Add("Configure", OpenConfig);
            if (!message.empty()) Add(message);
            Add("Back to Mods", [] { Go(Page::Mods); });
            break;
        }
    } else if (page == Page::Configure) {
        const auto menu=View();
        if (!caine::ModMenu(selected, &menu, CAINE_MENU_BUILD, 0)) { Go(Page::Details); return; }
    }
    bool changed = previous.size() != rows.size();
    if (!changed) for (size_t i = 0; i < rows.size(); ++i) if (previous[i].text != rows[i].text) changed = true;
    if (changed) dirty = true;
    else {
        // Keep label storage stable while native widgets still display this page.
        for (size_t i = 0; i < rows.size(); ++i) previous[i].action = std::move(rows[i].action);
        rows = std::move(previous);
    }
}
LRESULT CALLBACK WindowProcedure(HWND window, UINT msg, WPARAM wp, LPARAM lp) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (caine::CaptureIntroEscape(window,msg,wp)) return 0;
    const bool modal = modernReady && (modernShown || overlayShown) && GetTickCount64()-lastModernFrame<500;
    if (modal && renderer) renderer->Input(msg, wp, lp);
    const bool bindingCapture=modal && renderer && renderer->CapturingKey();
    if (page == Page::Configure && controls.empty()) {
        const auto menu=View();
        if (caine::ModMenu(selected, &menu, CAINE_MENU_WANTS_TEXT, 0)) {
            if (msg == WM_CHAR) {
                actions.push_back([value = static_cast<uint32_t>(wp)] {
                    const auto view=View();
                    caine::ModMenu(selected, &view, CAINE_MENU_CHAR, value); dirty = true;
                });
                return 0;
            }
            if (msg == WM_KEYDOWN || msg == WM_KEYUP) return 0;
        }
    }
    if (modal) {
        if (msg == WM_KEYDOWN && wp == VK_ESCAPE && !bindingCapture) {
            if (overlayShown) overlayActions.push_back([id=overlayMod] { caine::ModGameUI(id,nullptr,CAINE_GAMEUI_CLOSE,0); });
            else if (page!=Page::Home) actions.push_back([] { confirmAction=-1;Go(Page::Home); });
            else if (std::any_of(nativeItems.begin(),nativeItems.end(),[](const auto& item){return item.id==11;})) actions.push_back([] { if (nativeMenu) clickOriginal(nativeMenu,11); });
        }
        // DefWindowProc releases foreground raw-input storage without forwarding
        // captured input to the game. System keys (Alt-F4/Alt-Tab) remain native.
        if (msg == WM_INPUT) return DefWindowProcW(window, msg, wp, lp);
        if (bindingCapture && (msg==WM_SYSKEYDOWN || msg==WM_SYSKEYUP) && wp!=VK_TAB && wp!=VK_F4) return 0;
        if ((msg >= WM_MOUSEFIRST && msg <= WM_MOUSELAST) || msg == WM_KEYDOWN || msg == WM_KEYUP || msg == WM_CHAR || msg == WM_DEADCHAR) return 0;
    }
    return previousProcedure ? CallWindowProcW(previousProcedure, window, msg, wp, lp) : DefWindowProcW(window, msg, wp, lp);
}
BOOL CALLBACK FindGameWindow(HWND window, LPARAM) {
    DWORD pid{}; GetWindowThreadProcessId(window, &pid);
    if (pid == GetCurrentProcessId() && IsWindowVisible(window) && !GetWindow(window, GW_OWNER)) { gameWindow = window; return FALSE; }
    return TRUE;
}
const int* __fastcall MenuItems(void* self, void*, int* count) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!installed.load()) return itemsOriginal(self, count);
    if (modernReady && modernEnabled && (page != Page::Home || !nativeBusy)) { if (count) *count=0;return items.data(); }
    if (page != Page::Home) {
        if (modernReady && modernEnabled) { if (count) *count = 0; return items.data(); }
        for (size_t i = 0; i < rows.size(); ++i) items[i] = static_cast<int>(i);
        if (count) *count = static_cast<int>(rows.size());
        return items.data();
    }
    int length{}; const auto original = itemsOriginal(self, &length);
    if (count) *count = length;
    if (!original || length < 0 || length > 12) return original;
    int n{}; bool added = false;
    for (int i = 0; i < length; ++i) {
        if (!added && (original[i] == 10 || original[i] == 9)) { items[n++] = 5; added = true; }
        if (original[i] != 5) items[n++] = original[i];
    }
    if (!added) items[n++] = 5;
    if (count) *count = n;
    return items.data();
}
const char* __fastcall MenuLabel(void* self, void*, int id) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (installed.load()) {
        if (page == Page::Home && id == 5) return "MODS";
        if (page != Page::Home && id >= 0 && static_cast<size_t>(id) < rows.size()) return rows[id].text.c_str();
    }
    return labelOriginal(self, id);
}
void __fastcall MenuClick(void* self, void*, int id) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (logger) logger("CAINE_MENU_CLICK: native action id=" + std::to_string(id));
    if (installed.load()) {
        if (page != Page::Home) {
            if (modernReady) return;
            if (id >= 0 && static_cast<size_t>(id) < rows.size() && rows[id].action) actions.push_back(rows[id].action);
            return;
        }
        if (id == 5) { actions.push_back([] { Go(Page::Mods); }); return; }
        if (modernReady && modernEnabled && (id==9 || id==10 || id==13)) {
            actions.push_back([id] { confirmAction=id==13?10:id;Go(Page::Confirm); });return;
        }
        if (modernReady && gameMenus && (id==12 || id==4 || id==1 || id==3)) {
            // Loading keeps Unscripted readiness gating, like the native route.
            if (id==1) { const auto blocking=caine::BlockingMod();if (!blocking.empty()) { actions.push_back([blocking]{selected=blocking;OpenConfig();});return; } }
            actions.push_back([id] { OpenGameMenu(id==12?Page::Credits:id==4?Page::Settings:id==1?Page::Load:Page::Save); });return;
        }
        if (id == 0 || id == 1 || id == 2) {
            const auto blocking = caine::BlockingMod();
            if (!blocking.empty()) { actions.push_back([blocking] { selected = blocking; OpenConfig(); }); return; }
        }
    }
    clickOriginal(self, id);
    if (logger) logger("CAINE_MENU_NATIVE_ACTION_RETURNED: id=" + std::to_string(id));
}
void __fastcall MenuPaint(void* self, void*) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!installed.load()) { paintOriginal(self); return; }
    // Mirror the native routine's leading vtable +0x58 visibility test. The
    // engine can call this routine for a hidden menu, including during gameplay.
    const auto visible = reinterpret_cast<VisibleFn>((*reinterpret_cast<void***>(self))[0x58 / sizeof(void*)]);
    if (!visible(self)) {
        menuPainted = false; modernShown=false;
        if (modernReady && page != Page::Home) Go(Page::Home);
        paintOriginal(self); return;
    }
    try {
        menuThread = GetCurrentThreadId(); menuPainted = true;
        caine::PulseModMenus();
        nativeMenu=self;
        PrepareGameBridge();
        while (!actions.empty()) { auto action = std::move(actions.front()); actions.pop_front(); action(); }
        nativeBusy=caine::NativeMenuState::ChildBusy(reinterpret_cast<VisibleFn>(clientBase+0x65c90)(self),self);
        if (nativeBusy!=lastBusy) {
            logger("CAINE_MENU_NATIVE_DIALOG: busy="+std::to_string(nativeBusy)+" pending="+std::to_string(PendingAction(self)));
            lastBusy=nativeBusy;
        }
        if (nativeBusy && page==Page::Home) { modernShown=false;if (renderer) renderer->ClearInput(); }
        nativeItems.clear();
        if (page==Page::Home && !nativeBusy) {
            int count{};const auto ids=itemsOriginal(self,&count);
            if (ids && count>=0 && count<=12) {
                for (int i=0;i<count;++i) {
                    const int id=ids[i];
                    if (id==9 || id==10) { nativeItems.push_back({5,"Mods"});if (gameMenus) nativeItems.push_back({12,"Credits"}); }
                    const auto label=labelOriginal(self,id);
                    nativeItems.push_back({id,id==4?std::string("Settings"):(label?std::string(label):std::string("Menu"))});
                }
                if (std::none_of(nativeItems.begin(),nativeItems.end(),[](const auto& item){return item.id==5;})) nativeItems.push_back({5,"Mods"});
                if (std::any_of(nativeItems.begin(),nativeItems.end(),[](const auto& item){return item.id==9;}) &&
                    std::none_of(nativeItems.begin(),nativeItems.end(),[](const auto& item){return item.id==10;}))
                    nativeItems.push_back({13,"Quit to Desktop"});
            }
        }
        // Preserve rows and their callbacks throughout an active mouse click.
        if (!(GetAsyncKeyState(VK_LBUTTON) & 0x8000)) {
            BuildRows();
            const bool custom=modernReady && modernEnabled;
            if (!built || builtModern!=custom || (!custom && dirty)) {
                reinterpret_cast<PaintFn>(clientBase + 0x66250)(self);
                reinterpret_cast<PaintFn>(clientBase + 0x65f40)(self);
                reinterpret_cast<PaintFn>(clientBase + 0x660e0)(self);
                builtModern=custom;
                if (!built) { built = true; logger("CAINE_MODS_MENU_BUILT: native buttons constructed; visual acceptance pending"); }
            }
            dirty=false;
        }
        if (!gameWindow) {
            EnumWindows(FindGameWindow, 0);
            if (gameWindow) previousProcedure = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gameWindow, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WindowProcedure)));
        }
    } catch (...) { message = "Mod configuration operation failed"; }
    // Exact supported client profile: native constructor and Paint use these
    // float alpha fields. Keep lifecycle/audio/native dialogs in original Paint,
    // but complete the visual fade immediately while CAINE owns the menu.
    if (modernReady && modernEnabled) CompleteFade(self);
    paintOriginal(self);
    // Original Paint may complete Resume/disconnect/quit and hide this panel.
    // Do not render a stale menu or let it capture the next gameplay input.
    if (!visible(self)) { menuPainted=false;modernShown=false; }
}
}
namespace caine {
namespace {
void CollectOverlayControl(void* context,const CaineControlV1* value) {
    if (!value || value->size<sizeof(CaineControlV1) || value->kind>CAINE_CONTROL_TAB || value->maxBytes>65536) return;
    if (!std::isfinite(value->number) || !std::isfinite(value->minimum) || !std::isfinite(value->maximum)) return;
    auto& output=*static_cast<std::vector<MenuControl>*>(context);
    if (output.size()>=4096) return;
    auto copy=[](const char* text) { if (!text) return std::string{};const auto count=strnlen_s(text,65537);if (count>65536) throw std::runtime_error("Overlay text exceeds capacity");return std::string(text,count); };
    output.push_back({value->kind,value->id,value->flags,value->maxBytes,copy(value->label),copy(value->text),copy(value->hint),value->number,value->minimum,value->maximum});
}
bool PaintGameUI(IDirect3DDevice9* device) {
    while (!overlayActions.empty()) { auto action=std::move(overlayActions.front());overlayActions.pop_front();action(); }
    const auto owner=ActiveGameUI();
    if (owner.empty() || !gameWindow) {
        if (overlayShown && renderer) renderer->ClearInput();
        overlayShown=false;overlayMod.clear();return false;
    }
    if (!renderer) renderer=new MenuRenderer();
    if (!renderer->Prepare(device)) return false;
    modernReady=true;
    MenuView view;view.overlay=true;view.wantsText=true;view.selected=owner;view.pageTitle=owner;
    for (const auto& mod:ModCatalog()) if (mod.id==owner) view.pageTitle=mod.name;
    const CaineMenuV1 api{sizeof(CaineMenuV1),&view.controls,nullptr,CollectOverlayControl,nullptr};
    if (!ModGameUI(owner,&api,CAINE_MENU_BUILD,0)) return false;
    std::vector<MenuAction> events;
    if (!renderer->Render(gameWindow,view,events)) return false;
    modernShown=false;overlayShown=true;overlayMod=owner;lastModernFrame=GetTickCount64();
    for (auto& event:events) {
        if (event.kind==MenuActionKind::Control) overlayActions.push_back([owner,event] {
            const CaineValueV1 value{sizeof(CaineValueV1),event.text.c_str(),event.number};
            const CaineMenuV1 api{sizeof(CaineMenuV1),nullptr,nullptr,nullptr,&value};
            ModGameUI(owner,&api,CAINE_MENU_VALUE,event.value);
        });
        else if (event.kind==MenuActionKind::Close) overlayActions.push_back([owner] { ModGameUI(owner,nullptr,CAINE_GAMEUI_CLOSE,0); });
    }
    return true;
}
}
void ConfigureMenuRenderer(bool modern) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    modernEnabled=modern;
}
void PaintModernMenu(IDirect3DDevice9* device) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!installed.load()) return;
    if (!gameWindow) {
        EnumWindows(FindGameWindow,0);
        if (gameWindow) previousProcedure=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(gameWindow,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(WindowProcedure)));
    }
    PaintIntroSkip(device,gameWindow,menuPainted);
    // Only the passive intro overlay can draw without a native menu paint.
    // The supported engine paints and presents on the same thread; fail closed
    // on a different rendering model rather than run mod callbacks across threads.
    if (!menuPainted || GetCurrentThreadId() != menuThread || !modernEnabled) {
        menuPainted=false;modernShown=false;
        if (modernEnabled) PaintGameUI(device);
        return;
    }
    menuPainted = false;
    if (!renderer) renderer = new MenuRenderer();
    if (!renderer->Prepare(device)) return;
    if (!modernReady) { modernReady = true; dirty = true; logger("CAINE_MODERN_MENU_READY: DirectX 9 renderer initialized"); }
    if (!gameWindow || (page==Page::Home && nativeBusy)) { modernShown=false;PaintGameUI(device);return; }
    overlayShown=false;
    MenuView view;
    view.home=page==Page::Home;
    view.nativeItems=nativeItems;
    view.update=ReadUpdate();view.updateOpen=updateOpen;
    // The native quit route runs on the verified menu thread after helper readiness.
    if (ClaimUpdateRestart()) actions.push_back([] {
        if (nativeMenu) { clickOriginal(nativeMenu,10);CompleteFade(nativeMenu);logger("CAINE_UPDATE_RESTART: graceful native quit"); }
    });
    view.background=ModulePath(GetModuleHandleW(L"CAINE.asi")).parent_path()/L"CAINE"/L"background.png";
    view.mods = ModCatalog(); view.message = message; view.configure = page == Page::Configure;
    if (selected.empty() && !view.mods.empty()) selected = view.mods.front().id;
    view.selected = selected;
    if (page==Page::Confirm) {
        view.pageTitle=confirmAction==9?"Quit to Main Menu?":"Quit to Desktop?";
        view.controls={{CAINE_CONTROL_TEXT,0,0,0,"Any unsaved progress will be lost."},
            {CAINE_CONTROL_BUTTON,1,0,0,confirmAction==9?"Quit to Main Menu":"Quit to Desktop"},
            {CAINE_CONTROL_BUTTON,2,0,0,"Cancel"}};
    }
    if (IsGamePage()) { view.pageTitle=gamePageTitle;view.controls=controls; }
    if (view.configure) {
        const auto menu=View();
        view.wantsText = ModMenu(selected, &menu, CAINE_MENU_WANTS_TEXT, 0);
        view.controls=controls;
        for (const auto& row : rows) view.rows.push_back({row.text, static_cast<bool>(row.action)});
    }
    std::vector<MenuAction> events;
    if (!renderer->Render(gameWindow, view, events)) { modernShown=false;return; }
    modernShown=true;lastModernFrame=GetTickCount64();
    for (auto& event : events) {
        // Capture row callbacks now; their indices can change on the next build.
        if (event.kind == MenuActionKind::Row) {
            if (event.value < rows.size() && rows[event.value].action) actions.push_back(rows[event.value].action);
        } else if (event.kind==MenuActionKind::Control) {
            actions.push_back([event]() mutable {
                if (page==Page::Confirm) {
                    const int action=confirmAction;confirmAction=-1;Go(Page::Home);
                    if (event.value==1 && nativeMenu && (action==9 || action==10)) {
                        // Native id 9 normally opens the stock confirmation dialog.
                        // The custom confirmation has already obtained the choice.
                        auto flag=reinterpret_cast<int*>(clientBase+0x3ef0b8);
                        const int previous=*flag;
                        if (action==9) *flag=1;
                        clickOriginal(nativeMenu,action);
                        if (action==9) *flag=previous;
                        CompleteFade(nativeMenu);
                        logger("CAINE_MENU_CONFIRMED_EXIT: native action="+std::to_string(action));
                    }
                    return;
                }
                if (IsGamePage() && gameMenus) { if (gameMenus->Action(event.value,event.text,event.number)) Go(Page::Home);return; }
                const CaineValueV1 value{sizeof(CaineValueV1),event.text.c_str(),event.number};
                const auto menu=View(&value);
                if (!ModMenu(event.id,&menu,CAINE_MENU_VALUE,event.value)) Go(Page::Details);
                if (!event.text.empty()) SecureZeroMemory(event.text.data(),event.text.size());
            });
            if (!event.text.empty()) SecureZeroMemory(event.text.data(),event.text.size());
        } else actions.push_back([event] {
            switch (event.kind) {
            case MenuActionKind::Update: updateOpen=true;RequestUpdateInstall();break;
            case MenuActionKind::UpdateClose: if(!ReadUpdate().Busy())updateOpen=false;break;
            case MenuActionKind::UpdateInstall: RequestUpdateInstall();break;
            case MenuActionKind::Native:
                if (nativeMenu) MenuClick(nativeMenu,nullptr,static_cast<int>(event.value));
                break;
            case MenuActionKind::Select: selected = event.id; message.clear(); Go(Page::Details); break;
            case MenuActionKind::Toggle: if (SetModEnabled(event.id, event.value != 0, message)) message = "Saved. Restart Bloodlines to apply."; break;
            case MenuActionKind::Configure: selected = event.id; OpenConfig(); break;
            case MenuActionKind::Close: Go(Page::Home); break;
            case MenuActionKind::Details: Go(Page::Details); break;
            default: break;
            }
        });
    }
}
bool InstallModsMenu(const std::function<void(const std::string&)>& log) {
    if (installed.load()) return true;
    try {
        const auto client = Module::Inspect(GetModuleHandleW(L"client.dll"));
        if (client.sha256 != ClientHash) return false;
        clientBase = client.base; logger = log;
        hooks = new Hooks();
        std::string error;
        if (!hooks->InstallBatch(client, {
            {{"menu.items",ClientHash,0x678a0,{0xe8,0xab,0xff,0xff,0xff,0x85,0xc0,0x75,0x16}},reinterpret_cast<void*>(MenuItems),reinterpret_cast<void**>(&itemsOriginal)},
            {{"menu.label",ClientHash,0x65eb0,{0x56,0x57,0x8b,0x7c,0x24,0x0c,0x8b,0x04,0xbd}},reinterpret_cast<void*>(MenuLabel),reinterpret_cast<void**>(&labelOriginal)},
            {{"menu.dispatch",ClientHash,0x67610,{0x56,0x8b,0xf1,0x57,0x83,0xbe,0xe8,0x02,0,0,0xff}},reinterpret_cast<void*>(MenuClick),reinterpret_cast<void**>(&clickOriginal)},
            {{"menu.paint",ClientHash,0x65af0,{0x56,0x8b,0xf1,0x8b,0x06,0xff,0x50,0x58}},reinterpret_cast<void*>(MenuPaint),reinterpret_cast<void**>(&paintOriginal)}},error)) {
            log("CAINE_MODS_MENU_REJECTED: " + error); return false;
        }
        installed.store(true);
        log("CAINE_MODS_MENU_HOOKS: installed four guarded native menu hooks");
        return true;
    } catch (const std::exception& failure) { log(failure.what()); return false; }
}
}

