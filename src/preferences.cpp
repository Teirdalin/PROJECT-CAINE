#include <caine/preferences.hpp>
#include <caine/logging.hpp>
#include <windows.h>
#include <shellapi.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>

namespace caine {
namespace {
std::atomic<float> menuScale{1.f};
std::atomic<bool> meleeEnabled{},meleeBody{true};
bool SafeFile(const std::filesystem::path& file) {
    for (auto part=file; !part.empty(); part=part.parent_path()) {
        const auto attributes=GetFileAttributesW(part.c_str());
        if (attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT)) return false;
        if (part==part.parent_path()) break;
    }
    return true;
}
void Apply(const FrameworkOption& option,double value) {
    if (std::string(option.id)=="scale") menuScale.store(static_cast<float>(value)/100.f);
    if (std::string(option.id)=="verbose") SetVerboseLogging(value!=0);
    if (std::string(option.id)=="first_person_melee") meleeEnabled.store(value!=0);
    if (std::string(option.id)=="melee_body_camera") meleeBody.store(value!=0);
}
}
const std::vector<FrameworkOption>& FrameworkOptions() {
    static const std::vector<FrameworkOption> options{
        {"scale",L"Menu",L"ScalePercent","Interface scale","Applies immediately. Scales CAINE text and controls; original character sheet and HUD keep their own style.",100,75,150,false,true},
        {"verbose",L"Logging",L"Verbose","Detailed debug logging","Applies immediately. Includes hook, menu, input and dialogue events. Logs start fresh on each launch; errors and crash diagnostics remain enabled.",1,0,1,true,true},
        {"modern",L"Menu",L"Modern","Modern menus","Requires restart. CAINE main/pause menus, settings, Mods and save browser. Original character sheet, inventory and quest screens retain their native layout.",1,0,1,true,false},
        {"startup",L"Startup",L"SkipVideos","Skip startup logos","Requires restart. Skips the four publisher/developer videos without removing game files.",1,0,1,true,false},
        {"intro",L"Intro",L"Enabled","Hold Escape to skip the opening cinematic","Requires restart. Uses the native tutorial transition and preserves story initialization.",1,0,1,true,false},
        {"hold",L"Intro",L"HoldMilliseconds","Escape hold duration (milliseconds)","Requires restart. Releasing Escape or losing focus cancels the hold.",1500,500,5000,false,false},
        {"updates",L"Updates",L"Check","Check for updates","Requires restart. Checks GitHub in the background. Installation starts only after Install and Restart is selected.",1,0,1,true,false},
        {"preview",L"Updates",L"Preview","Include development updates","Requires restart. Allows prereleases as well as stable releases.",1,0,1,true,false},
        {"interval",L"Updates",L"IntervalHours","Update check interval (hours)","Requires restart. No network work runs on the rendering thread.",6,1,168,false,false},
        {"crash",L"Crash",L"Enabled","Crash reports","Requires restart. Captures exception context, loaded modules, stacks and recent activity locally.",1,0,1,true,false},
        {"dump",L"Crash",L"Dump","Include crash minidumps","Requires restart. Useful for diagnosis; dump files can include process memory and are kept locally.",1,0,1,true,false},
        {"limit",L"Crash",L"MaxReports","Maximum crash reports per launch","Requires restart. Bounds first-chance reports while retaining the game's exception handling.",16,1,64,false,false}
    };
    return options;
}
double ReadFrameworkOption(const std::filesystem::path& config,const FrameworkOption& option) {
    wchar_t buffer[64]{};
    if (!SafeFile(config) || !GetPrivateProfileStringW(option.section,option.key,L"",buffer,64,config.c_str())) return option.initial;
    wchar_t* end{};const auto value=wcstod(buffer,&end);
    return end!=buffer && !*end && std::isfinite(value)?std::clamp(std::round(value),option.minimum,option.maximum):option.initial;
}
const std::vector<FrameworkOption>& GameplayOptions() {
    static const std::vector<FrameworkOption> options{
        {"first_person_melee",L"Gameplay",L"FirstPersonMelee","First Person Melee","",0,0,1,true,true},
        {"melee_body_camera",L"Gameplay",L"MeleeBodyCamera","Melee camera style","",1,0,1,false,true}
    };
    return options;
}
bool FirstPersonMeleeEnabled() { return meleeEnabled.load(); }
bool MeleeBodyCamera() { return meleeBody.load(); }
bool WriteFrameworkOption(const std::filesystem::path& config,const FrameworkOption& option,double value) {
    if (!std::isfinite(value) || !SafeFile(config)) return false;
    value=std::round(std::clamp(value,option.minimum,option.maximum));
    const auto text=std::to_wstring(static_cast<int>(value));
    if (!WritePrivateProfileStringW(option.section,option.key,text.c_str(),config.c_str())) return false;
    Apply(option,value);
    WriteLog(std::string("CAINE_PREFERENCE_SAVED: ")+option.id+" value="+std::to_string(static_cast<int>(value))+" live="+std::to_string(option.live));
    return true;
}
void InitializeFrameworkPreferences(const std::filesystem::path& config) {
    for (const auto& option:FrameworkOptions()) if (option.live) Apply(option,ReadFrameworkOption(config,option));
    for (const auto& option:GameplayOptions()) Apply(option,ReadFrameworkOption(config,option));
}
float MenuScale() { return menuScale.load(); }
bool UnofficialPatchInstalled(const std::filesystem::path& root) {
    const auto patch=root/L"Unofficial_Patch";
    std::error_code error;
    return SafeFile(patch/L"cfg") && SafeFile(patch/L"maps") &&
        std::filesystem::is_directory(patch/L"cfg",error) && !error &&
        std::filesystem::is_directory(patch/L"maps",error) && !error;
}
std::filesystem::path ActiveGameFolder(const std::filesystem::path& root) {
    auto result=root/L"Vampire";
    int count{};auto args=CommandLineToArgvW(GetCommandLineW(),&count);
    if (args) {
        for (int i=1;i+1<count;++i) if (_wcsicmp(args[i],L"-game")==0) {
            const std::wstring name=args[i+1];
            if (!name.empty() && name!=L"." && name!=L".." && name.find_first_of(L"\\/:")==std::wstring::npos) result=root/name;
            break;
        }
        LocalFree(args);
    }
    return result;
}
}
