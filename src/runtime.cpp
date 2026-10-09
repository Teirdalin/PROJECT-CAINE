#include <caine/core.hpp>
#include <caine/native_bridge.hpp>
#include <caine/python_bridge.hpp>
#include <caine/mod_loader.hpp>
#include <caine/mod_api.h>
#include <caine/menu_view.hpp>
#include <caine/python_save.hpp>
#include <caine/crash.hpp>
#include <caine/intro_skip.hpp>
#include <caine/startup_videos.hpp>
#include <caine/update.hpp>
#include <shellapi.h>
#include <array>
#include <fstream>
#include <set>
#include <sstream>
#include <mutex>

namespace {
HMODULE self{};
volatile LONG status = 0; // 0 dormant, 1 initializing, 2 observing, 3 config-disabled, -1 failed
std::filesystem::path logFile;
std::mutex logMutex;

std::wstring Environment(const wchar_t* name) {
    const auto count = GetEnvironmentVariableW(name, nullptr, 0);
    if (!count) return {};
    std::wstring value(count, L'\0');
    const auto written = GetEnvironmentVariableW(name, value.data(), count);
    if (!written || written >= count) return {};
    value.resize(written);
    return value;
}
void Log(const std::string& message) {
    caine::CrashBreadcrumb(message.c_str());
    std::lock_guard<std::mutex> lock(logMutex);
    SYSTEMTIME now{}; GetLocalTime(&now);
    char timestamp[64]{};
    sprintf_s(timestamp, "%04u-%02u-%02u %02u:%02u:%02u", now.wYear, now.wMonth, now.wDay, now.wHour, now.wMinute, now.wSecond);
    std::ofstream output(logFile, std::ios::app);
    if (!output) throw std::runtime_error("cannot write CAINE log");
    output << timestamp << " thread=" << GetCurrentThreadId() << " " << message << '\n';
    OutputDebugStringA(("CAINE: " + message + "\n").c_str());
}
DWORD WINAPI Bootstrap(void*) {
    InterlockedExchange(&status, 1);
    try {
        // A native ASI has process lifetime. No hot unload, detach cleanup, or background game API calls.
        HMODULE pinned{};
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN,
                               reinterpret_cast<LPCWSTR>(&Bootstrap), &pinned))
            throw std::runtime_error("cannot pin CAINE runtime");
        const auto exe = caine::ModulePath(nullptr);
        if (_wcsicmp(exe.filename().c_str(), L"Vampire.exe") != 0) {
            InterlockedExchange(&status, -1); return 0;
        }
        const auto config = caine::ModulePath(self).parent_path() / L"CAINE" / L"CAINE.ini";
        if (!GetPrivateProfileIntW(L"Runtime", L"Enabled", 1, config.c_str())) {
            InterlockedExchange(&status, 3); return 0;
        }
        auto logRoot = Environment(L"CAINE_LOG_DIR");
        if (logRoot.empty()) {
            const auto local = Environment(L"LOCALAPPDATA");
            if (local.empty()) throw std::runtime_error("LOCALAPPDATA is unavailable");
            logRoot = (std::filesystem::path(local) / L"PROJECT CAINE" / L"Bloodlines" / L"logs").wstring();
        }
        std::filesystem::create_directories(logRoot);
        logFile = std::filesystem::path(logRoot) / (L"CAINE-" + std::to_wstring(GetCurrentProcessId()) + L".log");
        if (GetPrivateProfileIntW(L"Crash", L"Enabled", 1, config.c_str())) {
            const auto helper = config.parent_path() / L"CrashReporter.exe";
            const auto reports = Environment(L"CAINE_LOG_DIR").empty() ? std::filesystem::path(logRoot).parent_path() / L"crashes" : std::filesystem::path(logRoot) / L"crashes";
            if (caine::InstallCrashReporting(helper, reports, logFile))
                Log("CAINE_CRASH_REPORTER_READY: external x86 reporter; exception context, stacks, modules, recent activity and minidumps; first-chance candidates preserve native handling");
            else Log("CAINE_CRASH_REPORTER_UNAVAILABLE: helper missing or initialization failed; native crash handling retained");
        }
        Log("PROJECT CAINE 0.3.9 x86 starting; native loader route; mod API v1");
        caine::InitializeUpdates(exe.parent_path(),config,Log);
        Log("Executable SHA256=" + caine::Sha256(exe));
        const bool skipStartup=GetPrivateProfileIntW(L"Startup",L"SkipVideos",1,config.c_str())!=0;
        bool startupAttempted=false;
        if (skipStartup && GetModuleHandleW(L"engine.dll")) {
            startupAttempted=true;caine::InstallStartupVideoSkip(GetModuleHandleW(L"engine.dll"),Log);
        }
        caine::LoadMods(exe.parent_path() / L"mods", exe.parent_path(), Log);
        const std::array<const wchar_t*, 7> names{L"Loader.dll", L"engine.dll", L"client.dll", L"vampire.dll",
                                                  L"vampire_python21.dll", L"d3d9.dll", L"d3d8to9.dll"};
        std::set<HMODULE> observed;
        bool ready = false, menuAttempted = false, rendererAttempted = false, stringsAttempted = false, introAttempted = false;
        const bool modern=GetPrivateProfileIntW(L"Menu",L"Modern",1,config.c_str())!=0;
        const bool intro=GetPrivateProfileIntW(L"Intro",L"Enabled",1,config.c_str())!=0;
        caine::ConfigureMenuRenderer(modern);
        const auto started = GetTickCount64();
        bool warned = false;
        for (;;) {
            if (skipStartup && !startupAttempted && GetModuleHandleW(L"engine.dll")) {
                startupAttempted=true;caine::InstallStartupVideoSkip(GetModuleHandleW(L"engine.dll"),Log);
            }
            for (const auto name : names) {
                HMODULE module{};
                if (!GetModuleHandleExW(0, name, &module)) continue;
                struct ModuleReference { HMODULE value; ~ModuleReference() { FreeLibrary(value); } } reference{module};
                if (observed.count(module)) continue;
                const auto info = caine::Module::Inspect(module);
                std::ostringstream line;
                line << "Module=" << info.path.filename().string() << " base=" << static_cast<void*>(info.base)
                     << " image_size=" << info.size << " sha256=" << info.sha256
                     << " CreateInterface=" << (GetProcAddress(module, "CreateInterface") ? "present" : "absent");
                Log(line.str());
                observed.insert(module);
                // Do not assume modern Source SDK interface layouts or Python 3 compatibility.
                // Feature-specific callbacks belong on a verified game-thread hook, added later.
            }
            if (!ready && GetModuleHandleW(L"engine.dll")) {
                Log("CAINE_READY: native runtime observing engine; feature readiness reported separately");
                InterlockedExchange(&status, 2);
                ready = true;
            }
            if (!stringsAttempted) {
                const auto game=GetModuleHandleW(L"vampire.dll"), python=GetModuleHandleW(L"vampire_python21.dll");
                if (game && python) {
                    stringsAttempted=true;
                    caine::InstallLongStringReader(game,python,Log);
                }
            }
            caine::TickMods();
            if (GetModuleHandleW(L"vampire.dll") && GetModuleHandleW(L"client.dll")) { caine::InstallNativeBridge(Log);caine::InitializePythonBridge(Log); }
            if (!introAttempted && GetModuleHandleW(L"vampire.dll") && GetModuleHandleW(L"engine.dll") && GetModuleHandleW(L"client.dll")) {
                introAttempted=true;
                caine::InitializeIntroSkip(config,Log);
            }
            if (!menuAttempted && GetModuleHandleW(L"client.dll")) {
                menuAttempted = true;
                if (!caine::InstallModsMenu(Log)) Log("CAINE_MODS_MENU_UNAVAILABLE: unsupported or conflicting client profile");
            }
            if (menuAttempted && !rendererAttempted && GetModuleHandleW(L"shaderapidx9.dll")) {
                rendererAttempted = true;
                if (modern || intro) caine::InstallMenuRenderer(Log);
            }
            if (!ready && !warned && GetTickCount64() - started > 60000) {
                Log("Engine not observed after 60 seconds; continuing observation without applying patches");
                warned = true;
            }
            Sleep(250);
        }
    } catch (const std::exception& error) {
        InterlockedExchange(&status, -1);
        OutputDebugStringA(error.what());
        if (!logFile.empty()) { try { Log(std::string("CAINE_FAILED: ") + error.what()); } catch (...) {} }
    } catch (...) {
        InterlockedExchange(&status, -1);
    }
    return 0;
}
}
extern "C" LONG WINAPI CaineGetStatus() { return InterlockedCompareExchange(&status, 0, 0); }
extern "C" BOOL WINAPI CaineLongStringsReady() { return caine::LongStringReaderReady() ? TRUE : FALSE; }
extern "C" DWORD WINAPI CaineVersion() { return CAINE_FRAMEWORK_VERSION; }
BOOL WINAPI DllMain(HINSTANCE instance, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        self = instance;
        // Native loader starts CAINE for every normal launch. Emergency opt-out is optional.
        // The worker cannot execute its entry point until DLL initialization has completed.
        // Never wait for it here: loader-lock reentrancy would deadlock.
        wchar_t disabled[2]{};
        if (GetEnvironmentVariableW(L"KAIN_DISABLED", disabled, 2) == 1 && disabled[0] == L'1') return TRUE;
        if (!(GetEnvironmentVariableW(L"CAINE_DISABLED", disabled, 2) == 1 && disabled[0] == L'1')) {
            const auto thread = CreateThread(nullptr, 0, Bootstrap, nullptr, 0, nullptr);
            if (thread) CloseHandle(thread);
            else InterlockedExchange(&status, -1);
        }
    }
    return TRUE;
}
