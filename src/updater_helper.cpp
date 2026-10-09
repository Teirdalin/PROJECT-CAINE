#include <caine/update.hpp>
#include <json.hpp>
#include <shellapi.h>
#include <commctrl.h>
#include <fstream>
#include <mutex>
#include <thread>

namespace {
std::mutex mutex;
std::wstring message=L"Preparing CAINE update...";
int percent{};
bool finished{},failed{};
HWND label{},bar{},closeButton{};
struct Handle { HANDLE value{};~Handle(){if(value && value!=INVALID_HANDLE_VALUE)CloseHandle(value);} };
void SetStatus(std::wstring text,int progress,bool done=false,bool error=false) {
    std::lock_guard<std::mutex> lock(mutex);message=std::move(text);percent=progress;finished=done;failed=error;
}
nlohmann::json Read(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>2*1024*1024)throw std::runtime_error("Oversized updater metadata");
    std::ifstream input(path,std::ios::binary);return nlohmann::json::parse(input);
}
void SafePath(const std::filesystem::path& path) {
    if(!path.is_absolute())throw std::runtime_error("Updater paths must be absolute");
    for(auto p=path;!p.empty() && p!=p.root_path();p=p.parent_path()) {
        const auto attrs=GetFileAttributesW(p.c_str());
        if(attrs!=INVALID_FILE_ATTRIBUTES && (attrs&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Refusing redirected updater path");
    }
}
void Run(const std::filesystem::path& requestPath) {
    try {
        SafePath(requestPath);
        const auto request=Read(requestPath);
        if(request.at("schema").get<int>()!=1)throw std::runtime_error("Unsupported update request");
        const auto root=std::filesystem::path(caine::UpdateWide(request.at("gameRoot").get<std::string>()));
        const auto exe=root/L"Vampire.exe";
        const auto zip=std::filesystem::path(caine::UpdateWide(request.at("zip").get<std::string>()));
        SafePath(root);SafePath(zip);SafePath(exe);
        if(zip.parent_path()!=requestPath.parent_path() || caine::ModulePath(nullptr).parent_path()!=requestPath.parent_path())
            throw std::runtime_error("Update helper and package must share their private staging directory");
        if(caine::Sha256(zip)!=request.at("sha256").get<std::string>())throw std::runtime_error("Staged package integrity failed");
        auto command=caine::UpdateWide(request.at("commandLine").get<std::string>());
        if(command.size()>32760)throw std::runtime_error("Game command line is too long");
        int argc{};auto argv=CommandLineToArgvW(command.c_str(),&argc);
        const bool valid=argv && argc>0 && _wcsicmp(std::filesystem::path(argv[0]).filename().c_str(),L"Vampire.exe")==0;
        if(argv)LocalFree(argv);
        if(!valid)throw std::runtime_error("Restart executable differs from Bloodlines");
        const auto cwd=std::filesystem::path(caine::UpdateWide(request.at("workingDirectory").get<std::string>()));
        if(!cwd.is_absolute() || !std::filesystem::is_directory(cwd))throw std::runtime_error("Original game working directory is unavailable");
        Handle game{OpenProcess(SYNCHRONIZE|PROCESS_QUERY_LIMITED_INFORMATION,FALSE,request.at("gamePid").get<DWORD>())};
        if(!game.value)throw std::runtime_error("Cannot identify the requesting Bloodlines process");
        wchar_t actual[32768]{};DWORD count=32768;
        FILETIME created{},exited{},kernel{},user{};
        if(!QueryFullProcessImageNameW(game.value,0,actual,&count) || _wcsicmp(actual,exe.c_str())!=0 ||
           !GetProcessTimes(game.value,&created,&exited,&kernel,&user) ||
           ((static_cast<uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime)!=request.at("creationTime").get<uint64_t>())
            throw std::runtime_error("Requesting game identity changed; installation cancelled");
        const auto eventName=caine::UpdateWide(request.at("readyEvent").get<std::string>());
        if(eventName.rfind(L"Local\\CAINE.Update.",0)!=0)throw std::runtime_error("Invalid updater handshake");
        Handle ready{OpenEventW(EVENT_MODIFY_STATE,FALSE,eventName.c_str())};
        if(!ready.value)throw std::runtime_error("Requesting game is no longer waiting for the updater");
        SetStatus(L"Download verified. Waiting for Bloodlines to close...",5);
        if(!SetEvent(ready.value))throw std::runtime_error("Cannot confirm updater readiness");
        // Never kill the game or wait for a recycled PID. This handle is the exact original process.
        if(WaitForSingleObject(game.value,120000)!=WAIT_OBJECT_0)throw std::runtime_error("Bloodlines did not close within two minutes. Nothing was installed.");
        SetStatus(L"Installing PROJECT CAINE...",10);
        wchar_t system[32768]{};if(!GetSystemDirectoryW(system,32768))throw std::runtime_error("Cannot locate Windows PowerShell");
        const auto powershell=std::filesystem::path(system)/L"WindowsPowerShell"/L"v1.0"/L"powershell.exe";
        auto scriptCommand=caine::QuoteUpdateArgument(powershell.wstring())+L" -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -File "+
            caine::QuoteUpdateArgument((requestPath.parent_path()/L"ApplyUpdate.ps1").wstring())+L" -Request "+caine::QuoteUpdateArgument(requestPath.wstring());
        STARTUPINFOW startup{};startup.cb=sizeof(startup);startup.dwFlags=STARTF_USESHOWWINDOW;startup.wShowWindow=SW_HIDE;
        PROCESS_INFORMATION child{};
        if(!CreateProcessW(powershell.c_str(),scriptCommand.data(),nullptr,nullptr,FALSE,CREATE_NO_WINDOW,nullptr,requestPath.parent_path().c_str(),&startup,&child))
            throw std::runtime_error("Cannot start CAINE installation transaction");
        CloseHandle(child.hThread);Handle installer{child.hProcess};
        const auto progressPath=requestPath.parent_path()/L"progress.json";
        while(WaitForSingleObject(installer.value,250)==WAIT_TIMEOUT) {
            try{const auto status=Read(progressPath);SetStatus(caine::UpdateWide(status.at("message").get<std::string>()),status.at("progress").get<int>());}catch(...){}
        }
        DWORD result{};if(!GetExitCodeProcess(installer.value,&result) || result!=0) {
            std::string detail="Installation failed. Previous CAINE files were restored; Bloodlines was not restarted.";
            try{detail=Read(progressPath).at("message").get<std::string>();}catch(...){}
            throw std::runtime_error(detail);
        }
        SetStatus(L"CAINE installed. Restarting Bloodlines with its original launch options...",98);
        startup.dwFlags=0;
        PROCESS_INFORMATION restarted{};
        if(!CreateProcessW(exe.c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,cwd.c_str(),&startup,&restarted))
            throw std::runtime_error("CAINE installed successfully, but Bloodlines could not restart. Start it with your usual launcher.");
        CloseHandle(restarted.hThread);CloseHandle(restarted.hProcess);
        SetStatus(L"CAINE updated. Bloodlines has restarted. Saves and settings were retained.",100,true);
    }catch(const std::exception& error){SetStatus(caine::UpdateWide(error.what()),0,true,true);}
}
LRESULT CALLBACK WindowProc(HWND window,UINT msg,WPARAM w,LPARAM l) {
    if(msg==WM_CREATE) {
        const auto font=GetStockObject(DEFAULT_GUI_FONT);
        label=CreateWindowW(L"STATIC",message.c_str(),WS_CHILD|WS_VISIBLE,24,24,510,72,window,nullptr,nullptr,nullptr);
        bar=CreateWindowW(PROGRESS_CLASSW,L"",WS_CHILD|WS_VISIBLE,24,108,510,24,window,nullptr,nullptr,nullptr);
        closeButton=CreateWindowW(L"BUTTON",L"Close",WS_CHILD|WS_VISIBLE|WS_DISABLED,434,154,100,30,window,reinterpret_cast<HMENU>(1),nullptr,nullptr);
        SendMessageW(label,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);
        SendMessageW(closeButton,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);SetTimer(window,1,250,nullptr);return 0;
    }
    if(msg==WM_TIMER) {
        std::lock_guard<std::mutex> lock(mutex);
        SetWindowTextW(label,message.c_str());SendMessageW(bar,PBM_SETPOS,static_cast<WPARAM>(percent),0);
        EnableWindow(closeButton,finished);
        static ULONGLONG completed{};
        if(finished && !failed){if(!completed)completed=GetTickCount64();if(GetTickCount64()-completed>3000)PostMessageW(window,WM_CLOSE,0,0);}
        return 0;
    }
    if(msg==WM_CLOSE || (msg==WM_COMMAND && LOWORD(w)==1)) {
        std::lock_guard<std::mutex> lock(mutex);if(finished)DestroyWindow(window);return 0;
    }
    if(msg==WM_DESTROY){PostQuitMessage(0);return 0;}
    return DefWindowProcW(window,msg,w,l);
}
}
int WINAPI wWinMain(HINSTANCE instance,HINSTANCE,PWSTR,int) {
    int argc{};auto args=CommandLineToArgvW(GetCommandLineW(),&argc);
    if(!args || argc!=3 || std::wstring(args[1])!=L"--request"){if(args)LocalFree(args);return 2;}
    const std::filesystem::path request=args[2];LocalFree(args);
    INITCOMMONCONTROLSEX controls{sizeof(controls),ICC_PROGRESS_CLASS};InitCommonControlsEx(&controls);
    WNDCLASSW cls{};cls.lpfnWndProc=WindowProc;cls.hInstance=instance;cls.lpszClassName=L"CAINE Update";
    cls.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);cls.hCursor=LoadCursorW(nullptr,IDC_ARROW);RegisterClassW(&cls);
    const int x=(GetSystemMetrics(SM_CXSCREEN)-570)/2,y=(GetSystemMetrics(SM_CYSCREEN)-240)/2;
    const auto window=CreateWindowW(cls.lpszClassName,L"PROJECT CAINE â€” Update",WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,x,y,570,240,nullptr,nullptr,instance,nullptr);
    if(!window)return 3;ShowWindow(window,SW_SHOWNORMAL);UpdateWindow(window);
    std::thread worker(Run,request);
    MSG msg{};while(GetMessageW(&msg,nullptr,0,0)>0){TranslateMessage(&msg);DispatchMessageW(&msg);}worker.join();
    return failed?1:0;
}
