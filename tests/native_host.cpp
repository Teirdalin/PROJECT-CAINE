#include <chrono>
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
int wmain(int argc, wchar_t** argv) {
    if (argc != 4) return 1;
    const bool configDisabled = std::wstring(argv[3]) == L"config-disabled";
    const bool crashDisabled = std::wstring(argv[3]) == L"crash-disabled";
    const bool active = std::wstring(argv[3]) == L"active" || configDisabled || crashDisabled;
    const auto logs = std::filesystem::temp_directory_path() / (L"caine-native-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
    SetEnvironmentVariableW(L"CAINE_DISABLED", active ? nullptr : L"1");
    SetEnvironmentVariableW(L"CAINE_LOG_DIR", logs.c_str());
    if (active && !configDisabled) {
        std::filesystem::create_directories(logs);
        std::ofstream(logs/L"CAINE.log")<<"PREVIOUS_LAUNCH_SENTINEL\n";
        std::ofstream(logs/(L"CAINE-"+std::to_wstring(GetCurrentProcessId())+L".log"))<<"REUSED_PID_SENTINEL\n";
    }
    if (!LoadLibraryW(argv[2])) return 2;
    std::filesystem::path pluginPath = argv[1];
    if (configDisabled || crashDisabled) {
        const auto configRoot = pluginPath.parent_path() / (L"config-test-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        std::filesystem::create_directories(configRoot / L"CAINE");
        std::filesystem::copy_file(pluginPath, configRoot / L"CAINE.asi");
        { std::ofstream config(configRoot / L"CAINE" / L"CAINE.ini"); config << (configDisabled ? "[Runtime]\nEnabled=0\n" : "[Runtime]\nEnabled=1\n[Crash]\nEnabled=0\n"); }
        pluginPath = configRoot / L"CAINE.asi";
    }
    auto plugin = LoadLibraryW(pluginPath.c_str());
    if (!plugin) return 3;
    auto getStatus = reinterpret_cast<LONG(WINAPI*)()>(GetProcAddress(plugin, "CaineGetStatus"));
    if (!getStatus) return 4;
    if (!active) {
        Sleep(100);
        if (getStatus() != 0 || std::filesystem::exists(logs)) return 5;
        FreeLibrary(plugin);
        std::cout << "CAINE_NATIVE_INACTIVE_OK\n";
        return 0;
    }
    const LONG expected = configDisabled ? 3 : 2;
    for (int i = 0; i < 100 && getStatus() != expected && getStatus() != -1; ++i) Sleep(100);
    if (configDisabled) {
        if (getStatus() != 3 || std::filesystem::exists(logs)) return 8;
        std::cout << "CAINE_CONFIG_DISABLED_OK\n";
        return 0;
    }
    if (getStatus() != 2) { std::cerr << "runtime status=" << getStatus() << '\n'; return 6; }
    const auto log = logs / (L"CAINE-" + std::to_wstring(GetCurrentProcessId()) + L".log");
    std::ifstream input(log);
    const std::string content((std::istreambuf_iterator<char>(input)), {});
    input.close();
    if (content.find("CAINE_READY") == std::string::npos || content.find("Module=engine.dll") == std::string::npos) return 7;
    const auto current=logs/L"CAINE.log";
    std::ifstream currentInput(current);const std::string currentContent((std::istreambuf_iterator<char>(currentInput)),{});
    if (currentContent.find("CAINE_READY")==std::string::npos || currentContent.find("PREVIOUS_LAUNCH_SENTINEL")!=std::string::npos || content.find("REUSED_PID_SENTINEL")!=std::string::npos || content.find("CAINE_LOG_START")==std::string::npos) return 10;
    if ((content.find("CAINE_CRASH_REPORTER_READY") != std::string::npos) == crashDisabled) return 9;
    std::cout << "CAINE_NATIVE_ACTIVE_OK: automatic loader bootstrap, module observation, ready status, log evidence\n";
    // Plugin is pinned for process lifetime. No unsafe FreeLibrary while its worker runs.
    // Runtime logging owns open handles until process exit. Keep this isolated
    // fixture's diagnostics instead of removing files under its worker thread.
    return 0;
}
