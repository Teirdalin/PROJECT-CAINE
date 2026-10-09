#include <chrono>
#include <caine/mod_loader.hpp>
#include <fstream>
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Write(const std::filesystem::path& path,const std::string& text) { std::ofstream(path)<<text; }
int wmain(int argc,wchar_t** argv) {
    try {
        if (argc != 4) return 1;
        const std::wstring mode=argv[3];
        const auto root=std::filesystem::temp_directory_path()/(L"caine-mods-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        const auto folder=root/L"mods"/L"TestMod";
        std::filesystem::create_directories(folder);
        std::filesystem::copy_file(argv[2],folder/L"TestMod.dll");
        const auto module=LoadLibraryW(argv[1]); Check(module!=nullptr,"fixture load");
        auto add=reinterpret_cast<int(__cdecl*)(int)>(GetProcAddress(module,"FixtureAdd"));
        Check(add && add(5)==22,"fixture baseline");
        std::string xml=R"(<mod schemaVersion="1" id="test-mod" name="Test &amp; Mod" version="1.0.0" author="Example" dll="TestMod.dll" config="TestMod.cfg" logo="logo.png" minimumFramework="0.3.0"><description>Example mod.</description></mod>)";
        if (mode==L"path") xml.replace(xml.find("TestMod.dll"),11,"../evil.dll");
        if (mode==L"future") xml.replace(xml.find("0.3.0"),5,"9.0.0");
        if (mode==L"dtd") xml="<!DOCTYPE mod [<!ENTITY test SYSTEM 'file:///C:/Windows/win.ini'>]>"+xml;
        if (mode==L"malformed") xml.pop_back();
        if (mode==L"abi") SetEnvironmentVariableW(L"CAINE_TEST_BAD_ABI",L"1");
        if (mode==L"legacy") SetEnvironmentVariableW(L"CAINE_TEST_OLD_DESCRIPTOR",L"1");
        Write(folder/L"TestMod.xml",xml);
        Write(folder/L"TestMod.cfg",std::string("[Mod]\nEnabled=")+(mode==L"disabled"?"0":"1")+"\n[Example]\nKeep=42\n");
        if (mode==L"duplicate") {
            const auto duplicate=root/L"mods"/L"Duplicate";
            std::filesystem::create_directories(duplicate);
            std::filesystem::copy_file(argv[2],duplicate/L"TestMod.dll");
            Write(duplicate/L"Duplicate.xml",xml);
        }
        std::string logs;
        caine::LoadMods(root/L"mods",root,[&](const std::string& line){logs+=line+"\n";});
        auto mods=caine::ModCatalog();
        Check(mods.size()==(mode==L"duplicate" ? 2u : 1u),"catalog size");
        if (mode==L"success" || mode==L"legacy") {
            Check(mods[0].active && mods[0].name=="Test & Mod","metadata/registration");
            Check(add(5)==122,"guarded hook through public ABI");
            caine::TickMods(); Check(logs.find("FIXTURE_MOD_TICKED")!=std::string::npos,"tick");
            std::string row;
            CaineMenuV1 menu{sizeof(CaineMenuV1),&row,[](void* context,const char* text,uint32_t){*static_cast<std::string*>(context)=text;}};
            Check(caine::ModMenu("test-mod",&menu,CAINE_MENU_BUILD,0) && row=="Fixture settings","configuration extension");
            std::string error; Check(caine::SetModEnabled("test-mod",false,error),"toggle");
            Check(!caine::ReadModInfo(folder).enabled && caine::ModCatalog()[0].active && add(5)==122,"restart semantics");
            Check(GetPrivateProfileIntW(L"Example",L"Keep",0,(folder/L"TestMod.cfg").c_str())==42,"config preservation");
        } else if (mode==L"duplicate") {
            Check(mods[0].active && !mods[1].active && mods[0].id!=mods[1].id && mods[1].config.empty(),"duplicate catalog isolation");
            Check(add(5)==122,"duplicate must not repatch original");
        } else {
            Check(!mods[0].active && add(5)==22,"rejected/disabled mod must not hook");
            if (mode==L"disabled") Check(logs.find("CAINE_MOD_DISABLED")!=std::string::npos,"disabled");
            else Check(logs.find("CAINE_MOD_REJECTED")!=std::string::npos,"rejected");
        }
        std::cout<<"CAINE_MOD_API_OK\n"; return 0;
    } catch(const std::exception& e) { std::cerr<<e.what()<<"\n"; return 2; }
}
