#include <caine/game_menu.hpp>
#include <caine/key_input.hpp>
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
        Write(root/"Vampire/scripts/kb_act.lst","\"+attack\" \"Primary attack\"\n\"save quick\" \"Quick save\"\n");
        Vpk(root/"Vampire/pack000.vpk","vdata/system/credits.txt","@MainMenu\nTroika Games\n#ignored\n");
        Vpk(active/"pack000.vpk","vdata/system/credits.txt","@MainMenu\nPatch credits\n");
        Check(caine::ReadGameMenuResource(root,active,"vdata/system/credits.txt").find("Patch credits")!=std::string::npos,"VPK layer precedence");
        Write(active/"vdata/system/credits.txt","@MainMenu\nLocal credits\n");
        Check(caine::ReadGameMenuResource(root,active,"vdata/system/credits.txt").find("Local credits")!=std::string::npos,"loose file precedence");
        Check(caine::ReadGameMenuResource(root,active,"../secret").empty(),"resource traversal");
        Write(active/"save/Foo.sav","save sentinel");Write(root/"Vampire/save/wrong-layer.sav","another sentinel");
        std::vector<std::string> commands;
        std::map<std::string,double> values{{"volume",.8},{"m_pitch",.05},{"in_mlook",1}};
        caine::GameMenuBackend backend;
        backend.read=[&](const char* name)->std::optional<double>{const auto found=values.find(name);return found==values.end()?std::nullopt:std::optional<double>(found->second);};
        backend.command=[&](const std::string& command){commands.push_back(command);};
        backend.currentMode=[] { return caine::VideoMode{800,600,32}; };
        backend.modes=[] { return std::vector<caine::VideoMode>{{800,600,32},{1920,1080,32}}; };
        std::vector<caine::KeyBinding> bindings{{"MOUSE1","+attack"},{"SPACE","+attack"},{"ESCAPE","cancelselect"}};
        backend.bindings=[&] { return bindings; };
        backend.keyNames=[] { return std::vector<std::string>{"MOUSE1","SPACE","F","MWHEELUP","ESCAPE"}; };
        caine::GameMenus menus(backend,root,active);caine::MenuView view;
        auto build=[&] { view={};menus.Build(view); };
        auto action=[&](const std::string& label,const std::string& text="",double number=0) { return menus.Action(Find(view,label),text,number); };
        menus.Open(caine::GameMenuPage::Settings);build();action("Sound effects volume","",-100);Check(commands.empty(),"settings committed before Apply");build();action("Apply settings");
        Check(commands.size()==2 && commands[0]=="volume 0\n" && commands[1]=="host_writeconfig\n","bounded settings and persistence");commands.clear();
        build();action("Mouse");build();action("Invert vertical mouse","",1);build();action("Apply settings");
        Check(commands.front()=="m_pitch -0.05\n","invert preserves sensitivity magnitude");commands.clear();
        build();action("Invert vertical mouse","",0);build();action("Discard pending changes");build();Check(commands.empty(),"discard changed native settings");
        action("Video");build();action("Resolution","",1);build();action("Apply settings");
        Check(commands.front()=="_setvideomode 1920 1080 32\n","native video command");commands.clear();
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
        menus.Open(caine::GameMenuPage::Load);build();action("Foo");build();action("Load selected save");Check(commands.empty(),"load must confirm");build();
        Check(action("Load selected save") && commands.back()=="load Foo\n","load dispatch");commands.clear();
        menus.Open(caine::GameMenuPage::Credits);build();Check(view.pageTitle=="Credits","credits route");
        bool credit=false;for (const auto& control:view.controls) credit|=control.label.find("Local credits")!=std::string::npos;Check(credit,"credits read from installation");
        std::ifstream save(active/"save/Foo.sav");std::string sentinel;std::getline(save,sentinel);Check(sentinel=="save sentinel","browser changed save bytes");
        std::cout<<"CAINE_GAME_MENUS_OK: staged settings, clamping, inversion, video modes, bindings, active save isolation, confirmation, command validation and installed credits\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
