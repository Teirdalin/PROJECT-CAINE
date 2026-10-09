#include <windows.h>
#include <filesystem>
#include <fstream>
int wmain(int argc,wchar_t** argv) {
    if(argc<2)return 1;
    const std::filesystem::path marker=argv[1];
    if(std::filesystem::exists(marker)) {
        wchar_t cwd[32768]{};GetCurrentDirectoryW(32768,cwd);
        std::wofstream output(marker.wstring()+L".restarted");
        output<<GetCommandLineW()<<L'\n'<<cwd<<L'\n';return 0;
    }
    for(int i=0;i<600 && !std::filesystem::exists(marker);++i)Sleep(100);
    return std::filesystem::exists(marker)?0:2;
}
