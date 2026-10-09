#include <caine/game_menu.hpp>
#include <caine/key_input.hpp>
#include <caine/preferences.hpp>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>

namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path,const std::string& text) { std::filesystem::create_directories(path.parent_path());std::ofstream(path,std::ios::binary)<<text; }
void Vpk(const std::filesystem::path& path,const std::string& resource,const std::string& text) {
    std::filesystem::create_directories(path.parent_path());std::ofstream out(path,std::ios::binary);out<<text;
    uint32_t count=1,directory=static_cast<uint32_t>(text.size()),length=static_cast<uint32_t>(resource.size()),offset=0,bytes=directory;uint8_t kind=0;
    out.write(reinterpret_cast<char*>(&length),4);out<<resource;out.write(reinterpret_cast<char*>(&offset),4);out.write(reinterpret_cast<char*>(&bytes),4);
    out.write(reinterpret_cast<char*>(&count),4);out.write(reinterpret_cast<char*>(&directory),4);out.write(reinterpret_cast<char*>(&kind),1);
}
uint32_t Find(const caine::MenuView& view,const std::string& label) { for (const auto& control:view.controls) if (control.label==label) return control.id;throw std::runtime_error("Missing control: "+label); }
}
int main() {
    try {
        const auto root=std::filesystem::temp_directory_path()/("CAINE-game-menus-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(GetTickCount64()));
        const auto active=root/"Unofficial_Patch";
        Write(root/"Bin/loader/CAINE/CAINE.ini","[Runtime]\nEnabled=1\n[Unrelated]\nSentinel=keep\n");
        Write(root/"Vampire/scripts/kb_act.lst","\"+attack\" \"Primary attack\"\n\"save quick\" \"Quick save\"\n");
        Vpk(root/"Vampire/pack000.vpk","vdata/system/credits.txt","@MainMenu\nTroika Games\n#ignored\n");
        Vpk(active/"pack000.vpk","vdata/system/credits.txt","@MainMenu\nPatch credits\n");
        Check(caine::ReadGameMenuResource(root,active,"vdata/system/credits.txt").find("Patch credits")!=std::string::npos,"VPK layer precedence");
        Write(active/"vdata/system/credits.txt","@MainMenu\nLocal credits\n");
        Check(caine::ReadGameMenuResource(root,active,"vdata/system/credits.txt").find("Local credits")!=std::string::npos,"loose file precedence");
        Check(caine::ReadGameMenuResource(root,active,"../secret").empty(),"resource traversal");
        Write(active/"save/Foo.sav","save sentinel");Write(root/"Vampire/save/wrong-layer.sav","another sentinel");
        std::vector<std::string> commands;
        std::map<std::string,double> values{{"volume",.8},{"m_pitch",.05},{"fps_max",100},{"mat_trilinear",0}};
        caine::GameMenuBackend backend;
        backend.read=[&](const char* name)->std::optional<double>{const auto found=values.find(name);return found==values.end()?std::nullopt:std::optional<double>(found->second);};
        backend.command=[&](const std::string& command){commands.push_back(command);};
        bool inGame=true;backend.inGame=[&] { return inGame; };
        double fov=90;bool fovWritable=true;
        backend.fieldOfView=[&] { return fov; };
        backend.setFieldOfView=[&](double value) { if (!fovWritable) return false;fov=value;return true; };
        backend.currentMode=[] { return caine::VideoMode{800,600,32}; };
        backend.modes=[] { return std::vector<caine::VideoMode>{{800,600,32},{1920,1080,32}}; };
        std::vector<caine::KeyBinding> bindings{{"MOUSE1","+attack"},{"SPACE","+attack"},{"ESCAPE","cancelselect"}};
        backend.bindings=[&] { return bindings; };
        backend.keyNames=[] { return std::vector<std::string>{"MOUSE1","SPACE","F","MWHEELUP","ESCAPE"}; };
        caine::GameMenus menus(backend,root,active);caine::MenuView view;
        Check(!menus.ContinueLatest() && commands.empty(),"Continue must reject an unreadable save header");
        Write(root/"Vampire/save/other-profile.sav","JSAVanother save");
        Write(active/"save/Older.sav","JSAVolder save");Write(active/"save/Latest.SAV","JSAVlatest save");
        Write(active/"save/empty.sav","");Write(active/"save/inject;quit.sav","JSAVinvalid name");
        const auto time=std::filesystem::file_time_type::clock::now();
        std::filesystem::last_write_time(active/"save/Older.sav",time-std::chrono::hours(1));
        std::filesystem::last_write_time(active/"save/Latest.SAV",time);
        Check(menus.ContinueLatest() && commands.back()=="load Latest\n","Continue selects latest native save from active profile");commands.clear();
        std::filesystem::remove(active/"save/Latest.SAV");
        Check(menus.ContinueLatest() && commands.back()=="load Older\n","Continue must revalidate deleted cached save");commands.clear();
        std::filesystem::remove(active/"save/Older.sav");std::filesystem::remove(active/"save/empty.sav");std::filesystem::remove(active/"save/inject;quit.sav");
        Check(!menus.ContinueLatest() && commands.empty(),"Continue must never load a different game profile");
        auto build=[&] { view={};menus.Build(view); };
        auto action=[&](const std::string& label,const std::string& text="",double number=0) { return menus.Action(Find(view,label),text,number); };
        menus.Open(caine::GameMenuPage::Settings);build();action("Sound effects volume","",-100);Check(commands.empty(),"settings committed before Apply");build();action("Apply settings");
        Check(commands.size()==2 && commands[0]=="volume 0\n" && commands[1]=="host_writeconfig\n","bounded settings and persistence");commands.clear();
        build();action("Mouse");build();action("Invert vertical mouse","",1);build();action("Apply settings");
        Check(commands.front()=="m_pitch -0.05\n","invert preserves sensitivity magnitude");commands.clear();
        build();action("Invert vertical mouse","",0);build();action("Discard pending changes");build();Check(commands.empty(),"discard changed native settings");
        action("Display");build();action("Resolution","",1);build();action("Apply settings");
        Check(commands.front()=="_setvideomode 1920 1080 32\n","native video command");commands.clear();
        build();action("Graphics");build();
        for (const auto& control:view.controls) if (control.label=="Field of view") Check(control.minimum==60 && control.maximum==135 && (control.flags&CAINE_CONTROL_LIVE),"live FOV slider range");
        action("Field of view","",200);Check(fov==135,"FOV must apply immediately and clamp to 135");
        build();action("Field of view","",60);build();action("Discard pending changes");Check(fov==60,"discard must not undo already applied FOV");
        build();action("Field of view","",std::numeric_limits<double>::infinity());Check(fov==60,"nonfinite FOV");
        fovWritable=false;action("Field of view","",120);Check(fov==60 && commands.empty(),"FOV save failure");
        fovWritable=true;build();action("Field of view","",120);Check(fov==120,"FOV retry after save failure");commands.clear();
        action("Trilinear texture filtering","",1);build();action("Frame rate limit","",60);build();action("Apply settings");
        Check(commands.size()==3 && commands[0]=="fps_max 60\n" && commands[1]=="mat_trilinear 1\n","native graphics controls");commands.clear();
        caine::GameMenus restored(backend,root,active);
        Check(commands.size()==2 && commands[0]=="mat_trilinear 1\n" && commands[1]=="fps_max 60\n","non-archived graphics restored per profile");commands.clear();
        caine::GameMenus otherProfile(backend,root,root/"Vampire");Check(commands.empty(),"graphics preferences leaked to another profile");
        action("Framework");build();action("Interface scale","",150);Check(caine::MenuScale()==1.5f,"live interface scale");
        build();action("Skip startup logos","",0);Check(commands.empty(),"framework setting dispatched as engine command");
        const auto config=root/"Bin/loader/CAINE/CAINE.ini";
        Check(GetPrivateProfileIntW(L"Startup",L"SkipVideos",1,config.c_str())==0 && GetPrivateProfileIntW(L"Runtime",L"Enabled",0,config.c_str())==1,"framework persistence preserved unrelated settings");
        action("Interface scale","",std::numeric_limits<double>::infinity());Check(caine::MenuScale()==1.5f,"nonfinite preference");
        action("Interface scale","",100);build();
        build();action("Keyboard");build();action("Primary attack","F\";quit",0);Check(commands.empty(),"binding command injection");
        action("Primary attack","ESCAPE",0);Check(commands.empty(),"Escape reserved for cancellation");
        action("Primary attack","F",0);Check(commands.back()=="unbind \"MOUSE1\"\nbind \"F\" \"+attack\"\nhost_writeconfig\n","primary replacement must preserve alternative");commands.clear();bindings[0].key="F";
        build();action("Primary attack","MWHEELUP",1);Check(commands.back()=="unbind \"SPACE\"\nbind \"MWHEELUP\" \"+attack\"\nhost_writeconfig\n","alternative replacement must preserve primary");commands.clear();bindings[1].key="MWHEELUP";
        build();action("Primary attack","",0);Check(commands.back()=="unbind \"F\"\nhost_writeconfig\n","clear only selected binding slot");commands.clear();bindings.erase(bindings.begin());
        Check(caine::BindingKey(WM_KEYDOWN,'F',0)=="F" && caine::BindingKey(WM_KEYDOWN,VK_RETURN,1<<24)=="KP_ENTER" &&
              caine::BindingKey(WM_KEYDOWN,VK_UP,(0x48<<16))=="KP_UPARROW" &&
              caine::BindingKey(WM_MOUSEWHEEL,MAKEWPARAM(0,static_cast<WORD>(-WHEEL_DELTA)),0)=="MWHEELDOWN" &&
              caine::BindingKey(WM_XBUTTONDOWN,MAKEWPARAM(0,XBUTTON2),0)=="MOUSE5" &&
              caine::BindingKey(WM_KEYDOWN,'F',1<<30).empty() && caine::BindingKey(WM_KEYDOWN,VK_ESCAPE,0).empty(),"native capture names, keypad, mouse, repeat and Escape handling");
        menus.Open(caine::GameMenuPage::Save);build();Check(view.controls.size()==4,"active save folder isolation");action("Save name","foo");build();
        Check(!action("Save game") && commands.empty(),"case-insensitive overwrite must confirm");build();Check(Find(view,"Overwrite save")>0,"overwrite confirmation");action("Cancel");build();
        action("Save name","../escape;quit");build();action("Save game");Check(commands.empty(),"unsafe save name");
        Write(active/"save/Foo.sav","JSAVsave sentinel");
        menus.Open(caine::GameMenuPage::Load);build();action("Foo");build();action("Load selected save");Check(commands.empty(),"in-game load must confirm");build();
        Check(action("Load selected save") && commands.back()=="load Foo\n","load dispatch");commands.clear();
        inGame=false;menus.Open(caine::GameMenuPage::Load);build();action("Foo");build();
        Check(action("Load selected save") && commands.back()=="load Foo\n","main menu load must not warn about unsaved progress");commands.clear();
        menus.Open(caine::GameMenuPage::Load);build();action("Foo");build();std::filesystem::remove(active/"save/Foo.sav");
        Check(!action("Load selected save") && commands.empty(),"deleted selection must not reach engine");
        inGame=true;menus.Open(caine::GameMenuPage::Save);build();action("Save name","Foo");build();Write(active/"save/Foo.sav","JSAVsave sentinel");
        Check(!action("Save game") && commands.empty(),"save created after enumeration must require overwrite confirmation");build();action("Cancel");
        menus.Open(caine::GameMenuPage::Credits);build();Check(view.pageTitle=="Credits","credits route");
        bool credit=false;for (const auto& control:view.controls) credit|=control.label.find("Local credits")!=std::string::npos;Check(credit,"credits read from installation");
        std::ifstream save(active/"save/Foo.sav");std::string sentinel;std::getline(save,sentinel);Check(sentinel=="JSAVsave sentinel","browser changed save bytes");
        std::cout<<"CAINE_GAME_MENUS_OK: staged settings, clamping, inversion, video modes, bindings, active save isolation, confirmation, command validation and installed credits\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
