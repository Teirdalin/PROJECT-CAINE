#include <caine/menu_view.hpp>
#include <caine/key_input.hpp>
#include <imgui.h>
#include <backends/imgui_impl_dx9.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <windowsx.h>
#include <algorithm>
#include <map>
#include <cmath>
#include <cstring>

using Microsoft::WRL::ComPtr;
namespace caine {
namespace {
ImGuiKey Key(WPARAM key) {
    switch (key) {
    case VK_TAB: return ImGuiKey_Tab;
    case VK_LEFT: return ImGuiKey_LeftArrow;
    case VK_RIGHT: return ImGuiKey_RightArrow;
    case VK_UP: return ImGuiKey_UpArrow;
    case VK_DOWN: return ImGuiKey_DownArrow;
    case VK_BACK: return ImGuiKey_Backspace;
    case VK_DELETE: return ImGuiKey_Delete;
    case VK_HOME: return ImGuiKey_Home;
    case VK_END: return ImGuiKey_End;
    case VK_ESCAPE: return ImGuiKey_Escape;
    case VK_CONTROL: return ImGuiMod_Ctrl;
    case VK_MENU: return ImGuiMod_Alt;
    case 'A': return ImGuiKey_A;
    case 'C': return ImGuiKey_C;
    case 'V': return ImGuiKey_V;
    case 'X': return ImGuiKey_X;
    case 'Y': return ImGuiKey_Y;
    case 'Z': return ImGuiKey_Z;
    case VK_RETURN: return ImGuiKey_Enter;
    case VK_SPACE: return ImGuiKey_Space;
    case VK_SHIFT: return ImGuiMod_Shift;
    default: return ImGuiKey_None;
    }
}
struct ContextScope {
    ImGuiContext* previous;
    explicit ContextScope(ImGuiContext* context) : previous(ImGui::GetCurrentContext()) { ImGui::SetCurrentContext(context); }
    ~ContextScope() { ImGui::SetCurrentContext(previous); }
};
struct Logo { ComPtr<IDirect3DTexture9> texture; UINT width{}, height{}; };
Logo LoadLogo(IDirect3DDevice9* device, const std::filesystem::path& path) {
    Logo result;
    if (path.empty()) return result;
    std::error_code error;
    const auto bytes = std::filesystem::file_size(path, error);
    if (error || bytes > 16 * 1024 * 1024) return result;
    const HRESULT apartment = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
    struct Apartment { bool owned; ~Apartment() { if (owned) CoUninitialize(); } } scope{SUCCEEDED(apartment)};
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICFormatConverter> converter;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))) ||
        FAILED(factory->CreateDecoderFromFilename(path.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand, &decoder)) ||
        FAILED(decoder->GetFrame(0, &frame)) || FAILED(frame->GetSize(&result.width, &result.height)) ||
        !result.width || !result.height || result.width > 4096 || result.height > 4096 ||
        FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(frame.Get(), GUID_WICPixelFormat32bppBGRA, WICBitmapDitherTypeNone, nullptr, 0, WICBitmapPaletteTypeCustom))) return {};
    if (FAILED(device->CreateTexture(result.width, result.height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_MANAGED, &result.texture, nullptr))) return {};
    D3DLOCKED_RECT rect{};
    if (FAILED(result.texture->LockRect(0, &rect, nullptr, 0))) return {};
    const auto hr = converter->CopyPixels(nullptr, static_cast<UINT>(rect.Pitch), static_cast<UINT>(rect.Pitch) * result.height, static_cast<BYTE*>(rect.pBits));
    result.texture->UnlockRect(0);
    if (FAILED(hr)) return {};
    return result;
}
void Theme(float scale) {
    ImGuiStyle style;
    style.WindowPadding = {24, 20}; style.FramePadding = {12, 9};
    style.ItemSpacing = {12, 12}; style.ItemInnerSpacing = {8, 6};
    style.WindowRounding = 10; style.ChildRounding = 6; style.FrameRounding = 4;
    style.ScrollbarSize = 12; style.GrabMinSize = 12; style.WindowBorderSize = 1;
    auto& c = style.Colors;
    c[ImGuiCol_Text] = {0.92f,0.91f,0.89f,1}; c[ImGuiCol_TextDisabled] = {0.59f,0.60f,0.64f,1};
    c[ImGuiCol_WindowBg] = {0.045f,0.043f,0.053f,0.99f}; c[ImGuiCol_ChildBg] = {0.070f,0.063f,0.075f,1};
    c[ImGuiCol_TitleBg] = {0.14f,0.06f,0.08f,1}; c[ImGuiCol_TitleBgActive] = {0.28f,0.075f,0.12f,1};
    c[ImGuiCol_ModalWindowDimBg] = {0,0,0,0.55f};
    c[ImGuiCol_Border] = {0.27f,0.18f,0.21f,1}; c[ImGuiCol_FrameBg] = {0.15f,0.12f,0.15f,1};
    c[ImGuiCol_FrameBgHovered] = {0.27f,0.13f,0.17f,1}; c[ImGuiCol_FrameBgActive] = {0.36f,0.13f,0.18f,1};
    c[ImGuiCol_Button] = {0.28f,0.075f,0.12f,1}; c[ImGuiCol_ButtonHovered] = {0.46f,0.11f,0.17f,1};
    c[ImGuiCol_ButtonActive] = {0.58f,0.12f,0.19f,1}; c[ImGuiCol_Header] = {0.28f,0.09f,0.14f,1};
    c[ImGuiCol_HeaderHovered] = c[ImGuiCol_ButtonHovered]; c[ImGuiCol_HeaderActive] = c[ImGuiCol_ButtonActive];
    c[ImGuiCol_CheckMark] = {0.89f,0.32f,0.36f,1}; c[ImGuiCol_SliderGrab] = {0.75f,0.20f,0.29f,1}; c[ImGuiCol_SliderGrabActive] = {0.94f,0.32f,0.39f,1}; c[ImGuiCol_Separator] = c[ImGuiCol_Border];
    c[ImGuiCol_ScrollbarBg] = {0.05f,0.04f,0.06f,1}; c[ImGuiCol_ScrollbarGrab] = {0.30f,0.22f,0.27f,1};
    c[ImGuiCol_ScrollbarGrabHovered] = {0.44f,0.27f,0.33f,1}; c[ImGuiCol_ScrollbarGrabActive] = {0.56f,0.27f,0.33f,1};
    c[ImGuiCol_NavCursor] = {0.9f,0.4f,0.4f,1};
    style.ScaleAllSizes(scale); ImGui::GetStyle() = style;
}
}
struct MenuRenderer::State {
    ImGuiContext* context{};
    IDirect3DDevice9* device{};
    std::map<std::filesystem::path, Logo> logos;
    struct InputEvent { UINT message; WPARAM value; LPARAM data; };
    std::vector<InputEvent> input;
    struct BindingCapture {
        bool active{}, cancel{}, mouse{};
        uint32_t control{};
        size_t slot{};
        std::string context, label, candidate;
    } capture;
    ULONGLONG last{};
    bool previousText{}, resetInput{};
    struct Field {
        std::vector<char> buffer;
        std::string observed;
        bool secret{}, disabled{true};
        ~Field() { if (secret) { if (!buffer.empty()) SecureZeroMemory(buffer.data(),buffer.size());if (!observed.empty()) SecureZeroMemory(observed.data(),observed.size()); } }
    };
    std::map<std::string,Field> fields;
    std::string fieldContext, clipboard;
    struct NumberField { double observed{}, value{}; };
    std::map<std::string,NumberField> numbers;
    void ClearClipboard() { if (!clipboard.empty()) SecureZeroMemory(clipboard.data(),clipboard.size()); clipboard.clear(); }
    void ClearFields() { fields.clear(); numbers.clear(); fieldContext.clear(); ClearClipboard(); capture={}; }
    ~State() {
        ClearFields();
        if (!context) return;
        ContextScope scope(context);
        if (device) ImGui_ImplDX9_Shutdown();
        ImGui::GetIO().BackendPlatformUserData=nullptr;
        ImGui::GetPlatformIO().Platform_GetClipboardTextFn=nullptr;
        ImGui::GetPlatformIO().Platform_SetClipboardTextFn=nullptr;
        ImGui::DestroyContext(context);
    }
};
MenuRenderer::MenuRenderer() : state_(std::make_unique<State>()) {}
MenuRenderer::~MenuRenderer() = default;
bool MenuRenderer::Prepare(IDirect3DDevice9* device) {
    auto& s = *state_;
    if (!device || device->TestCooperativeLevel() != D3D_OK) return false;
    if (!s.context) {
        const auto previous = ImGui::GetCurrentContext();
        s.context = ImGui::CreateContext();
        ImGui::SetCurrentContext(previous);
        ContextScope scope(s.context);
        auto& io = ImGui::GetIO(); io.IniFilename = nullptr; io.LogFilename = nullptr;
        io.ConfigFlags = ImGuiConfigFlags_NavEnableKeyboard;
        io.BackendPlatformUserData = &s;
        auto& platform = ImGui::GetPlatformIO();
        platform.Platform_GetClipboardTextFn = [](ImGuiContext*) -> const char* {
            auto& state = *static_cast<State*>(ImGui::GetIO().BackendPlatformUserData);
            state.ClearClipboard();
            if (!OpenClipboard(nullptr)) return "";
            const auto data = GetClipboardData(CF_UNICODETEXT);
            const auto text = data ? static_cast<const wchar_t*>(GlobalLock(data)) : nullptr;
            if (text) {
                const int size = WideCharToMultiByte(CP_UTF8,0,text,-1,nullptr,0,nullptr,nullptr);
                if (size>0 && size<=65537) {
                    state.clipboard.resize(static_cast<size_t>(size));
                    WideCharToMultiByte(CP_UTF8,0,text,-1,state.clipboard.data(),size,nullptr,nullptr);
                    state.clipboard.pop_back();
                }
                GlobalUnlock(data);
            }
            CloseClipboard(); return state.clipboard.c_str();
        };
        platform.Platform_SetClipboardTextFn = [](ImGuiContext*,const char* text) {
            const int size=MultiByteToWideChar(CP_UTF8,0,text,-1,nullptr,0);
            if (size<=0 || !OpenClipboard(nullptr)) return;
            const auto data=GlobalAlloc(GMEM_MOVEABLE,static_cast<size_t>(size)*sizeof(wchar_t));
            const auto wide=data ? static_cast<wchar_t*>(GlobalLock(data)) : nullptr;
            if (wide) {
                MultiByteToWideChar(CP_UTF8,0,text,-1,wide,size);GlobalUnlock(data);
                EmptyClipboard();if (!SetClipboardData(CF_UNICODETEXT,data)) GlobalFree(data);
            } else if (data) GlobalFree(data);
            CloseClipboard();
        };
        wchar_t windows[MAX_PATH]{}; GetWindowsDirectoryW(windows, MAX_PATH);
        const auto font = std::filesystem::path(windows) / L"Fonts" / L"segoeui.ttf";
        if (!io.Fonts->AddFontFromFileTTF(font.u8string().c_str(), 24)) io.Fonts->AddFontDefault();
    }
    ContextScope scope(s.context);
    if (s.device != device) {
        if (s.device) ImGui_ImplDX9_Shutdown();
        s.logos.clear(); s.device = nullptr;
        if (!ImGui_ImplDX9_Init(device)) return false;
        s.device = device;
        if (!ImGui_ImplDX9_CreateDeviceObjects()) return false;
        ImGui_ImplDX9_InvalidateDeviceObjects();
    }
    return true;
}
void MenuRenderer::Input(UINT message, WPARAM value, LPARAM data) {
    if (state_->input.size() < 256) state_->input.push_back({message, value, data});
}
bool MenuRenderer::CapturingKey() const { return state_->capture.active; }
void MenuRenderer::ClearInput() { state_->input.clear(); state_->last = 0; state_->resetInput = true; state_->ClearFields(); }
bool MenuRenderer::Render(HWND window, const MenuView& view, std::vector<MenuAction>& actions) {
    auto& s = *state_;
    if (!s.context || !s.device) return false;
    ContextScope scope(s.context);
    ComPtr<IDirect3DSurface9> target;
    D3DSURFACE_DESC surface{};
    if (FAILED(s.device->GetRenderTarget(0, &target)) || FAILED(target->GetDesc(&surface)) || !surface.Width || !surface.Height) return false;
    auto& io = ImGui::GetIO();
    if (s.resetInput) { io.ClearInputKeys(); io.ClearEventsQueue(); s.resetInput = false; }
    const float width = static_cast<float>(surface.Width), height = static_cast<float>(surface.Height);
    const float scale = std::clamp(std::min(width / 1280.0f, height / 900.0f), 0.70f, 1.5f);
    Theme(scale); io.FontGlobalScale = scale * 20.0f / 24.0f;
    io.DisplaySize = {width, height};
    const auto now = GetTickCount64(); io.DeltaTime = s.last ? std::clamp(static_cast<float>(now - s.last) / 1000.0f, 0.001f, 0.1f) : 1.0f / 60.0f; s.last = now;
    POINT cursor{}; RECT client{};
    if (window && GetCursorPos(&cursor) && ScreenToClient(window, &cursor) && GetClientRect(window, &client) && client.right && client.bottom && GetForegroundWindow() == window) {
        io.AddMousePosEvent(static_cast<float>(cursor.x) * width / static_cast<float>(client.right), static_cast<float>(cursor.y) * height / static_cast<float>(client.bottom));
    } else io.AddMousePosEvent(-FLT_MAX, -FLT_MAX);
    for (const auto& event : s.input) {
        if (s.capture.active) {
            if ((event.message==WM_KEYDOWN && event.value==VK_ESCAPE) || event.message==WM_KILLFOCUS ||
                (event.message==WM_ACTIVATEAPP && !event.value)) s.capture.cancel=true;
            if (s.capture.candidate.empty()) {
                s.capture.candidate=BindingKey(event.message,event.value,event.data);
                s.capture.mouse=event.message>=WM_MOUSEFIRST && event.message<=WM_MOUSELAST && event.message!=WM_MOUSEWHEEL;
            }
            if (event.message==WM_KEYDOWN || event.message==WM_KEYUP || event.message==WM_SYSKEYDOWN || event.message==WM_SYSKEYUP || event.message==WM_CHAR) continue;
        }
        if (event.message==WM_MOUSEMOVE && client.right && client.bottom)
            io.AddMousePosEvent(static_cast<float>(GET_X_LPARAM(event.data))*width/static_cast<float>(client.right),static_cast<float>(GET_Y_LPARAM(event.data))*height/static_cast<float>(client.bottom));
        else if (event.message == WM_LBUTTONDOWN || event.message == WM_LBUTTONUP) io.AddMouseButtonEvent(0, event.message == WM_LBUTTONDOWN);
        else if (event.message == WM_MOUSEWHEEL) io.AddMouseWheelEvent(0, static_cast<float>(GET_WHEEL_DELTA_WPARAM(event.value)) / WHEEL_DELTA);
        else if ((!view.wantsText || !view.controls.empty()) && (event.message == WM_KEYDOWN || event.message == WM_KEYUP)) {
            const auto key = Key(event.value); if (key != ImGuiKey_None) io.AddKeyEvent(key, event.message == WM_KEYDOWN);
        } else if (event.message == WM_CHAR && !view.controls.empty()) io.AddInputCharacterUTF16(static_cast<ImWchar16>(event.value));
    }
    s.input.clear();
    // Poll for releases lost during an Alt-Tab. Queued events still preserve quick clicks.
    const bool pressed = window && GetForegroundWindow() == window && (GetAsyncKeyState(VK_LBUTTON) & 0x8000);
    io.AddMouseButtonEvent(0, pressed);
    if (view.wantsText != s.previousText) { io.ClearInputKeys(); s.previousText = view.wantsText; }
    if (!ImGui_ImplDX9_CreateDeviceObjects()) return false;
    ImGui_ImplDX9_NewFrame(); ImGui::NewFrame();
    // Gameplay overlays have no native cursor. Native menus keep their own
    // cursor, avoiding the double pointer that would result from drawing both.
    io.MouseDrawCursor = view.overlay && !view.intro;
    if (view.intro) {
        // Passive overlay: preserve the cinematic, draw no menu/cursor/widgets.
        auto draw=ImGui::GetForegroundDrawList();
        const char* label=view.skipping?"Skipping intro...":"Hold Escape to Skip";
        const auto text=ImGui::CalcTextSize(label);
        const float padding=18*scale,margin=34*scale;
        const ImVec2 min{std::max(margin,width-text.x-padding*2-margin),height-text.y-padding*2-margin-7*scale};
        const ImVec2 max{width-margin,height-margin};
        draw->AddRectFilled(min,max,IM_COL32(10,8,11,205),6*scale);
        draw->AddRect(min,max,IM_COL32(110,56,64,190),6*scale);
        draw->AddText({min.x+padding,min.y+padding},IM_COL32(239,231,232,255),label);
        const ImVec2 start{min.x+padding,max.y-12*scale},end{max.x-padding,max.y-8*scale};
        draw->AddRectFilled(start,end,IM_COL32(69,48,53,255),2*scale);
        const float progress=std::clamp(view.skipProgress,0.0f,1.0f);
        if (progress>0) draw->AddRectFilled(start,{start.x+(end.x-start.x)*progress,end.y},IM_COL32(205,64,79,255),2*scale);
        s.input.clear();io.ClearInputKeys();
        ImGui::Render();ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
        ImGui_ImplDX9_InvalidateDeviceObjects();
        return true;
    }
    auto logoFor = [&](const std::filesystem::path& path) -> Logo* {
        auto logo=s.logos.find(path);
        if (logo==s.logos.end() && s.logos.size()<64) logo=s.logos.emplace(path,LoadLogo(s.device,path)).first;
        return logo==s.logos.end() ? nullptr : &logo->second;
    };
    auto drawControls=[&](bool composer=false) {
            std::string fieldContext=view.pageTitle+view.selected;
            for (const auto& control:view.controls) if (control.kind==CAINE_CONTROL_TAB && (control.flags&CAINE_CONTROL_SELECTED)) fieldContext+=control.label;
            if (s.fieldContext!=fieldContext) { s.ClearFields();s.fieldContext=fieldContext; }
            bool previousTab=false,previousButton=false;
            for (const auto& control:view.controls) {
                const bool reply=control.kind==CAINE_CONTROL_INPUT && (control.flags&CAINE_CONTROL_SUBMIT);
                if (view.overlay && reply!=composer) continue;
                if (view.confirmation && previousButton && control.kind==CAINE_CONTROL_BUTTON) ImGui::SameLine();
                ImGui::PushID(static_cast<int>(control.id));
                const bool selected=(control.flags&CAINE_CONTROL_SELECTED)!=0;
                const bool disabled=(control.flags&CAINE_CONTROL_DISABLED)!=0;
                ImGui::BeginDisabled(disabled);
                auto emit=[&](std::string text,double number) { actions.push_back({MenuActionKind::Control,view.selected,control.id,std::move(text),number}); };
                if (control.kind==CAINE_CONTROL_TAB) {
                    const float nextWidth=ImGui::CalcTextSize(control.label.c_str()).x+ImGui::GetStyle().FramePadding.x*2;
                    if (previousTab && ImGui::GetItemRectMax().x+ImGui::GetStyle().ItemSpacing.x+nextWidth<ImGui::GetWindowPos().x+ImGui::GetWindowContentRegionMax().x) ImGui::SameLine();
                    if (selected) ImGui::PushStyleColor(ImGuiCol_Button,ImGui::GetStyleColorVec4(ImGuiCol_ButtonActive));
                    if (ImGui::Button(control.label.c_str())) emit({},0);
                    if (selected) ImGui::PopStyleColor();
                } else if (control.kind==CAINE_CONTROL_HEADING) {
                    ImGui::Spacing();ImGui::Separator();ImGui::TextWrapped("%s",control.label.c_str());
                } else if (control.kind==CAINE_CONTROL_TEXT) ImGui::TextWrapped("%s",control.label.c_str());
                else if (control.kind==CAINE_CONTROL_BUTTON) {
                    const auto width=std::max(40.f,ImGui::GetContentRegionAvail().x);
                    const auto padding=ImGui::GetStyle().FramePadding;
                    const auto text=ImGui::CalcTextSize(control.label.c_str(),nullptr,false,std::max(1.f,width-2*padding.x));
                    const auto position=ImGui::GetCursorScreenPos();
                    if (ImGui::Button("##button",{std::min(width,text.x+2*padding.x),text.y+2*padding.y})) emit({},0);
                    ImGui::GetWindowDrawList()->AddText(nullptr,0,{position.x+padding.x,position.y+padding.y},ImGui::GetColorU32(ImGuiCol_Text),control.label.c_str(),nullptr,std::max(1.f,width-2*padding.x));
                } else if (control.kind==CAINE_CONTROL_CHOICE) {
                    const float wrap=std::max(40.f,ImGui::GetContentRegionAvail().x-16*scale);
                    const auto labelSize=ImGui::CalcTextSize(control.label.c_str(),nullptr,false,wrap);
                    const auto position=ImGui::GetCursorScreenPos();
                    if (ImGui::Selectable("##choice",selected,0,{0,std::max(30*scale,labelSize.y+12*scale)})) emit({},0);
                    ImGui::GetWindowDrawList()->AddText(nullptr,0,{position.x+4*scale,position.y+6*scale},ImGui::GetColorU32(ImGuiCol_Text),control.label.c_str(),nullptr,wrap);
                } else if (control.kind==CAINE_CONTROL_TOGGLE) {
                    bool enabled=control.number!=0;
                    if (ImGui::Checkbox(control.label.c_str(),&enabled)) emit({},enabled?1:0);
                } else if (control.kind==MenuControlDropdown) {
                    ImGui::TextWrapped("%s",control.label.c_str());
                    ImGui::SetNextItemWidth(std::min(360*scale,ImGui::GetContentRegionAvail().x));
                    if (ImGui::BeginCombo("##dropdown",control.text.c_str(),ImGuiComboFlags_HeightLarge)) {
                        for (size_t option=0;option<control.options.size();++option) {
                            ImGui::PushID(static_cast<int>(option));
                            const bool chosen=control.number==static_cast<double>(option);
                            if (ImGui::Selectable(control.options[option].c_str(),chosen))
                                emit(control.options[option],static_cast<double>(option));
                            if (chosen) ImGui::SetItemDefaultFocus();
                            ImGui::PopID();
                        }
                        ImGui::EndCombo();
                    }
                } else if (control.kind==MenuControlBindings) {
                    if (ImGui::BeginTable("##bindings",3,ImGuiTableFlags_SizingStretchProp|ImGuiTableFlags_NoSavedSettings)) {
                        ImGui::TableSetupColumn("Action",0,2.f);
                        ImGui::TableSetupColumn("Primary",0,1.f);
                        ImGui::TableSetupColumn("Alternative",0,1.f);
                        ImGui::TableNextRow();ImGui::TableNextColumn();ImGui::AlignTextToFramePadding();
                        ImGui::TextWrapped("%s",control.label.c_str());
                        for (size_t slot=0;slot<2;++slot) {
                            ImGui::TableNextColumn();ImGui::PushID(static_cast<int>(slot));
                            const auto key=slot<control.options.size()?control.options[slot]:std::string{};
                            const auto label=std::string(slot==0?"Primary: ":"Alternative: ")+(key.empty()?"Unbound":key);
                            if (ImGui::Button(label.c_str(),{ImGui::GetContentRegionAvail().x,0})) {
                                s.capture={true,false,false,control.id,slot,fieldContext,control.label,{}};
                            }
                            ImGui::PopID();
                        }
                        ImGui::EndTable();
                    }
                } else if (control.kind==CAINE_CONTROL_SLIDER) {
                    const auto numberKey=fieldContext+std::to_string(control.id)+control.label;
                    auto [numberEntry,created]=s.numbers.try_emplace(numberKey,State::NumberField{control.number,control.number});
                    auto& retained=numberEntry->second;
                    if (!created && retained.observed!=control.number) retained={control.number,control.number};
                    float number=static_cast<float>(retained.value);
                    ImGui::SetNextItemWidth(std::min(360*scale,ImGui::GetContentRegionAvail().x*.58f));
                    const bool changed=ImGui::SliderFloat(control.label.c_str(),&number,static_cast<float>(control.minimum),static_cast<float>(control.maximum),(control.flags&CAINE_CONTROL_INTEGER)?"%.0f":"%.2f");
                    retained.value=number;
                    if (control.flags&CAINE_CONTROL_LIVE) { if (changed) emit({},number); }
                    else if (ImGui::IsItemDeactivatedAfterEdit()) emit({},number);
                } else if (control.kind==CAINE_CONTROL_INPUT) {
                    auto& field=s.fields[fieldContext+std::to_string(control.id)+control.label];
                    const bool focusReply=view.overlay && reply && !disabled && (field.buffer.empty() || field.disabled);
                    field.disabled=disabled;
                    field.secret=(control.flags&CAINE_CONTROL_SECRET)!=0;
                    const size_t capacity=std::clamp<size_t>(control.maxBytes?control.maxBytes:4096,1,65536)+1;
                    if (field.buffer.size()!=capacity || field.observed!=control.text) {
                        if (field.secret && !field.buffer.empty()) SecureZeroMemory(field.buffer.data(),field.buffer.size());
                        field.buffer.assign(capacity,0);memcpy(field.buffer.data(),control.text.data(),std::min(control.text.size(),capacity-1));field.observed=control.text;
                    }
                    ImGui::TextWrapped("%s",control.label.c_str());
                    ImGui::SetNextItemWidth(std::max(70*scale,ImGui::GetContentRegionAvail().x-100*scale));
                    if (focusReply) ImGui::SetKeyboardFocusHere();
                    const auto flags=field.secret?ImGuiInputTextFlags_Password:ImGuiInputTextFlags_None;
                    const bool changed=ImGui::InputText("##value",field.buffer.data(),field.buffer.size(),flags|ImGuiInputTextFlags_EnterReturnsTrue);
                    const bool edited=ImGui::IsItemEdited();
                    ImGui::SameLine();const bool apply=ImGui::Button((control.flags&CAINE_CONTROL_SUBMIT)?"Send":"Apply");
                    if (changed || apply || ((control.flags&CAINE_CONTROL_LIVE) && edited)) {
                        emit(field.buffer.data(),0);
                        if (control.flags&CAINE_CONTROL_SUBMIT) { field.buffer[0]=0; }
                        if (field.secret) SecureZeroMemory(field.buffer.data(),field.buffer.size());
                    }
                    if (ImGui::IsItemHovered() && !field.secret && strlen(field.buffer.data())>100) ImGui::SetTooltip("%s",field.buffer.data());
                }
                if (!control.hint.empty()) { ImGui::PushStyleColor(ImGuiCol_Text,ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled)); ImGui::TextWrapped("%s",control.hint.c_str()); ImGui::PopStyleColor(); }
                previousTab=control.kind==CAINE_CONTROL_TAB;
                previousButton=control.kind==CAINE_CONTROL_BUTTON;
                ImGui::EndDisabled();ImGui::PopID();
            }
    };
    if (view.home) {
        s.ClearFields();
        auto draw=ImGui::GetBackgroundDrawList();
        draw->AddRectFilled({0,0},{width,height},IM_COL32(0,0,0,255));
        if (auto logo=logoFor(view.background);logo && logo->texture) {
            const float imageWidth=width;
            const float imageHeight=imageWidth*static_cast<float>(logo->height)/static_cast<float>(logo->width);
            const float fit=std::min(1.0f,height*.64f/imageHeight);
            const ImVec2 start{(width-imageWidth*fit)*.5f,height*.50f-imageHeight*fit*.5f};
            draw->AddImage(reinterpret_cast<ImTextureID>(logo->texture.Get()),start,{start.x+imageWidth*fit,start.y+imageHeight*fit});
        }
        const float buttonWidth=std::min(250*scale,width*.22f);
        const float buttonHeight=46*scale;
        const float menuHeight=(buttonHeight+10*scale)*static_cast<float>(view.nativeItems.size()+(view.update.available?1:0))+18*scale;
        const float menuTop=std::clamp(height*.44f-menuHeight*.5f,24*scale,std::max(24*scale,height-menuHeight-32*scale));
        ImGui::SetNextWindowPos({width*.145f,menuTop},ImGuiCond_Always);
        ImGui::SetNextWindowSize({buttonWidth,menuHeight},ImGuiCond_Always);
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,{0,0});
        ImGui::PushStyleVar(ImGuiStyleVar_ButtonTextAlign,{0,0.5f});
        ImGui::PushStyleColor(ImGuiCol_Button,{0,0,0,0});
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered,{0.25f,0.04f,0.07f,0.78f});
        ImGui::PushStyleColor(ImGuiCol_ButtonActive,{0.38f,0.06f,0.10f,0.88f});
        ImGui::Begin("CAINE main menu",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoBackground);
        ImGui::SetWindowFontScale(1.50f);
        for (const auto& item:view.nativeItems) {
            ImGui::PushID(item.id);
            ImGui::BeginDisabled(item.disabled);
            if (ImGui::Button(item.label.c_str(),{buttonWidth,buttonHeight})) actions.push_back({MenuActionKind::Native,{},static_cast<uint32_t>(item.id)});
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        if(view.update.available && ImGui::Button("Update Available",{buttonWidth,buttonHeight}))actions.push_back({MenuActionKind::Update,{}});
        ImGui::End();ImGui::PopStyleColor(3);ImGui::PopStyleVar(2);
        const char* version="PROJECT CAINE 0.3.14";
        const auto size=ImGui::CalcTextSize(version);
        draw->AddText({(width-size.x)/2,height-28*scale},IM_COL32(145,136,141,255),version);
    } else if (view.confirmation) {
        ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},{width,height},IM_COL32(0,0,0,255));
        ImGui::SetNextWindowPos({width*.5f,height*.5f},ImGuiCond_Always,{.5f,.5f});
        ImGui::SetNextWindowSize({std::min(520*scale,width-48*scale),0},ImGuiCond_Always);
        ImGui::Begin("CAINE confirmation",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_AlwaysAutoResize);
        ImGui::SetWindowFontScale(1.15f);ImGui::TextWrapped("%s",view.pageTitle.c_str());ImGui::SetWindowFontScale(1);
        ImGui::Spacing();ImGui::Separator();ImGui::Spacing();drawControls();ImGui::End();
    } else if (!view.pageTitle.empty()) {
        const float margin=24*scale;
        ImVec2 panel{std::min(width-2*margin,1050*scale),std::min(height-2*margin,850*scale)};
        if (view.overlay) {
            panel.x=std::min(width-2*margin,820*scale);
            float content=0;
            for (const auto& control:view.controls) if (control.kind!=CAINE_CONTROL_INPUT)
                content+=ImGui::CalcTextSize(control.label.c_str(),nullptr,false,std::max(40.f,panel.x-2*margin-24*scale)).y+24*scale;
            const float maximum=std::min(height-2*margin,580*scale);
            panel.y=std::clamp(content+200*scale,std::min(310*scale,maximum),maximum);
        }
        ImGui::GetBackgroundDrawList()->AddRectFilled({0,0},{width,height},view.overlay?IM_COL32(0,0,0,100):IM_COL32(0,0,0,255));
        ImGui::SetNextWindowPos({(width-panel.x)/2,(height-panel.y)/2},ImGuiCond_Always);
        ImGui::SetNextWindowSize(panel,ImGuiCond_Always);
        ImGui::Begin("CAINE game menu",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoMove|ImGuiWindowFlags_NoSavedSettings);
        ImGui::SetWindowFontScale(1.3f);ImGui::TextUnformatted(view.pageTitle.c_str());ImGui::SetWindowFontScale(1);
        ImGui::Separator();
        const bool composer=view.overlay && std::any_of(view.controls.begin(),view.controls.end(),[](const auto& control){return control.kind==CAINE_CONTROL_INPUT && (control.flags&CAINE_CONTROL_SUBMIT);});
        ImGui::BeginChild("game-menu-content",{0,std::max(30*scale,ImGui::GetContentRegionAvail().y-(composer?140:52)*scale)},ImGuiChildFlags_NavFlattened);
        drawControls();
        if (!view.message.empty()) { ImGui::Separator();ImGui::TextWrapped("%s",view.message.c_str()); }
        ImGui::EndChild();ImGui::Separator();
        if (composer) { drawControls(true);ImGui::Spacing(); }
        if (ImGui::Button(view.overlay?"End Conversation":"Back")) actions.push_back({MenuActionKind::Close,{}});
        if (view.overlay) { ImGui::SameLine();ImGui::TextDisabled("Esc to close"); }
        ImGui::End();
    } else {
    const ImVec2 margin(24 * scale, 24 * scale);
    const ImVec2 panel(std::min(width - margin.x * 2, 1280 * scale), std::min(height - margin.y * 2, 850 * scale));
    ImGui::GetBackgroundDrawList()->AddRectFilled({0,0}, {width,height}, IM_COL32(0,0,0,255));
    ImGui::SetNextWindowPos({(width-panel.x)/2, (height-panel.y)/2}, ImGuiCond_Always);
    ImGui::SetNextWindowSize(panel, ImGuiCond_Always);
    ImGui::Begin("PROJECT CAINE Mods", nullptr, ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetWindowFontScale(1.30f); ImGui::TextUnformatted("PROJECT CAINE"); ImGui::SetWindowFontScale(1);
    ImGui::SameLine(); ImGui::TextDisabled("  /  MODS");
    ImGui::TextDisabled("Your Bloodlines. Your mods.");
    ImGui::Separator();
    const float footer = 54 * scale;
    const float body = std::max(120 * scale, ImGui::GetContentRegionAvail().y - footer);
    const float listWidth = std::min(300 * scale, ImGui::GetContentRegionAvail().x * 0.30f);
    ImGui::BeginChild("mod-list", {listWidth,body}, ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    ImGui::TextDisabled("INSTALLED  /  %u", static_cast<unsigned>(view.mods.size()));
    ImGui::Separator();
    for (const auto& mod : view.mods) {
        ImGui::PushID(mod.id.c_str());
        const float wrap=std::max(40.f,ImGui::GetContentRegionAvail().x-8*scale);
        const auto labelSize=ImGui::CalcTextSize(mod.name.c_str(),nullptr,false,wrap);
        const auto position=ImGui::GetCursorScreenPos();
        if (ImGui::Selectable("##mod",mod.id==view.selected,0,{0,std::max(34*scale,labelSize.y+12*scale)})) actions.push_back({MenuActionKind::Select,mod.id});
        ImGui::GetWindowDrawList()->AddText(nullptr,0,{position.x,position.y+6*scale},ImGui::GetColorU32(ImGuiCol_Text),mod.name.c_str(),nullptr,wrap);
        ImGui::TextDisabled("%s  |  %s", mod.version.c_str(), mod.active ? "Loaded" : mod.enabled ? "Not loaded" : "Disabled");
        ImGui::Spacing(); ImGui::PopID();
    }
    if (view.mods.empty()) ImGui::TextWrapped("No mods installed. Add a mod folder to the game's mods directory, then restart Bloodlines.");
    ImGui::EndChild(); ImGui::SameLine();
    ImGui::BeginChild("mod-details", {0,body}, ImGuiChildFlags_Borders | ImGuiChildFlags_NavFlattened);
    const auto found = std::find_if(view.mods.begin(), view.mods.end(), [&](const ModInfo& mod) { return mod.id == view.selected; });
    if (found != view.mods.end()) {
        const auto& mod = *found;
        if (!view.configure) {
            auto logo = s.logos.find(mod.logo);
            if (logo == s.logos.end() && s.logos.size() < 64) logo = s.logos.emplace(mod.logo, LoadLogo(s.device, mod.logo)).first;
            if (logo != s.logos.end() && logo->second.texture) {
                const float imageWidth = ImGui::GetContentRegionAvail().x;
                const float imageHeight = imageWidth * static_cast<float>(logo->second.height) / static_cast<float>(logo->second.width);
                const float fit = std::min(1.0f, 260 * scale / imageHeight);
                ImGui::Image(reinterpret_cast<ImTextureID>(logo->second.texture.Get()), {imageWidth * fit, imageHeight * fit});
            }
        }
        ImGui::SetWindowFontScale(1.15f); ImGui::TextWrapped("%s", mod.name.c_str()); ImGui::SetWindowFontScale(1);
        ImGui::TextDisabled("Version %s%s%s", mod.version.c_str(), mod.author.empty() ? "" : "  /  ", mod.author.c_str());
        ImGui::Separator();
        if (view.configure) {
            if (ImGui::Button("Back to mod details")) actions.push_back({MenuActionKind::Details,{}});
            ImGui::TextDisabled("CONFIGURATION");
            if (view.wantsText) ImGui::TextWrapped("Type to edit. Enter saves; Esc cancels. Secret values stay masked.");
            drawControls();
            for (size_t i=0; i<view.rows.size(); ++i) {
                const auto& row = view.rows[i]; ImGui::PushID(static_cast<int>(i));
                if (row.actionable) {
                    // A wrapped button label preserves long paths and model names.
                    const ImVec2 label = ImGui::CalcTextSize(row.label.c_str(), nullptr, false, std::max(40.0f, ImGui::GetContentRegionAvail().x-24*scale));
                    const auto position = ImGui::GetCursorScreenPos();
                    if (ImGui::Button("##setting", {ImGui::GetContentRegionAvail().x, std::max(38*scale,label.y+18*scale)})) actions.push_back({MenuActionKind::Row,{},static_cast<uint32_t>(i)});
                    ImGui::GetWindowDrawList()->AddText(nullptr, 0, {position.x+12*scale,position.y+9*scale}, ImGui::GetColorU32(ImGuiCol_Text), row.label.c_str(), nullptr, ImGui::GetItemRectSize().x-24*scale);
                } else ImGui::TextWrapped("%s", row.label.c_str());
                ImGui::PopID();
            }
        } else {
            ImGui::TextWrapped("%s", mod.description.c_str()); ImGui::Spacing();
            ImGui::TextColored(mod.active ? ImVec4(0.46f,0.82f,0.61f,1) : ImVec4(0.85f,0.63f,0.40f,1), "%s", mod.state.c_str());
            bool enabled = mod.enabled; ImGui::BeginDisabled(mod.config.empty());
            if (ImGui::Checkbox("Enable on next launch", &enabled)) actions.push_back({MenuActionKind::Toggle,mod.id,enabled ? 1u : 0u});
            ImGui::EndDisabled();
            ImGui::TextDisabled("Changes to enabled mods take effect after restarting.");
            ImGui::Spacing(); ImGui::BeginDisabled(!mod.active);
            if (ImGui::Button("Configure mod", {180*scale,0})) actions.push_back({MenuActionKind::Configure,mod.id});
            ImGui::EndDisabled();
            if (!mod.active) ImGui::TextWrapped("Enable this mod and restart to access its configuration.");
        }
        if (!view.message.empty()) { ImGui::Separator(); ImGui::TextWrapped("%s", view.message.c_str()); }
    } else ImGui::TextWrapped("Select a mod to view its details and settings.");
    ImGui::EndChild(); ImGui::Separator();
    if (ImGui::Button("Back to main menu")) actions.push_back({MenuActionKind::Close,{}});
    ImGui::SameLine(); ImGui::TextDisabled("  ESC  /  Close     |     CAINE 0.3.14");
    ImGui::End();
    }
    if(view.updateOpen) {
        const char* popup="PROJECT CAINE Update";
        if(!ImGui::IsPopupOpen(popup))ImGui::OpenPopup(popup);
        ImGui::SetNextWindowPos({width*.5f,height*.5f},ImGuiCond_Always,{.5f,.5f});
        ImGui::SetNextWindowSize({std::min(600*scale,width-48*scale),0},ImGuiCond_Always);
        if(ImGui::BeginPopupModal(popup,nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)) {
            ImGui::TextWrapped("CAINE %s",view.update.version.c_str());ImGui::Separator();
            ImGui::TextWrapped("%s",view.update.message.c_str());
            if(view.update.Busy()) {
                ImGui::PushStyleColor(ImGuiCol_PlotHistogram,{.65f,.12f,.16f,1});
                ImGui::ProgressBar(std::clamp(view.update.progress,0.0f,1.0f),{-1,0});ImGui::PopStyleColor();
                ImGui::TextWrapped("Installation progress continues in the CAINE updater window after Bloodlines closes. The game will restart with its original launch options.");
            } else {
                ImGui::TextWrapped("Install this update and restart Bloodlines? Unsaved progress will be lost. Your saves, configuration, and other mods are retained.");
                if(ImGui::Button(view.update.phase==UpdatePhase::Error?"Retry update":"Install and Restart"))actions.push_back({MenuActionKind::UpdateInstall,{}});
                ImGui::SameLine();
                if(ImGui::Button("Later")){actions.push_back({MenuActionKind::UpdateClose,{}});ImGui::CloseCurrentPopup();}
            }
            ImGui::EndPopup();
        }
    }
    if (s.capture.active) {
        const auto found=std::find_if(view.controls.begin(),view.controls.end(),[&](const auto& control){return control.id==s.capture.control && control.kind==MenuControlBindings && control.label==s.capture.label;});
        if (found==view.controls.end() || s.capture.context!=s.fieldContext) s.capture={};
        else {
            const char* popup="Press the new key###CAINE key binding";
            if (!ImGui::IsPopupOpen(popup)) ImGui::OpenPopup(popup);
            ImGui::SetNextWindowPos({width*.5f,height*.5f},ImGuiCond_Always,{.5f,.5f});
            ImGui::SetNextWindowSize({std::min(540*scale,width-48*scale),0},ImGuiCond_Always);
            if (ImGui::BeginPopupModal(popup,nullptr,ImGuiWindowFlags_AlwaysAutoResize|ImGuiWindowFlags_NoSavedSettings)) {
                ImGui::TextWrapped("%s binding for %s",s.capture.slot==0?"Primary":"Alternative",s.capture.label.c_str());
                ImGui::Spacing();ImGui::TextWrapped("Press a keyboard key, mouse button, or scroll the mouse wheel.");
                ImGui::TextDisabled("Escape cancels. System shortcuts remain available.");
                ImGui::Separator();
                const bool cancel=ImGui::Button("Cancel");bool footerHovered=ImGui::IsItemHovered();
                ImGui::SameLine();const bool clear=ImGui::Button("Clear binding");footerHovered|=ImGui::IsItemHovered();
                const bool assign=!s.capture.candidate.empty() && !(s.capture.mouse && footerHovered);
                if (cancel || s.capture.cancel || clear || assign) {
                    if (!cancel && !s.capture.cancel && (clear || assign))
                        actions.push_back({MenuActionKind::Control,view.selected,s.capture.control,clear?std::string{}:s.capture.candidate,static_cast<double>(s.capture.slot)});
                    s.capture={};io.ClearInputKeys();io.ClearEventsQueue();ImGui::CloseCurrentPopup();
                } else s.capture.candidate.clear();
                ImGui::EndPopup();
            }
        }
    }
    if (!s.capture.active && ImGui::IsPopupOpen("Press the new key###CAINE key binding")) {
        if (ImGui::BeginPopupModal("Press the new key###CAINE key binding",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
            ImGui::CloseCurrentPopup();ImGui::EndPopup();
        }
    }
    s.ClearClipboard();
    ImGui::Render();
    ImGui_ImplDX9_RenderDrawData(ImGui::GetDrawData());
    // The renderer owns default-pool buffers only inside this frame. Bloodlines
    // can Reset/recreate its device on any of its legacy paths without extra
    // reset hooks or outstanding UI resources. Managed logo textures survive Reset.
    ImGui_ImplDX9_InvalidateDeviceObjects();
    return true;
}
}
