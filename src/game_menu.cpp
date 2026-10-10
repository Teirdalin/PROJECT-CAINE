#include <caine/game_menu.hpp>
#include <caine/native_bridge.hpp>
#include <caine/logging.hpp>
#include <caine/preferences.hpp>
#include <caine/melee_camera.hpp>
#include <algorithm>
#include <cmath>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <set>
#include <cctype>

namespace caine {
namespace {
constexpr char EngineHash[]="9d9aa493cdb0d26820d8cc2db005f84fd74d0cefb642a2a056d95e8e405bcff5";
template<class T> T Method(void* object,size_t slot) { return reinterpret_cast<T>((*static_cast<void***>(object))[slot]); }
bool CleanToken(const std::string& text) {
    return !text.empty() && text.size()<=128 && std::all_of(text.begin(),text.end(),[](unsigned char c){return std::isalnum(c) || c=='_' || c=='-';});
}
std::string Number(double value) { std::ostringstream stream;stream.imbue(std::locale::classic());stream<<std::setprecision(8)<<value;return stream.str(); }
bool Matches(std::string text,std::string filter) {
    auto lower=[](unsigned char c){return static_cast<char>(std::tolower(c));};
    std::transform(text.begin(),text.end(),text.begin(),lower);std::transform(filter.begin(),filter.end(),filter.begin(),lower);
    return text.find(filter)!=std::string::npos;
}
std::string Upper(std::string text) {
    std::transform(text.begin(),text.end(),text.begin(),[](unsigned char c){return static_cast<char>(std::toupper(c));});return text;
}
bool Redirected(const std::filesystem::path& path) { const auto attr=GetFileAttributesW(path.c_str());return attr!=INVALID_FILE_ATTRIBUTES && (attr&FILE_ATTRIBUTE_REPARSE_POINT); }
bool SaveHeader(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary);char signature[4]{};file.read(signature,4);
    return file && memcmp(signature,"JSAV",4)==0;
}
std::vector<std::filesystem::path> SaveFiles(const std::filesystem::path& active) {
    std::vector<std::pair<std::filesystem::file_time_type,std::filesystem::path>> entries;
    std::error_code error;const auto folder=active/L"save";
    if (Redirected(active) || Redirected(folder) || !std::filesystem::is_directory(folder,error)) return {};
    for (std::filesystem::directory_iterator it(folder,error),end;!error && it!=end;it.increment(error)) {
        const auto path=it->path();
        if (Redirected(path) || Upper(path.extension().u8string())!=".SAV" || !CleanToken(path.stem().u8string())) continue;
        if (!it->is_regular_file(error) || error) { error.clear();continue; }
        const auto bytes=it->file_size(error);if (error || !bytes) { error.clear();continue; }
        const auto time=it->last_write_time(error);if (error) { error.clear();continue; }
        entries.emplace_back(time,path);
    }
    std::sort(entries.begin(),entries.end(),[](const auto& a,const auto& b){return a.first!=b.first?a.first>b.first:a.second<b.second;});
    std::vector<std::filesystem::path> result;for (auto& entry:entries) result.push_back(std::move(entry.second));return result;
}
std::string ReadLoose(const std::filesystem::path& path) {
    std::error_code error;const auto size=std::filesystem::file_size(path,error);
    if (error || size>1024*1024 || Redirected(path)) return {};
    std::ifstream input(path,std::ios::binary);std::string text(static_cast<size_t>(size),'\0');input.read(text.data(),static_cast<std::streamsize>(text.size()));return input?text:std::string{};
}
std::string ReadVpk(const std::filesystem::path& path,const std::string& resource) {
    if (Redirected(path)) return {};
    std::ifstream input(path,std::ios::binary);input.seekg(0,std::ios::end);const auto size=input.tellg();
    if (size<9) return {};
    uint32_t count{}, directory{};uint8_t kind{};
    input.seekg(size-std::streamoff(9));input.read(reinterpret_cast<char*>(&count),4);input.read(reinterpret_cast<char*>(&directory),4);input.read(reinterpret_cast<char*>(&kind),1);
    if (!input || count>1000000 || directory>size-std::streamoff(9) || kind>1) return {};
    input.seekg(directory);
    for (uint32_t index=0;index<count;++index) {
        uint32_t length{}, offset{}, bytes{};input.read(reinterpret_cast<char*>(&length),4);
        if (!input || !length || length>4096) return {};
        std::string name(length,'\0');input.read(name.data(),length);input.read(reinterpret_cast<char*>(&offset),4);input.read(reinterpret_cast<char*>(&bytes),4);
        if (!input || static_cast<uint64_t>(offset)+bytes>directory) return {};
        std::replace(name.begin(),name.end(),'\\','/');std::transform(name.begin(),name.end(),name.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
        if (name!=resource) continue;
        if (bytes>1024*1024) return {};
        std::string text(bytes,'\0');input.seekg(offset);input.read(text.data(),bytes);return input?text:std::string{};
    }
    return {};
}
struct Setting { const char* tab;const char* name;const char* label;double low,high;bool toggle,integer;const char* hint; };
// Names and ranges inspected in this installation's GameUI option constructors.
const Setting Settings[]={
    {"Audio","volume","Sound effects volume",0,1,false,false,"Volume of game sound effects."},
    {"Audio","bgmvolume","Music volume",0,1,false,false,"Volume of the original game soundtrack."},
    {"Audio","cl_captions","Dialogue captions",0,1,true,true,"Displays the game's dialogue captions."},
    {"Audio","dsp_on","Environmental sound effects",0,1,true,true,"Room and environment sound processing."},
    {"Audio","hisound","High quality audio",0,1,true,true,"Native audio quality setting."},
    {"Mouse","sensitivity","Mouse sensitivity",.1,20,false,false,"Camera sensitivity; changes apply with Apply settings."},
    {"Mouse","m_pitch","Invert vertical mouse",0,1,true,true,"Reverses the vertical camera axis while retaining your sensitivity."},
    {"Mouse","m_filter","Smooth mouse input",0,1,true,true,"Averages mouse input. Disable for more immediate camera response."},
    {"Gameplay","vdiscipline_allow_renewables","Renew active disciplines",0,1,true,true,"Allows renewal of supported active disciplines."},
    {"Gameplay","damage_floaters","Show floating damage numbers",0,1,true,true,"Displays native floating damage feedback."},
    {"Visual","cl_v_bump_mapping","Bump mapping",0,1,true,true,"Uses surface normal detail where the original materials support it."},
    {"Visual","cl_v_lighting","Lighting quality",0,2,false,true,"Original lighting quality. Does not add HDR or new light sources."},
    {"Visual","cl_v_image_quality","Texture quality",1,4,false,true,"Higher values retain more texture detail. Uses the game's quality mapping; textures may require a reload."},
    {"Visual","mat_trilinear","Trilinear texture filtering",0,1,true,true,"Blends mip levels to reduce visible transitions on distant surfaces. Saved per active game profile by CAINE."},
    {"Visual","fps_max","Frame rate limit",30,240,false,true,"Limits the native frame loop. 60 is recommended for legacy game timing; higher limits need gameplay testing. Saved per active profile."},
    {"Visual","cl_v_combat_effects","Combat effects",0,1,true,true,"Native visual combat effects."},
    {"Visual","cl_v_shadows","Shadow quality",0,3,false,true,"Original shadow modes. Higher settings increase rendering cost."},
    {"Visual","cl_v_shadow_count","Shadow count",1,2,false,true,"Original limit on dynamic shadows."},
    {"Visual","cl_v_geometric_detail","Geometry detail",0,5,false,false,"Original geometry detail and level of detail behavior. Does not change map visibility data."},
    {"Visual","cl_v_gamma","Gamma",1,3,false,false,"Native display gamma. Fullscreen and desktop color settings can affect its appearance."},
    {"Visual","brightness","Brightness",0,5,false,false,"Native brightness adjustment."},
    {"Visual","particle_scale","Particle detail",0,1,false,false,"Original particle detail. Lower values can reduce effect rendering cost."}
};
}
std::optional<GameMenuBackend> NativeGameMenuBackend(const Module& client) {
    if (client.sha256!="9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01") return {};
    const auto engine=Module::Inspect(GetModuleHandleW(L"engine.dll"));
    if (engine.sha256!=EngineHash) return {};
    using Factory=void*(__cdecl*)(const char*,int*);
    const auto factory=reinterpret_cast<Factory>(GetProcAddress(engine.handle,"CreateInterface"));
    if (!factory) return {};
    void* cvars=factory("VEngineCvar001",nullptr);
    void* commands=factory("VEngineClient006",nullptr);
    void* game=factory("VENGINE_GAMEUIFUNCS_VERSION001",nullptr);
    if (!cvars || !commands || !game) return {};
    // These are the installed 2004 interfaces, not current Source SDK layouts.
    if ((*static_cast<uint8_t***>(cvars))[2]!=engine.base+0x43920 ||
        (*static_cast<uint8_t***>(commands))[28]!=engine.base+0x1a570 ||
        (*static_cast<uint8_t***>(commands))[57]!=engine.base+0x1a9e0 ||
        (*static_cast<uint8_t***>(game))[5]!=engine.base+0xfbbd0 ||
        (*static_cast<uint8_t***>(game))[6]!=engine.base+0xfbbf0 ||
        (*static_cast<uint8_t***>(game))[1]!=engine.base+0xfbbb0 ||
        (*static_cast<uint8_t***>(game))[2]!=engine.base+0xfbbc0) return {};
    GameMenuBackend backend;
    backend.firstPersonMeleeAvailable=[] { return MeleeCameraAvailable(); };
    backend.fieldOfView=[] { return NativeFieldOfView(); };
    backend.inGame=[commands] { using Active=bool(__thiscall*)(void*);return Method<Active>(commands,57)(commands); };
    backend.read=[cvars,cache=std::map<std::string,uint8_t*>{}](const char* name) mutable ->std::optional<double> {
        using Find=void*(__thiscall*)(void*,const char*);
        auto variable=cache[name];
        if (!variable) variable=static_cast<uint8_t*>(Method<Find>(cvars,2)(cvars,name));
        if (!variable) return {};
        cache[name]=variable; // registered engine ConVars live for the process
        // ConVar +4 is its registered parent. The inspected getter reads +0x28.
        const auto parent=*reinterpret_cast<uint8_t**>(variable+4);
        if (!parent) return {};
        const auto value=*reinterpret_cast<float*>(parent+0x28);
        return std::isfinite(value)?std::optional<double>(value):std::nullopt;
    };
    backend.command=[commands](const std::string& command) {
        std::istringstream lines(command);std::string line;
        while (std::getline(lines,line)) {
            const auto end=line.find_first_of(" \t\r");
            TraceLog("CAINE_ENGINE_COMMAND: verb="+line.substr(0,end)+" bytes="+std::to_string(line.size())+" arguments=omitted");
        }
        using Command=void(__thiscall*)(void*,const char*);
        Method<Command>(commands,28)(commands,command.c_str());
    };
    backend.modes=[game] {
        using Modes=void(__thiscall*)(void*,const uint8_t**,int*);
        const uint8_t* source{};int count{};Method<Modes>(game,5)(game,&source,&count);
        std::vector<VideoMode> result;
        if (!source || count<0 || count>4096) return result;
        for (int i=0;i<count;++i) {
            const auto row=reinterpret_cast<const int*>(source+static_cast<size_t>(i)*0x2c);
            if (row[0]>=640 && row[0]<=16384 && row[1]>=480 && row[1]<=16384 && (row[2]==16 || row[2]==32)) result.push_back({row[0],row[1],row[2]});
        }
        return result;
    };
    backend.currentMode=[game] { using Mode=void(__thiscall*)(void*,int*,int*,int*);VideoMode value;Method<Mode>(game,6)(game,&value.width,&value.height,&value.depth);return value; };
    backend.bindings=[game] {
        using Name=const char*(__thiscall*)(void*,int);
        std::vector<KeyBinding> result;
        for (int i=0;i<256;++i) {
            const auto key=Method<Name>(game,1)(game,i),command=Method<Name>(game,2)(game,i);
            if (key && command && *key && *command) result.push_back({key,command});
        }
        return result;
    };
    backend.setFieldOfView=[command=backend.command](double value) {
        if (!SetNativeFieldOfView(value)) return false;
        command("fov "+Number(std::round(std::clamp(value,60.,135.)))+"\n");return true;
    };
    backend.keyNames=[game] {
        using Name=const char*(__thiscall*)(void*,int);
        std::vector<std::string> result;
        for (int i=0;i<256;++i) {
            const auto name=Method<Name>(game,1)(game,i);
            if (name && *name && name[0]!='<') result.emplace_back(name);
        }
        return result;
    };
    for (const auto& entry:Settings) {
        const auto value=backend.read(entry.name);
        WriteLog(std::string("CAINE_NATIVE_SETTING_OBSERVED: name=")+entry.name+" value="+(value?Number(*value):"unavailable"));
    }
    return backend;
}
std::string ReadGameMenuResource(const std::filesystem::path& root,const std::filesystem::path& active,const std::string& resource) {
    if (resource.find("..")!=std::string::npos || resource.empty() || resource.front()=='/') return {};
    std::string result;
    for (const auto& folder:std::vector<std::filesystem::path>{root/L"Vampire",active}) {
        if (Redirected(folder) || !std::filesystem::is_directory(folder)) continue;
        std::vector<std::filesystem::path> archives;
        for (const auto& entry:std::filesystem::directory_iterator(folder)) if (entry.path().extension()==L".vpk") archives.push_back(entry.path());
        std::sort(archives.begin(),archives.end());
        for (const auto& archive:archives) { auto text=ReadVpk(archive,resource);if (!text.empty()) result=std::move(text); }
        auto text=ReadLoose(folder/std::filesystem::u8path(resource));if (!text.empty()) result=std::move(text);
    }
    return result;
}
GameMenus::GameMenus(GameMenuBackend backend,std::filesystem::path root,std::filesystem::path active):backend_(std::move(backend)),root_(std::move(root)),active_(std::move(active)),config_(root_/L"Bin/loader/CAINE/CAINE.ini") {
    patchInstalled_=UnofficialPatchInstalled(root_);
    // These engine variables are not archived by host_writeconfig. Restore only
    // values explicitly chosen in CAINE, and only for the active -game profile.
    const auto section=L"Graphics."+active_.filename().wstring();
    for (const auto& entry:Settings) if (std::string(entry.name)=="fps_max" || std::string(entry.name)=="mat_trilinear") {
        wchar_t buffer[64]{};const auto key=std::wstring(entry.name,entry.name+strlen(entry.name));
        if (!GetPrivateProfileStringW(section.c_str(),key.c_str(),L"",buffer,64,config_.c_str())) continue;
        wchar_t* end{};const auto value=wcstod(buffer,&end);
        if (end!=buffer && !*end && std::isfinite(value) && value>=entry.low && value<=entry.high && backend_.read(entry.name) && backend_.command)
            backend_.command(std::string(entry.name)+" "+Number(std::round(value))+"\n");
    }
}
bool GameMenus::HasContinueSave() {
    const auto now=GetTickCount64();
    if (!continueChecked_ || now-continueChecked_>=1000) {
        continueSave_.reset();
        for (const auto& save:SaveFiles(active_)) {
            if (SaveHeader(save)) { continueSave_=save;break; }
        }
        continueChecked_=now;
    }
    return continueSave_.has_value();
}
bool GameMenus::ContinueLatest() {
    // Repeat enumeration after the click: a save may have been removed or a
    // newer quick/autosave written since the menu was drawn.
    continueChecked_=0;if (!HasContinueSave() || !backend_.command) return false;
    backend_.command("load "+continueSave_->stem().u8string()+"\n");
    TraceLog("CAINE_CONTINUE_SUBMITTED: active profile's latest save");return true;
}
void GameMenus::RefreshBindings() {
    bindings_=backend_.bindings();bindingsChecked_=GetTickCount64();const auto& bindings=bindings_;
    for (const auto& [command,label]:actionsList_) {
        (void)label;
        auto& slots=bindingSlots_[command];
        std::vector<std::string> keys;
        for (const auto& binding:bindings) if (binding.command==command && Upper(binding.key)!="ESCAPE") {
            const auto key=Upper(binding.key);
            if (std::find(keys.begin(),keys.end(),key)==keys.end()) keys.push_back(key);
        }
        for (auto& slot:slots) if (std::find(keys.begin(),keys.end(),slot)==keys.end()) slot.clear();
        for (const auto& key:keys) if (key!=slots[0] && key!=slots[1]) {
            if (slots[0].empty()) slots[0]=key;else if (slots[1].empty()) slots[1]=key;
        }
    }
}
void GameMenus::Open(GameMenuPage page) {
    TraceLog("CAINE_GAME_MENU_OPEN: page="+std::to_string(static_cast<int>(page)));
    page_=page;message_.clear();search_.clear();confirmSave_.clear();actions_.clear();pending_.clear();pendingMode_.reset();
    if (page==GameMenuPage::Settings && actionsList_.empty()) {
        std::istringstream source(ReadGameMenuResource(root_,active_,"scripts/kb_act.lst"));std::string line;
        while (std::getline(source,line)) {
            const auto a=line.find('"'),b=a==std::string::npos?a:line.find('"',a+1),c=b==std::string::npos?b:line.find('"',b+1),d=c==std::string::npos?c:line.find('"',c+1);
            if (d==std::string::npos) continue;
            const auto action=line.substr(a+1,b-a-1),label=line.substr(c+1,d-c-1);
            if (action.empty() || action.size()>128 || action==" " || action.find_first_not_of("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_ +#")!=std::string::npos) continue;
            actionsList_.emplace_back(action,label);
        }
    }
    if (page==GameMenuPage::Settings) RefreshBindings();
    if (page==GameMenuPage::Credits && credits_.empty()) {
        std::istringstream source(ReadGameMenuResource(root_,active_,"vdata/system/credits.txt"));std::string line;
        while (std::getline(source,line)) {
            line.erase(std::remove(line.begin(),line.end(),'\r'),line.end());
            if (line.empty()) continue;
            if (std::string("#@$%^{}").find(line.front())!=std::string::npos) continue;
            // The shipped resource is CP1252. Preserve names and punctuation in UTF-8.
            const auto count=MultiByteToWideChar(1252,0,line.data(),static_cast<int>(line.size()),nullptr,0);std::wstring wide(static_cast<size_t>(count),0);
            MultiByteToWideChar(1252,0,line.data(),static_cast<int>(line.size()),wide.data(),count);
            const auto bytes=WideCharToMultiByte(CP_UTF8,0,wide.data(),count,nullptr,0,nullptr,nullptr);std::string utf8(static_cast<size_t>(bytes),0);
            WideCharToMultiByte(CP_UTF8,0,wide.data(),count,utf8.data(),bytes,nullptr,nullptr);credits_+=utf8+"\n";
        }
    }
    if (page==GameMenuPage::Load || page==GameMenuPage::Save) {
        saves_=SaveFiles(active_);selectedSave_.clear();saveName_.clear();continueChecked_=0;
        if (page==GameMenuPage::Load) saves_.erase(std::remove_if(saves_.begin(),saves_.end(),[](const auto& path){return !SaveHeader(path);}),saves_.end());
    }
}
bool GameMenus::SubmitSave(bool saving,const std::string& name,bool confirmed) {
    if (!CleanToken(name) || !backend_.command) { message_="Use a save name with letters, numbers, underscores or hyphens (up to 128 characters).";return false; }
    const auto folder=active_/L"save";
    if (Redirected(active_) || Redirected(folder)) { message_="The active save folder is redirected. No save action was submitted.";return false; }
    std::error_code error;std::optional<std::filesystem::path> existing;
    if (std::filesystem::is_directory(folder,error)) {
        for (std::filesystem::directory_iterator it(folder,error),end;!error && it!=end;it.increment(error)) {
            const auto path=it->path();
            if (Upper(path.extension().u8string())!=".SAV" || _stricmp(path.stem().u8string().c_str(),name.c_str())!=0) continue;
            if (Redirected(path) || !it->is_regular_file(error) || error) { message_="The selected save is inaccessible or redirected.";return false; }
            existing=path;break;
        }
    }
    if (error) { message_="Could not inspect the active save folder.";return false; }
    if (!saving && (!existing || !SaveHeader(*existing))) { confirmSave_.clear();message_="This save was removed or is not a recognized Bloodlines save. Reopen the browser to refresh it.";return false; }
    if (saving && (!backend_.inGame || !backend_.inGame())) { message_="Saving is available during an active game.";return false; }
    const bool needsConfirm=saving?existing.has_value():(backend_.inGame && backend_.inGame());
    if (needsConfirm && !confirmed) { confirmSave_=name;return false; }
    backend_.command((saving?"save ":"load ")+name+"\n");return true;
}
void GameMenus::Build(MenuView& view) {
    actions_.clear();view.controls.clear();view.message=message_;uint32_t next=1;
    auto add=[&](uint32_t kind,std::string label,std::string text,double value,double low,double high,uint32_t flags,std::function<bool(const std::string&,double)> action={}) {
        const auto id=next++;view.controls.push_back({kind,id,flags,1024,std::move(label),std::move(text),{},value,low,high});if (action) actions_.emplace(id,std::move(action));
    };
    auto text=[&](const std::string& label){add(CAINE_CONTROL_TEXT,label,{},0,0,0,0);};
    auto heading=[&](const std::string& label){add(CAINE_CONTROL_HEADING,label,{},0,0,0,0);};
    auto button=[&](const std::string& label,std::function<bool()> action,bool disabled=false){add(CAINE_CONTROL_BUTTON,label,{},0,0,0,disabled?CAINE_CONTROL_DISABLED:0,[action](const std::string&,double){return action();});};
    if (page_==GameMenuPage::Credits) {
        view.pageTitle="Credits";heading("PROJECT CAINE");text("Native Bloodlines mod framework and menu renderer.");
        for (const auto& mod:view.mods) { heading(mod.name);if (!mod.author.empty()) text(mod.author); }
        heading("Technology");text("Dear ImGui by Omar Cornut and contributors (MIT). MinHook by Tsuda Kageyu and contributors. Native loader by Behar, with support by Psycho-A.");
        heading("Vampire: The Masquerade - Bloodlines");text(credits_.empty()?"Developed by Troika Games. Published by Activision. World of Darkness by White Wolf. Original credits could not be read from this installation.":credits_);return;
    }
    if (page_==GameMenuPage::Load || page_==GameMenuPage::Save) {
        const bool saving=page_==GameMenuPage::Save;view.pageTitle=saving?"Save game":"Load game";
        if (!confirmSave_.empty()) {
            heading(saving?"Overwrite this save?":"Load this save?");text(confirmSave_);
            if (!saving) text("Unsaved progress will be lost.");
            button(saving?"Overwrite save":"Load selected save",[this,saving]{return SubmitSave(saving,confirmSave_,true);});
            button("Cancel",[this]{confirmSave_.clear();return false;});return;
        }
        add(CAINE_CONTROL_INPUT,"Search saves",search_,0,0,0,CAINE_CONTROL_LIVE,[this](const std::string& value,double){search_=value;return false;});
        if (saves_.empty()) text("No saves found in the active game's save folder.");
        for (const auto& path:saves_) {
            const auto name=path.stem().u8string();if (!Matches(name,search_)) continue;
            add(CAINE_CONTROL_CHOICE,name,{},0,0,0,name==selectedSave_?CAINE_CONTROL_SELECTED:0,[this,name](const std::string&,double){selectedSave_=name;saveName_=name;return false;});
        }
        if (saving) add(CAINE_CONTROL_INPUT,"Save name",saveName_,0,0,0,CAINE_CONTROL_LIVE,[this](const std::string& value,double){saveName_=value;return false;});
        button(saving?"Save game":"Load selected save",[this,saving]{
            return SubmitSave(saving,saving?saveName_:selectedSave_,false);
        },saving?saveName_.empty():selectedSave_.empty());return;
    }
    view.pageTitle="Settings";
    for (const std::string tab:{"Audio","Mouse","Keyboard","Gameplay","Video","Visual","Framework"}) add(CAINE_CONTROL_TAB,tab=="Visual"?"Graphics":tab=="Video"?"Display":tab,{},0,0,0,tab==tab_?CAINE_CONTROL_SELECTED:0,[this,tab](const std::string&,double){tab_=tab;search_.clear();if(tab=="Keyboard")RefreshBindings();return false;});
    heading(tab_=="Visual"?"Graphics":tab_=="Video"?"Display":tab_);
    if (tab_=="Gameplay") {
        const bool available=backend_.firstPersonMeleeAvailable && backend_.firstPersonMeleeAvailable();
        const auto& options=GameplayOptions();
        const auto enabled=ReadFrameworkOption(config_,options[0]);
        for (const auto& option:options) {
            const bool disabled=!available || (!option.toggle && enabled==0);
            const auto value=ReadFrameworkOption(config_,option);
            const std::string selected=option.toggle?"":(value==0?"Native first person (custom viewmodels)":"Body camera (experimental)");
            add(option.toggle?CAINE_CONTROL_TOGGLE:MenuControlDropdown,option.label,selected,value,0,1,
                CAINE_CONTROL_INTEGER|CAINE_CONTROL_LIVE|(disabled?CAINE_CONTROL_DISABLED:0),
                [this,option](const std::string&,double number){
                    if (backend_.firstPersonMeleeAvailable && backend_.firstPersonMeleeAvailable() &&
                        (option.toggle || ReadFrameworkOption(config_,GameplayOptions()[0])!=0) && std::isfinite(number) && (number==0 || number==1))
                        message_=WriteFrameworkOption(config_,option,number)?"Melee camera setting applied and saved.":"Could not save CAINE settings. Check the installation folder's permissions.";
                    return false;
                });
            if (!option.toggle) view.controls.back().options={"Native first person (custom viewmodels)","Body camera (experimental)"};
            view.controls.back().hint=available?option.hint:"Unavailable: this client build or an existing camera hook is incompatible with CAINE's melee camera feature.";
        }
    }
    if (tab_=="Framework") {
        text("Active game profile: "+active_.filename().u8string());
        const bool patchActive=_wcsicmp(active_.filename().c_str(),L"Unofficial_Patch")==0;
        text(patchInstalled_?(patchActive?"Unofficial Patch: detected and active.":"Unofficial Patch: detected. Start through its shortcut or use -game Unofficial_Patch to enable it. The CAINE launcher selects it automatically."):"Unofficial Patch: not detected in the standard Unofficial_Patch folder. Other mod profiles can be selected with -game.");
        for (const auto& option:FrameworkOptions()) {
            add(option.toggle?CAINE_CONTROL_TOGGLE:CAINE_CONTROL_SLIDER,option.label,{},ReadFrameworkOption(config_,option),option.minimum,option.maximum,CAINE_CONTROL_INTEGER|CAINE_CONTROL_LIVE,
                [this,option](const std::string&,double value){message_=WriteFrameworkOption(config_,option,value)?(option.live?"Setting applied and saved.":"Setting saved. Restart Bloodlines to apply it."):"Could not save CAINE settings. Check the installation folder's permissions.";return false;});
            view.controls.back().hint=option.hint;
        }
        return;
    }
    if (tab_=="Visual" && backend_.fieldOfView && backend_.setFieldOfView) {
        text(RendererDescription());
        add(CAINE_CONTROL_SLIDER,"Field of view",{},backend_.fieldOfView(),60,135,CAINE_CONTROL_INTEGER|CAINE_CONTROL_LIVE,
            [this](const std::string&,double value){
                if (std::isfinite(value)) message_=backend_.setFieldOfView(std::round(std::clamp(value,60.,135.)))?"Field of view applied and saved.":"Could not save field of view. Check that CAINE's settings folder is writable.";
                return false;
            });
        view.controls.back().hint="Applies immediately through Bloodlines' fov command and is saved for future loads and restarts.";
    }
    for (const auto& entry:Settings) {
        if (tab_!=entry.tab) continue;
        const auto native=backend_.read(entry.name);if (!native) { text(std::string(entry.label)+": unavailable in this game build");continue; }
        const bool pitch=std::string(entry.name)=="m_pitch";
        const auto staged=pending_.find(entry.name);const auto value=staged==pending_.end()?(pitch?(*native<0?1.:0.):*native):staged->second;
        add(entry.toggle?CAINE_CONTROL_TOGGLE:CAINE_CONTROL_SLIDER,entry.label,{},value,entry.low,entry.high,entry.integer?CAINE_CONTROL_INTEGER:0,[this,entry](const std::string&,double number){
            if (std::isfinite(number)) pending_[entry.name]=entry.integer?std::round(std::clamp(number,entry.low,entry.high)):std::clamp(number,entry.low,entry.high);return false;
        });
        view.controls.back().hint=entry.hint;
    }
    if (tab_=="Video") {
        const auto current=backend_.currentMode();const auto selected=pendingMode_.value_or(current);
        auto label=[](const VideoMode& mode) { return std::to_string(mode.width)+" x "+std::to_string(mode.height)+" / "+std::to_string(mode.depth)+" bit"; };
        std::vector<VideoMode> modes;
        std::vector<std::string> options;
        double selectedIndex=-1;
        std::set<std::string> seen;
        for (const auto& mode:backend_.modes()) {
            const auto name=label(mode);
            if (!seen.insert(name).second) continue;
            if (mode.width==selected.width && mode.height==selected.height && mode.depth==selected.depth)
                selectedIndex=static_cast<double>(modes.size());
            modes.push_back(mode);options.push_back(name);
        }
        add(MenuControlDropdown,"Resolution",label(selected),selectedIndex,0,0,modes.empty()?CAINE_CONTROL_DISABLED:0,
            [this,modes](const std::string&,double index){
                if (std::isfinite(index) && index>=0 && index<static_cast<double>(modes.size()) && index==std::floor(index))
                    pendingMode_=modes[static_cast<size_t>(index)];
                return false;
            });
        view.controls.back().options=std::move(options);
        view.controls.back().hint=modes.empty()?"No display modes available from Bloodlines.":"Video mode changes use Bloodlines' existing video settings and may require restarting the game.";
    }
    if (tab_=="Keyboard") {
        text("Click a Primary or Alternative box, then press the new key. Escape cancels. Assigning a key replaces its current action.");
        add(CAINE_CONTROL_INPUT,"Search actions",search_,0,0,0,CAINE_CONTROL_LIVE,[this](const std::string& value,double){search_=value;return false;});
        // Key tables are 256 native calls each. Refresh once per second while
        // browsing, and revalidate the replaced key at the actual edit.
        if (GetTickCount64()-bindingsChecked_>=1000) RefreshBindings();
        const auto& bindings=bindings_;
        for (const auto& [command,label]:actionsList_) {
            if (!Matches(label+" "+command,search_)) continue;
            const auto& slots=bindingSlots_[command];
            add(MenuControlBindings,label,{},0,0,0,0,[this,command](const std::string& value,double number){
                if (number!=0 && number!=1) return false;
                const auto slot=static_cast<size_t>(number);auto& keys=bindingSlots_[command];
                std::string key=Upper(value);
                if (!key.empty()) {
                    const auto names=backend_.keyNames();
                    if (key=="ESCAPE" || key.size()>32 || key.find_first_of("\"\r\n")!=std::string::npos ||
                        std::none_of(names.begin(),names.end(),[&](const auto& name){return Upper(name)==key;})) {
                        message_="This key is unavailable in Bloodlines. Escape remains reserved for closing menus.";return false;
                    }
                }
                if (key==keys[slot]) return false;
                if (!key.empty() && key==keys[1-slot]) { std::swap(keys[0],keys[1]);return false; }
                std::string request;
                const auto native=backend_.bindings();
                // Only remove the replaced slot if it still belongs to this action.
                if (!keys[slot].empty() && std::any_of(native.begin(),native.end(),[&](const auto& entry){return Upper(entry.key)==keys[slot] && entry.command==command;}))
                    request="unbind \""+keys[slot]+"\"\n";
                if (!key.empty()) request+="bind \""+key+"\" \""+command+"\"\n";
                if (!request.empty()) backend_.command(request+"host_writeconfig\n");
                for (auto& [other,pair]:bindingSlots_) if (other!=command)
                    for (auto& entry:pair) if (!key.empty() && entry==key) entry.clear();
                keys[slot]=key;message_="Key binding submitted to Bloodlines.";return false;
            });
            view.controls.back().options={slots[0],slots[1]};
            std::string extras;
            for (const auto& binding:bindings) if (binding.command==command && Upper(binding.key)!=slots[0] && Upper(binding.key)!=slots[1] && Upper(binding.key)!="ESCAPE")
                extras+=(extras.empty()?"Also bound: ":", ")+Upper(binding.key);
            view.controls.back().hint=extras;
        }
    }
    heading("Apply changes");
    button("Apply settings",[this]{
        bool persistenceFailed=false;
        for (const auto& [name,value]:pending_) {
            if (name=="in_mlook") backend_.command(value!=0?"+mlook\n":"-mlook\n");
            else if (name=="m_pitch") { const auto native=backend_.read("m_pitch").value_or(.022);backend_.command("m_pitch "+Number((value!=0?-1:1)*std::max(std::abs(native),.001))+"\n"); }
            else backend_.command(name+" "+Number(value)+"\n");
            if (name=="fps_max" || name=="mat_trilinear") {
                const auto section=L"Graphics."+active_.filename().wstring(),key=std::wstring(name.begin(),name.end());
                const auto number=std::to_wstring(static_cast<int>(std::round(value)));
                persistenceFailed|=WritePrivateProfileStringW(section.c_str(),key.c_str(),number.c_str(),config_.c_str())==FALSE;
            }
        }
        if (pendingMode_) { const auto& mode=*pendingMode_;backend_.command("_setvideomode "+std::to_string(mode.width)+" "+std::to_string(mode.height)+" "+std::to_string(mode.depth)+"\n"); }
        backend_.command("host_writeconfig\n");pending_.clear();pendingMode_.reset();message_=persistenceFailed?"Settings applied, but CAINE could not save the graphics preferences. Check folder permissions.":"Settings submitted and saved. Restart after changing display mode.";return false;
    },pending_.empty() && !pendingMode_);
    button("Discard pending changes",[this]{pending_.clear();pendingMode_.reset();message_="Pending changes discarded.";return false;},pending_.empty() && !pendingMode_);
}
bool GameMenus::Action(uint32_t id,const std::string& text,double number) {
    const auto found=actions_.find(id);
    TraceLog("CAINE_GAME_MENU_ACTION: page="+std::to_string(static_cast<int>(page_))+" control="+std::to_string(id)+" number="+Number(number)+" text_bytes="+std::to_string(text.size())+" known="+std::to_string(found!=actions_.end()));
    return found!=actions_.end() && found->second(text,number);
}
}
