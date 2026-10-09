#include <caine/menu_view.hpp>
#include <caine/game_menu.hpp>
#include <caine/preferences.hpp>
#include <caine/window_input.hpp>
#include <wincodec.h>
#include <wrl/client.h>
#include <iostream>
#include <stdexcept>
using Microsoft::WRL::ComPtr;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
void Save(IDirect3DDevice9* device, const wchar_t* path) {
    ComPtr<IDirect3DSurface9> back, copy; D3DSURFACE_DESC desc{};
    Check(SUCCEEDED(device->GetRenderTarget(0, &back)), "render target"); back->GetDesc(&desc);
    Check(SUCCEEDED(device->CreateOffscreenPlainSurface(desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &copy, nullptr)), "readback surface");
    Check(SUCCEEDED(device->GetRenderTargetData(back.Get(), copy.Get())), "readback");
    ComPtr<IWICImagingFactory> factory; ComPtr<IWICStream> stream; ComPtr<IWICBitmapEncoder> encoder; ComPtr<IWICBitmapFrameEncode> frame;
    Check(SUCCEEDED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory))), "WIC");
    Check(SUCCEEDED(factory->CreateStream(&stream)) && SUCCEEDED(stream->InitializeFromFilename(path, GENERIC_WRITE)), "PNG stream");
    Check(SUCCEEDED(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder)) && SUCCEEDED(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache)), "PNG encoder");
    Check(SUCCEEDED(encoder->CreateNewFrame(&frame, nullptr)) && SUCCEEDED(frame->Initialize(nullptr)) && SUCCEEDED(frame->SetSize(desc.Width, desc.Height)), "PNG frame");
    WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
    Check(SUCCEEDED(frame->SetPixelFormat(&format)) && IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA), "PNG format");
    D3DLOCKED_RECT rect{}; Check(SUCCEEDED(copy->LockRect(&rect, nullptr, D3DLOCK_READONLY)), "PNG lock");
    std::vector<BYTE> pixels(desc.Width * desc.Height * 4);
    for (UINT y=0; y<desc.Height; ++y) {
        memcpy(pixels.data()+y*desc.Width*4, static_cast<BYTE*>(rect.pBits)+y*rect.Pitch, desc.Width*4);
        for (UINT x=0; x<desc.Width; ++x) pixels[(y*desc.Width+x)*4+3]=255;
    }
    copy->UnlockRect();
    const HRESULT written = frame->WritePixels(desc.Height, desc.Width*4, static_cast<UINT>(pixels.size()), pixels.data());
    Check(SUCCEEDED(written) && SUCCEEDED(frame->Commit()) && SUCCEEDED(encoder->Commit()), "PNG write");
}
int wmain(int argc, wchar_t** argv) {
    try {
        Check(argc >= 3, "usage: caine_menu_preview output.png logo.png [width height] [settings|empty|many]");
        CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        const UINT width = argc > 3 ? static_cast<UINT>(_wtoi(argv[3])) : 1280;
        const UINT height = argc > 4 ? static_cast<UINT>(_wtoi(argv[4])) : 720;
        const std::wstring mode = argc > 5 ? argv[5] : L"details";
        const auto window = CreateWindowExW(0,L"STATIC",L"CAINE renderer test",WS_OVERLAPPEDWINDOW,0,0,static_cast<int>(width),static_cast<int>(height),nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(window != nullptr, "hidden test window");
        ComPtr<IDirect3D9> api; api.Attach(Direct3DCreate9(D3D_SDK_VERSION)); Check(api != nullptr,"Direct3D9");
        D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window;
        pp.BackBufferWidth = width; pp.BackBufferHeight = height; pp.BackBufferFormat = D3DFMT_X8R8G8B8;
        ComPtr<IDirect3DDevice9> device;
        Check(SUCCEEDED(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,D3DCREATE_SOFTWARE_VERTEXPROCESSING,&pp,&device)),"D3D device");
        {
            caine::MenuRenderer renderer;
            Check(renderer.Prepare(device.Get()), "renderer prepare");
            caine::MenuView view;
            caine::ModInfo mod;
            mod.id="unscripted"; mod.name="Bloodlines: Unscripted"; mod.author="PROJECT CAINE";
            mod.version="0.2.2-dev"; mod.state="Loaded"; mod.active=true; mod.enabled=true; mod.config=L"Unscripted.cfg"; mod.logo=argv[2];
            mod.description="NPC personalities, knowledge, memories, Gemini dialogue and xVASynth voices. Development build; gameplay integration is incomplete.";
            if (mode != L"empty") view.mods.push_back(mod);
            view.selected=mod.id;
            if (mode==L"framework") {
                caine::GameMenuBackend backend;
                backend.read=[](const char*) { return std::optional<double>{}; };
                backend.bindings=[] { return std::vector<caine::KeyBinding>{}; };
                const auto temporary=std::filesystem::temp_directory_path()/("CAINE-framework-preview-"+std::to_string(GetCurrentProcessId()));
                std::filesystem::create_directories(temporary);
                caine::GameMenus menus(backend,temporary,temporary/"Vampire");
                menus.Open(caine::GameMenuPage::Settings);menus.Build(view);
                for (const auto& control:view.controls) if (control.label=="Framework") { menus.Action(control.id,{},0);break; }
                menus.Build(view);
                Check(caine::WriteFrameworkOption(temporary/"scale.ini",caine::FrameworkOptions().front(),150),"preview scale preference"); // maximum supported UI scale
            }
            if (mode == L"many") for (int i=0; i<30; ++i) { mod.id="fixture"+std::to_string(i); mod.name="Example mod "+std::to_string(i+1); mod.active=false; view.mods.push_back(mod); }
            if (mode == L"settings") {
                view.configure=true;
                view.rows={{"Gemini configuration",false},{"Model: gemini-2.5-flash",true},{"API keys",true},{"Validate configuration",true},{"Shared usage and request limits",true},{"Voice configuration",true},{"Back",true}};
            }
            if (mode==L"home" || mode==L"update-available") { view.home=true;view.background=argv[2];view.nativeItems={{caine::MenuContinue,"Continue"},{0,"New Game"},{1,"Load Game"},{4,"Settings"},{5,"Mods"},{12,"Credits"},{10,"Quit"}}; }
            if (mode==L"quit") {
                view.pageTitle="Quit to Desktop?";view.confirmation=true;
                view.controls={{CAINE_CONTROL_TEXT,0,0,0,"Any unsaved progress will be lost."},{CAINE_CONTROL_BUTTON,1,0,0,"Quit to Desktop"},{CAINE_CONTROL_BUTTON,2,0,0,"Cancel"}};
            }
            if (mode==L"ambient") {
                view.pageTitle="Bloodlines: Unscripted";view.overlay=true;view.wantsText=true;
                view.controls={{CAINE_CONTROL_HEADING,0,0,0,"Jack Tutorial"},{CAINE_CONTROL_TEXT,10,0,0,"Write what you would like to say below."},{CAINE_CONTROL_INPUT,1,CAINE_CONTROL_SUBMIT,8192,"Your reply"}};
            }
            if (mode==L"update-available")view.update={caine::UpdatePhase::Available,true,"0.3.10-framework-dev","CAINE update available",0};
            if (mode==L"update") {
                view.home=true;view.background=argv[2];view.nativeItems={{0,"New Game"},{1,"Load Game"},{4,"Settings"},{5,"Mods"},{12,"Credits"},{10,"Quit"}};
                view.update={caine::UpdatePhase::Downloading,true,"0.3.10-framework-dev","Downloading CAINE 0.3.10-framework-dev",.57f};view.updateOpen=true;
            }
            if (mode==L"intro") { view.intro=true;view.skipProgress=.6f; }
            if (mode==L"graphics") {
                view.pageTitle="Settings";
                view.controls={{CAINE_CONTROL_TAB,1,0,0,"Audio"},{CAINE_CONTROL_TAB,2,0,0,"Video"},
                    {CAINE_CONTROL_TAB,3,CAINE_CONTROL_SELECTED,0,"Graphics"},
                    {CAINE_CONTROL_SLIDER,4,CAINE_CONTROL_INTEGER|CAINE_CONTROL_LIVE,0,"Field of view",{},"Applies immediately through Bloodlines' fov command and is saved for future loads and restarts.",135,60,135},
                    {CAINE_CONTROL_SLIDER,5,CAINE_CONTROL_INTEGER,0,"Shadow quality",{}, {},3,0,3},
                    {CAINE_CONTROL_BUTTON,6,0,0,"Apply settings"},{CAINE_CONTROL_BUTTON,7,0,0,"Discard pending changes"}};
            }
            if (mode==L"dialogue") {
                view.pageTitle="Bloodlines: Unscripted";view.overlay=true;
                view.controls={{CAINE_CONTROL_HEADING,0,0,0,"Jeanette"},
                    {CAINE_CONTROL_TEXT,10,0,0,"Jeanette: This is a dialogue renderer fixture. It uses the shared CAINE UI over the rendered scene."},
                    {CAINE_CONTROL_INPUT,1,CAINE_CONTROL_SUBMIT,8192,"Your reply","Hello"},
                    {CAINE_CONTROL_HEADING,4,0,0,"Original responses"},
                    {CAINE_CONTROL_BUTTON,100,0,0,"An original response with a longer explanation can wrap across several lines while preserving its complete text and its native choice index. CAINE uses the game's original handler for conditions, costs, quest scripts and transitions."},
                    {CAINE_CONTROL_BUTTON,101,0,0,"Leave"}};
            }
            if (mode==L"dialogue-queue") {
                view.pageTitle="Bloodlines: Unscripted";view.overlay=true;view.wantsText=true;
                view.controls={{CAINE_CONTROL_HEADING,0,0,0,"Jack Tutorial"},{CAINE_CONTROL_TEXT,10,0,0,"Write what you would like to say below."},{CAINE_CONTROL_INPUT,1,CAINE_CONTROL_SUBMIT,8192,"Your reply"}};
            }
            if (mode==L"bindings") {
                view.pageTitle="Settings";
                view.controls={{caine::MenuControlBindings,1,0,0,"Primary attack",{}, {},0,0,0,{"MOUSE1","SPACE"}},
                               {caine::MenuControlBindings,2,0,0,"Move forward",{}, {},0,0,0,{"W","UPARROW"}}};
            }
            if (mode==L"resolution") {
                view.pageTitle="Settings";
                view.controls={{CAINE_CONTROL_TAB,10,0,0,"Audio"},
                               {CAINE_CONTROL_TAB,11,0,0,"Mouse"},
                               {CAINE_CONTROL_TAB,12,0,0,"Keyboard"},
                               {CAINE_CONTROL_TAB,13,0,0,"Gameplay"},
                               {CAINE_CONTROL_TAB,14,CAINE_CONTROL_SELECTED,0,"Video"},
                               {CAINE_CONTROL_TAB,15,0,0,"Visual"},
                               {CAINE_CONTROL_HEADING,16,0,0,"Video"},
                               {CAINE_CONTROL_TOGGLE,17,0,0,"Bump mapping",{}, {},1},
                               {CAINE_CONTROL_SLIDER,18,CAINE_CONTROL_INTEGER,0,"Lighting quality",{}, {},2,0,2},
                               {CAINE_CONTROL_SLIDER,19,CAINE_CONTROL_INTEGER,0,"Image quality",{}, {},4,1,4},
                               {caine::MenuControlDropdown,20,0,0,"Resolution","1920 x 1080 / 32 bit",
                                "Video mode changes use Bloodlines' existing video settings and may require restarting the game.",1,0,0,
                                {"1280 x 720 / 32 bit","1920 x 1080 / 32 bit","2560 x 1440 / 32 bit","3840 x 2160 / 32 bit"}},
                               {CAINE_CONTROL_HEADING,21,0,0,"Apply changes"},
                               {CAINE_CONTROL_BUTTON,22,CAINE_CONTROL_DISABLED,0,"Apply settings"},
                               {CAINE_CONTROL_BUTTON,23,CAINE_CONTROL_DISABLED,0,"Discard pending changes"}};
            }
            if (mode==L"controls") {
                view.pageTitle="Settings";
                view.controls={{CAINE_CONTROL_INPUT,1,0,65536,"Long text","Initial text",{},0,0,0},
                               {CAINE_CONTROL_TOGGLE,2,0,0,"Enable voice acting",{}, {},1,0,1},
                               {CAINE_CONTROL_SLIDER,3,0,0,"Volume",{}, {},.8,0,1},
                               {CAINE_CONTROL_INPUT,4,CAINE_CONTROL_SECRET,512,"API key",{},"Masked and cleared after submission.",0,0,0}};
            }
            std::vector<caine::MenuAction> events;
            caine::WindowInput inputQueue;
            if (mode==L"dialogue-queue") Check(inputQueue.Attach(window,[&](HWND,UINT message,WPARAM value,LPARAM data)->std::optional<LRESULT> {
                if ((message>=WM_MOUSEFIRST && message<=WM_MOUSELAST) || message==WM_KEYDOWN || message==WM_KEYUP || message==WM_CHAR) { renderer.Input(message,value,data);return 0; }
                return {};
            }),"real Windows queue capture");
            for (int phase=0; phase<2; ++phase) {
                for (int frame=0; frame<4; ++frame) {
                    if (mode==L"ambient") renderer.Input(WM_MOUSEMOVE,0,MAKELPARAM(1550,780));
                    device->Clear(0,nullptr,D3DCLEAR_TARGET,D3DCOLOR_XRGB(15,10,13),1,0);
                    device->SetRenderState(D3DRS_FOGENABLE, TRUE); device->SetRenderState(D3DRS_LIGHTING, TRUE);
                    Check(SUCCEEDED(device->BeginScene()),"BeginScene");
                    Check(renderer.Render(mode==L"ambient"?window:nullptr,view,events),"UI render");
                    DWORD fog{}, lighting{}; device->GetRenderState(D3DRS_FOGENABLE,&fog); device->GetRenderState(D3DRS_LIGHTING,&lighting);
                    Check(fog == TRUE && lighting == TRUE,"UI leaked render state");
                    Check(SUCCEEDED(device->EndScene()),"EndScene");
                    if (mode==L"ambient" && width==1920 && height==1080) {
                        ComPtr<IDirect3DSurface9> target,copy;D3DSURFACE_DESC desc{};
                        device->GetRenderTarget(0,&target);target->GetDesc(&desc);
                        Check(SUCCEEDED(device->CreateOffscreenPlainSurface(width,height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr)),"cursor surface");
                        Check(SUCCEEDED(device->GetRenderTargetData(target.Get(),copy.Get())),"cursor pixels");
                        D3DLOCKED_RECT rect{};Check(SUCCEEDED(copy->LockRect(&rect,nullptr,D3DLOCK_READONLY)),"cursor lock");
                        bool visible{};
                        for (UINT y=760;y<900;++y) for (UINT x=1500;x<1700;++x) {
                            const auto pixel=reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(rect.pBits)+y*rect.Pitch)[x];
                            visible|=(pixel&0xffffff)==0xffffff;
                        }
                        copy->UnlockRect();Check(visible,"gameplay overlay software cursor did not render");
                    }
                    if (mode==L"intro") {
                        ComPtr<IDirect3DSurface9> target,copy;D3DSURFACE_DESC desc{};
                        device->GetRenderTarget(0,&target);target->GetDesc(&desc);
                        Check(SUCCEEDED(device->CreateOffscreenPlainSurface(width,height,desc.Format,D3DPOOL_SYSTEMMEM,&copy,nullptr)),"overlay readback");
                        Check(SUCCEEDED(device->GetRenderTargetData(target.Get(),copy.Get())),"overlay readback data");
                        D3DLOCKED_RECT rect{};Check(SUCCEEDED(copy->LockRect(&rect,nullptr,D3DLOCK_READONLY)),"overlay pixels");
                        const auto pixel=reinterpret_cast<const DWORD*>(static_cast<const BYTE*>(rect.pBits)+height/2*rect.Pitch)[width/2]&0xffffff;
                        copy->UnlockRect();Check(pixel==0x0f0a0d,"intro overlay replaced cinematic background");
                    }
                }
                if (phase == 0) Check(SUCCEEDED(device->Reset(&pp)), "UI retained a default-pool resource across Reset");
            }
            Save(device.Get(),argv[1]);
            Check(renderer.FontUploads()==1,"font atlas was uploaded again across frames/device Reset");
            Check(events.empty(),"UI emitted an action without input");
            if (mode==L"dialogue-queue") {
                auto frame=[&](UINT message,WPARAM value,LPARAM data=0) {
                    if (message==WM_LBUTTONDOWN || message==WM_LBUTTONUP) Check(PostMessageW(window,WM_MOUSEMOVE,0,data)!=FALSE,"queue pointer position");
                    if (message) Check(PostMessageW(window,message,value,data)!=FALSE,"post owned fixture input");
                    MSG queued{};while (PeekMessageW(&queued,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&queued);DispatchMessageW(&queued); }
                    Check(SUCCEEDED(device->BeginScene()),"queue BeginScene");
                    Check(renderer.Render(window,view,events),"queue render");
                    Check(SUCCEEDED(device->EndScene()),"queue EndScene");
                };
                frame(WM_CHAR,'Z');frame(WM_CHAR,0x00e9);frame(WM_KEYDOWN,VK_RETURN);frame(WM_KEYUP,VK_RETURN);frame(0,0);
                bool sent{};for(const auto& event:events) sent|=event.kind==caine::MenuActionKind::Control && event.value==1 && event.text=="Z\xc3\xa9";
                Check(sent,"real queued Unicode/Enter did not submit free text");
                events.clear();
                RECT client{};Check(GetClientRect(window,&client)!=FALSE,"queue client rectangle");
                // Actual End Conversation button in the compact 1280x720 view.
                const auto point=MAKELPARAM(382*client.right/width,470*client.bottom/height);
                frame(WM_MOUSEMOVE,0,point);frame(WM_LBUTTONDOWN,MK_LBUTTON,point);frame(WM_LBUTTONUP,0,point);frame(0,0);
                bool closed{};for(const auto& event:events) closed|=event.kind==caine::MenuActionKind::Close;
                Check(closed,"real queued mouse click did not close conversation");
                std::cout<<"CAINE_DIALOGUE_QUEUE_OK: Windows queue to actual renderer, Unicode typing, Enter submission and End Conversation click\n";
            }
            if (mode == L"details" || mode==L"home" || mode==L"controls" || mode==L"bindings" || mode==L"dialogue" || mode==L"update-available") {
                auto inputFrame = [&](UINT message, WPARAM value, LPARAM data=0) {
                    renderer.Input(message,value,data);
                    Check(SUCCEEDED(device->BeginScene()),"input BeginScene");
                    Check(renderer.Render(mode==L"controls"?window:nullptr,view,events),"input frame");
                    Check(SUCCEEDED(device->EndScene()),"input EndScene");
                };
                if (mode==L"controls" && width==1280 && height==720) {
                    // Click the visible label, beyond the checkbox's hit area.
                    RECT client{};Check(GetClientRect(window,&client)!=FALSE,"fixture client rectangle");
                    const auto point=MAKELPARAM(330*client.right/width,154*client.bottom/height);
                    auto mouseFrame=[&](UINT message,WPARAM value) {
                        renderer.Input(WM_MOUSEMOVE,0,point);
                        inputFrame(message,value,point);
                    };
                    inputFrame(WM_MOUSEMOVE,0,point);
                    mouseFrame(WM_LBUTTONDOWN,MK_LBUTTON);mouseFrame(WM_LBUTTONUP,0);
                    Check(events.size()==1 && events[0].kind==caine::MenuActionKind::Control && events[0].value==2 && events[0].number==0,"wrapped checkbox label did not toggle");
                    events.clear();view.controls[1].flags=CAINE_CONTROL_DISABLED;
                    mouseFrame(WM_LBUTTONDOWN,MK_LBUTTON);mouseFrame(WM_LBUTTONUP,0);
                    Check(events.empty(),"disabled checkbox label remained interactive");
                    view.controls[1].flags=0;
                }
                if(mode==L"update-available") {
                    for(int i=0;i<14;++i) { inputFrame(WM_KEYDOWN,VK_TAB);inputFrame(WM_KEYUP,VK_TAB);inputFrame(WM_KEYDOWN,VK_SPACE);inputFrame(WM_KEYUP,VK_SPACE); }
                    bool update{};for(const auto& event:events)update|=event.kind==caine::MenuActionKind::Update;
                    Check(update,"bottom Update Available button did not dispatch update action");
                    events.clear();view.updateOpen=true;view.update.phase=caine::UpdatePhase::Downloading;view.update.progress=.5f;
                    inputFrame(WM_KEYDOWN,VK_TAB);inputFrame(WM_KEYUP,VK_TAB);inputFrame(WM_KEYDOWN,VK_SPACE);inputFrame(WM_KEYUP,VK_SPACE);
                    Check(events.empty(),"download modal allowed underlying main-menu activation");
                }
                if (mode==L"bindings") {
                    auto open=[&] {
                        for (int i=0;i<8 && !renderer.CapturingKey();++i) {
                            inputFrame(WM_KEYDOWN,VK_TAB);inputFrame(WM_KEYUP,VK_TAB);
                            inputFrame(WM_KEYDOWN,VK_SPACE);inputFrame(WM_KEYUP,VK_SPACE);
                        }
                        Check(renderer.CapturingKey(),"primary/alternative box did not open capture popup");
                    };
                    auto bindingCount=[&] { size_t count{};for(const auto& event:events) if(event.kind==caine::MenuActionKind::Control)++count;return count; };
                    open();Check(bindingCount()==0,"opening key leaked into binding capture");
                    const auto popup=std::filesystem::path(argv[1]).parent_path()/(std::filesystem::path(argv[1]).stem().wstring()+L"-capture.png");
                    Save(device.Get(),popup.c_str());
                    inputFrame(WM_KEYDOWN,'F',1<<30);Check(renderer.CapturingKey() && bindingCount()==0,"auto-repeat assigned a binding");
                    inputFrame(WM_KEYDOWN,'F');inputFrame(WM_KEYUP,'F');
                    Check(!renderer.CapturingKey() && bindingCount()==1 && events.back().text=="F","fresh key did not submit captured binding");
                    open();const auto count=bindingCount();inputFrame(WM_KEYDOWN,VK_ESCAPE);inputFrame(WM_KEYUP,VK_ESCAPE);
                    Check(!renderer.CapturingKey() && bindingCount()==count,"Escape assigned a binding instead of cancelling");
                    open();inputFrame(WM_KILLFOCUS,0);Check(!renderer.CapturingKey() && bindingCount()==count,"focus loss did not cancel capture");
                    open();inputFrame(WM_MOUSEWHEEL,MAKEWPARAM(0,WHEEL_DELTA));
                    Check(!renderer.CapturingKey() && bindingCount()==count+1 && events.back().text=="MWHEELUP","mouse wheel binding capture");
                    std::cout<<"CAINE_BINDING_CAPTURE_OK: primary/alternative boxes, actual popup, opening-key isolation, fresh key, repeat rejection, Escape, focus loss and mouse wheel\n";
                }
                if (mode!=L"bindings" && mode!=L"update-available") {
                for (int i=0; i<(mode==L"details"?12:10); ++i) {
                    inputFrame(WM_KEYDOWN,VK_TAB); inputFrame(WM_KEYUP,VK_TAB);
                    if (mode==L"controls" || mode==L"dialogue") { inputFrame(WM_CHAR,'Z');inputFrame(WM_CHAR,0x00e9);inputFrame(WM_KEYDOWN,VK_RETURN);inputFrame(WM_KEYUP,VK_RETURN); }
                    else { inputFrame(WM_KEYDOWN,VK_SPACE); inputFrame(WM_KEYUP,VK_SPACE); }
                }
                bool selected=false, toggled=false, configured=false, closed=false;
                for (const auto& event : events) {
                    selected |= event.kind == caine::MenuActionKind::Select && event.id == view.selected;
                    toggled |= event.kind == caine::MenuActionKind::Toggle && event.id == view.selected && event.value == 0;
                    configured |= event.kind == caine::MenuActionKind::Configure && event.id == view.selected;
                    closed |= event.kind == caine::MenuActionKind::Close;
                }
                std::cout << "actions=" << events.size() << " selected=" << selected << " toggled=" << toggled << " configured=" << configured << " closed=" << closed << std::endl;
                if (mode==L"details") Check(selected && toggled && configured && closed,"keyboard navigation/action dispatch");
                if (mode==L"home") {
                    bool native=false;for (const auto& event:events) native|=event.kind==caine::MenuActionKind::Native;
                    Check(native,"main menu native action routing");
                }
                if (mode==L"controls") {
                    bool text=false,secret=false;
                    for (const auto& event:events) if (event.kind==caine::MenuActionKind::Control) {
                        std::cout<<"control="<<event.value<<" bytes="<<event.text.size()<<" number="<<event.number<<std::endl;
                        text|=event.value==1 && event.text.find("Z\xc3\xa9")!=std::string::npos;
                        secret|=event.value==4 && !event.text.empty();
                    }
                    Check(text && secret,"Unicode input and masked field submission");
                }
                if (mode==L"dialogue") {
                    bool sent{};for (const auto& event:events) sent|=event.kind==caine::MenuActionKind::Control && event.value==1 && event.text.find("Z\xc3\xa9")!=std::string::npos;
                    Check(sent,"free-text dialogue submission");
                }
                }
            }
        }
        device.Reset(); api.Reset(); DestroyWindow(window); CoUninitialize();
        std::cout << "CAINE_MENU_RENDER_OK: rendered PNG, game render state restored, device Reset, rerender and keyboard actions passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
