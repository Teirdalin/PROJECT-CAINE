#include <caine/melee_camera.hpp>
#include <caine/preferences.hpp>
#include <array>
#include <cstring>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace caine { void TestMeleeOriginals(void*,void*,void*); }
namespace {
unsigned views{},thinks{},alphas{};
unsigned holsters{};
int movementType=2;
void* currentWeapon{};
void Check(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class T> void Put(void* p,size_t o,T value) { memcpy(static_cast<uint8_t*>(p)+o,&value,sizeof(value)); }
void* __fastcall Weapon(void*,void*) { return currentWeapon; }
bool __fastcall CvarFlag(void*,void*) { return false; }
int __fastcall MoveType(void*,void*) { return movementType; }
void __fastcall Think(void*,void*) { ++thinks; }
void __fastcall View(void*,void*,void* view) { ++views;Put(view,0x38,123.f);Put(view,0x50,456.f); }
float __fastcall Alpha(void*,void*) { ++alphas;return .25f; }
void __fastcall Command(void*,void*,const char* command,bool) {
    Check(std::string(command)=="inven_holster","unexpected native camera command");++holsters;
}
__declspec(naked) void __cdecl ToggleEntry(void*,void*,void*) {
    __asm {
        mov eax,dword ptr [esp+4]
        mov ecx,dword ptr [esp+8]
        mov edx,dword ptr [esp+12]
        push ebp
        push esi
        push edi
        mov esi,ecx
        mov ebp,edx
        jmp eax
    }
}
}
int wmain(int argc,wchar_t** argv) {
 try {
    Check(argc==2 || argc==3,"installed client DLL [conflict]");
    const auto image=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
    Check(image!=nullptr,"map installed client");const auto module=caine::Module::Inspect(image);
    auto input=module.base+0x2ea6c8;
    Put(input,0,module.base+0x224d4c); // native constructors are intentionally skipped
    DWORD pointerProtection{};
    Check(VirtualProtect(module.base+0x27ba58,4,PAGE_READWRITE,&pointerProtection)!=FALSE,"fixture input pointer protection");
    Put(module.base,0x27ba58,static_cast<void*>(input));
    auto wrong=module;wrong.sha256="unsupported";
    Check(!caine::InstallMeleeCamera(wrong,[](const auto&){}),"unknown binary accepted");
    if (argc==3) {
        DWORD old{};auto address=module.base+0xffb00;
        Check(VirtualProtect(address,1,PAGE_EXECUTE_READWRITE,&old)!=FALSE,"fixture patch");
        *address=0xcc;DWORD ignored{};VirtualProtect(address,1,old,&ignored);
        Check(!caine::InstallMeleeCamera(module,[](const auto&) {}) && !caine::MeleeCameraAvailable(),"conflicting camera hook accepted");
        Check(*address==0xcc && module.base[0x9c250]==0x56 && module.base[0xff130]==0x83,"partial hook batch changed game code");
        std::cout<<"CAINE_MELEE_CONFLICT_OK\n";return 0;
    }
    Check(caine::InstallMeleeCamera(module,[](const auto& s){std::cout<<s<<'\n';}),"production hook profile");
    caine::TestMeleeOriginals(reinterpret_cast<void*>(Think),reinterpret_cast<void*>(View),reinterpret_cast<void*>(Alpha));
    std::array<uint8_t,0x1700> player{};std::array<void*,150> playerTable{};
    playerTable[0x250/4]=reinterpret_cast<void*>(Weapon);Put(player.data(),0,playerTable.data());
    std::array<void*,9> movementTable{};movementTable[8]=reinterpret_cast<void*>(MoveType);
    Put(player.data(),0xc,movementTable.data());
    Put(module.base,0x4a0d50,player.data());
    std::array<uint8_t,0x980> weapon{};currentWeapon=weapon.data();Put(weapon.data(),0x95e,uint16_t(0));
    std::vector<uint8_t> definitions(0x5f330);auto data=definitions.data()+12;
    Put(module.base,0x61993c,definitions.data());Put(data,0x2440,16);
    memcpy(data+0x2450,"weapon_melee hidden",20);
    std::array<void*,2> cvarTable{};cvarTable[1]=reinterpret_cast<void*>(CvarFlag);
    std::array<uint8_t,0x30> cvar{};Put(cvar.data(),0,cvarTable.data());Put(cvar.data(),0x2c,1);
    Put(module.base,0x49d914,cvar.data());
    std::array<void*,28> engineTable{};engineTable[27]=reinterpret_cast<void*>(Command);
    auto enginePointer=engineTable.data();Put(module.base,0x4a57d4,&enginePointer);
    const auto config=std::filesystem::temp_directory_path()/("CAINE-melee-"+std::to_string(GetCurrentProcessId())+".ini");
    { std::ofstream(config)<<"[Gameplay]\nFirstPersonMelee=0\nMeleeBodyCamera=1\n[Unrelated]\nSentinel=73\n"; }
    caine::InitializeFrameworkPreferences(config);
    const auto& options=caine::GameplayOptions();
    auto set=[&](size_t i,double value){Check(caine::WriteFrameworkOption(config,options[i],value),"persist option");};
    using Void=void(__thiscall*)(void*);using ViewCall=void(__thiscall*)(void*,void*);using AlphaCall=float(__thiscall*)(void*);
    const auto think=reinterpret_cast<Void>(module.base+0xff130),change=reinterpret_cast<Void>(module.base+0x9c250);
    const auto view=reinterpret_cast<ViewCall>(module.base+0xffb00);const auto alpha=reinterpret_cast<AlphaCall>(module.base+0xffaf0);
    std::array<uint8_t,0x94> setup{};Put(setup.data(),0x38,10.f);Put(setup.data(),0x50,20.f);
    think(input);Check(input[0xf8]==0,"disabled startup re-evaluated the native camera");
    change(currentWeapon);Check(input[0xf8]==1,"off changed original melee camera");
    view(input,setup.data());Check(views==1 && alpha(input)==.25f,"disabled changed rendering");
    set(0,1);think(input);Put(setup.data(),0x38,10.f);Put(setup.data(),0x50,20.f);
    view(input,setup.data());Check(views==1 && alpha(input)==1.f,"body FPV did not preserve eye view/visible model");
    Check(*reinterpret_cast<float*>(setup.data()+0x38)==10.f && *reinterpret_cast<float*>(setup.data()+0x50)==20.f,"eye origin or aim changed");
    for (const auto offset:{0x88u,0x15e8u,0x16e0u}) {
        Put(player.data(),offset,1);view(input,setup.data());Check(alpha(input)==.25f,"script camera priority");Put(player.data(),offset,0);
    }
    Put(player.data(),0x16f0,1);Check(alpha(input)==.25f,"native dialogue camera priority");Put(player.data(),0x16f0,0);
    for(const auto bits:{0x10u,0x400u}) { Put(player.data(),0x16f4,bits);Check(alpha(input)==.25f,"special character camera priority"); }
    Put(player.data(),0x16f4,0u);movementType=10;Check(alpha(input)==.25f,"ladder camera priority");movementType=2;
    for (const auto offset:{0x138u,0x100u}) {
        Put(input,offset,1.f);Check(alpha(input)==.25f,"script camera blend priority");Put(input,offset,0.f);
    }
    input[0xf0]=1;Check(alpha(input)==.25f,"manual third person ignored");input[0xf0]=0;
    input[0xf0]=1;ToggleEntry(module.base+0xff87f,input,player.data());
    Check(!holsters && input[0xf0]==1 && input[0xf8]==1,"body camera toggle holstered melee or erased preference");input[0xf0]=0;
    Put(data,0x2440,2);Check(alpha(input)==.25f,"ranged weapon overridden");Put(data,0x2440,16);
    memcpy(data+0x2450,"weapon_ranged",14);Check(alpha(input)==.25f,"forced ranged camera overridden");
    memcpy(data+0x2450,"weapon_melee hidden",20);
    currentWeapon=nullptr;Check(alpha(input)==.25f,"weapon unload retained state");currentWeapon=weapon.data();
    Put(module.base,0x4a0d50,static_cast<void*>(nullptr));Check(alpha(input)==.25f,"player unload retained state");Put(module.base,0x4a0d50,player.data());
    std::thread other([&]{Check(alpha(input)==.25f,"cross-thread native access");});other.join();
    set(1,0);think(input);Check(input[0xf8]==0 && input[0xf0]==0,"native first person not applied or preference changed");
    change(currentWeapon);Check(input[0xf8]==0,"melee equip forced third person in native mode");
    input[0xf0]=1;change(currentWeapon);Check(input[0xf8]==1,"manual third-person camera not retained");input[0xf0]=0;
    input[0xf0]=1;ToggleEntry(module.base+0xff87f,input,player.data());
    Check(!holsters && input[0xf0]==1 && input[0xf8]==1,"native camera toggle holstered melee");input[0xf0]=0;
    Put(player.data(),0x16e0,1);change(currentWeapon);Check(input[0xf8]==1,"scripted camera flag suppressed");
    set(0,0);think(input);Check(input[0xf8]==1,"preference change rewrote cinematic camera");Put(player.data(),0x16e0,0);think(input);
    Check(input[0xf8]==1 && !caine::FirstPersonMeleeEnabled(),"disable did not restore original melee camera");
    ToggleEntry(module.base+0xff87f,input,player.data());
    Check(holsters==1 && input[0xf0]==0 && input[0xf8]==0,"disabled camera toggle changed vanilla holster behavior");
    set(0,1);set(1,1);think(input);Check(alpha(input)==1.f,"body mode recovery");
    caine::InitializeFrameworkPreferences(config);Check(caine::FirstPersonMeleeEnabled() && caine::MeleeBodyCamera(),"restart persistence");
    Check(GetPrivateProfileIntW(L"Unrelated",L"Sentinel",0,config.c_str())==73,"unrelated config changed");
    Check(!caine::WriteFrameworkOption(config,options[0],std::numeric_limits<double>::quiet_NaN()),"nonfinite camera setting");
    std::filesystem::remove(config);std::cout<<"CAINE_MELEE_NATIVE_OK: installed-binary hooks, configuration, camera priority and restoration\n";
    return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
