#include <caine/melee_camera.hpp>
#include <caine/preferences.hpp>
#include <array>
#include <cmath>
#include <cstring>
#include <cwchar>
#include <fstream>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>
namespace caine { void TestMeleeOriginals(void*,void*,void*);void TestMeleeBoneSetup(void*);void TestMeleeDraw(void*); }
namespace {
unsigned views{},thinks{},alphas{};
unsigned holsters{};
int movementType=2;
void* currentWeapon{};
void* expectedRenderable{};
bool setupSucceeds=true;
unsigned setups{},draws{};
float* expectedMatrices{};
void* nativeBoneEntry{};
bool testDrawPose{},drawException{},drawBones=true;
std::array<float,36> renderOutput{};
bool Near(float a,float b) { return std::abs(a-b)<.002f; }
struct NativeView {
    std::array<uint8_t,0xa4> renderer{};
    uint8_t* data() { return renderer.data()+0x10; }
};
void Check(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
template<class T> void Put(void* p,size_t o,T value) { memcpy(static_cast<uint8_t*>(p)+o,&value,sizeof(value)); }
void* __fastcall Weapon(void*,void*) { return currentWeapon; }
bool __fastcall CvarFlag(void*,void*) { return false; }
bool __fastcall ModelReady(void*,void*) { return true; }
bool __fastcall SetupBones(void* self,void*,void* output,int maximum,int mask,float time,int force) {
    Check(self==expectedRenderable && ((!output && maximum==-1) || (output==renderOutput.data() && maximum==3)) && mask==0x100 && time==1.25f && force==0,"native bone setup ABI");
    ++setups;
    if (setupSucceeds && output) memcpy(output,expectedMatrices,144);
    return setupSucceeds;
}
int __fastcall Draw(void* self,void*,int flags,int instance) {
    ++draws;Check(flags==1 && instance==7,"draw ABI arguments");
    if (testDrawPose) {
        Check(self==static_cast<uint8_t*>(expectedRenderable)-4,"local draw entity");
        Check(Near(expectedMatrices[12],1) && Near(expectedMatrices[14],0) && Near(expectedMatrices[21],-1),"visible head did not follow camera pitch");
        // Head pivot stays animated; child is rotated around that same pivot.
        Check(Near(expectedMatrices[15],60) && Near(expectedMatrices[19],100) && Near(expectedMatrices[23],68),"head pivot drift");
        Check(Near(expectedMatrices[27],60) && Near(expectedMatrices[31],101) && Near(expectedMatrices[35],68),"head child did not follow");
        Check(expectedMatrices[0]==1 && expectedMatrices[5]==1 && expectedMatrices[10]==1 && expectedMatrices[3]==80,"body/root changed");
        if (drawBones) {
            using Setup=bool(__thiscall*)(void*,void*,int,int,float,int);
            const auto setup=reinterpret_cast<Setup>(nativeBoneEntry);
            Check(setup(expectedRenderable,renderOutput.data(),3,0x100,1.25f,0),"render bone setup");
            Check(Near(renderOutput[21],-1) && Near(renderOutput[31],101),"renderer output missed head tracking");
            Check(setup(expectedRenderable,renderOutput.data(),3,0x100,1.25f,0),"repeat render bone setup");
            Check(Near(renderOutput[31],101),"repeated render accumulated head transform");
        }
        if (drawException) RaiseException(0xe012ca1e,0,0,nullptr);
    }
    return 42;
}
bool ExceptionDraw(void* entry,void* player) {
    __try { using Call=int(__thiscall*)(void*,int,int);reinterpret_cast<Call>(entry)(player,1,7); }
    __except(GetExceptionCode()==0xe012ca1e?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH) { return true; }
    return false;
}
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
__declspec(naked) void __cdecl InitializeNearPlanes(void*,void*) {
    __asm {
        mov eax,dword ptr [esp+4]
        mov ecx,dword ptr [esp+8]
        push esi
        mov esi,ecx
        call eax
        pop esi
        ret
    }
}
}
int wmain(int argc,wchar_t** argv) {
 try {
    Check(argc==2 || argc==3,"installed client DLL [conflict|clip-conflict|draw-conflict|bones-conflict]");
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
        const bool clipConflict=std::wcscmp(argv[2],L"clip-conflict")==0;
        const bool drawConflict=std::wcscmp(argv[2],L"draw-conflict")==0;
        const bool bonesConflict=std::wcscmp(argv[2],L"bones-conflict")==0;
        DWORD old{};auto address=module.base+(clipConflict?0x19179f:drawConflict?0x92970:bonesConflict?0x919c0:0xffb00);
        Check(VirtualProtect(address,1,PAGE_EXECUTE_READWRITE,&old)!=FALSE,"fixture patch");
        *address=0xcc;DWORD ignored{};VirtualProtect(address,1,old,&ignored);
        Check(!caine::InstallMeleeCamera(module,[](const auto&) {}) && !caine::MeleeCameraAvailable(),"conflicting camera hook accepted");
        Check(*address==0xcc && module.base[0x9c250]==0x56 && module.base[0xff130]==0x83,"partial hook batch changed game code");
        std::cout<<"CAINE_MELEE_CONFLICT_OK\n";return 0;
    }
    Check(caine::InstallMeleeCamera(module,[](const auto& s){std::cout<<s<<'\n';}),"production hook profile");
    // Execute the installed builder's two near-plane writes, with a controlled
    // return before its unrelated engine calls. Only this owned mapped image
    // is changed; no game file or running game is touched.
    DWORD clipProtection{};
    Check(VirtualProtect(module.base+0x1917ad,1,PAGE_EXECUTE_READWRITE,&clipProtection)!=FALSE,"fixture clip continuation");
    module.base[0x1917ad]=0xc3;
    DWORD ignoredClip{};VirtualProtect(module.base+0x1917ad,1,clipProtection,&ignoredClip);
    FlushInstructionCache(GetCurrentProcess(),module.base+0x1917ad,1);
    caine::TestMeleeOriginals(reinterpret_cast<void*>(Think),reinterpret_cast<void*>(View),reinterpret_cast<void*>(Alpha));
    std::array<uint8_t,0x1700> player{};std::array<void*,150> playerTable{};
    playerTable[0x250/4]=reinterpret_cast<void*>(Weapon);Put(player.data(),0,playerTable.data());
    playerTable[0x11c/4]=reinterpret_cast<void*>(ModelReady);
    std::array<void*,16> renderableTable{};renderableTable[15]=reinterpret_cast<void*>(SetupBones);
    Put(player.data(),4,renderableTable.data());expectedRenderable=player.data()+4;
    caine::TestMeleeBoneSetup(reinterpret_cast<void*>(SetupBones));
    caine::TestMeleeDraw(reinterpret_cast<void*>(Draw));nativeBoneEntry=module.base+0x919c0;
    std::array<uint8_t,0x600> header{};
    Put(header.data(),0,uint32_t(0x54534449));Put(header.data(),4,uint32_t(0x9e3));Put(header.data(),0x8c,uint32_t(header.size()));
    Put(header.data(),0xf0,3);Put(header.data(),0xf4,0x1a8);
    const size_t headRow=0x1a8+160;
    Put(header.data(),headRow,int(0x3a0-headRow));memcpy(header.data()+0x3a0,"Bip01 Head",11);
    Put(header.data(),0x1ac,-1);Put(header.data(),headRow+4,0);
    const size_t childRow=headRow+160;Put(header.data(),childRow+4,1);Put(header.data(),childRow+0x88,0x100);
    Put(header.data(),headRow+0x88,0x100);
    const std::array<float,12> bind{0,0,1,0, 0,-1,0,0, 1,0,0,0};
    memcpy(header.data()+headRow+0x58,bind.data(),48);
    Put(header.data(),0x148,1);Put(header.data(),0x14c,0x400);
    Put(header.data(),0x400,0x80);memcpy(header.data()+0x480,"eyes",5);Put(header.data(),0x408,1);
    Put(header.data(),0x418,4.5f);Put(header.data(),0x428,2.2f);
    Put(player.data(),0x6ac,header.data());
    std::array<float,36> matrices{};expectedMatrices=matrices.data();matrices[12]=matrices[17]=matrices[22]=1.f;
    matrices[24]=matrices[29]=matrices[34]=1.f;matrices[27]=60.f;matrices[31]=100.f;matrices[35]=69.f;
    matrices[15]=50.f;matrices[19]=100.f;matrices[23]=75.f;Put(player.data(),0x6e8,matrices.data());
    std::array<float,4> globals{};globals[3]=1.25f;
    DWORD globalsProtection{};Check(VirtualProtect(module.base+0x2b8494,4,PAGE_READWRITE,&globalsProtection)!=FALSE,"fixture globals protection");
    Put(module.base,0x2b8494,globals.data());
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
    const auto nativeView=reinterpret_cast<ViewCall>(module.base+0xffb00);const auto alpha=reinterpret_cast<AlphaCall>(module.base+0xffaf0);
    NativeView setup;Put(setup.data(),0x38,10.f);Put(setup.data(),0x50,0.f);
    Put(setup.data(),0x60,28400.f);Put(setup.data(),0x68,28400.f);
    const auto view=[&](void* self,void* data) {
        InitializeNearPlanes(module.base+0x19179f,setup.renderer.data());
        nativeView(self,data);
    };
    const auto plane=[&](size_t offset) { float value{};memcpy(&value,setup.data()+offset,4);return value; };
    think(input);Check(input[0xf8]==0,"disabled startup re-evaluated the native camera");
    change(currentWeapon);Check(input[0xf8]==1,"off changed original melee camera");
    view(input,setup.data());Check(views==1 && alpha(input)==.25f,"disabled changed rendering");
    Check(plane(0x5c)==8.f && plane(0x64)==1.f,"native clip defaults or disabled restoration");
    set(0,1);think(input);Put(setup.data(),0x38,10.f);Put(setup.data(),0x50,0.f);
    view(input,setup.data());Check(views==1 && alpha(input)==1.f && setups==1,"head FPV did not refresh native bones/visible model");
    Check(plane(0x5c)==1.f && plane(0x60)==28400.f && plane(0x64)==1.f && plane(0x68)==28400.f,"body near clip changed wrong projection fields");
    auto origin=[&](size_t i){return *reinterpret_cast<float*>(setup.data()+0x38+i*4);};
    Check(Near(origin(0),54.7f) && Near(origin(1),100.f) && Near(origin(2),79.5f) && *reinterpret_cast<float*>(setup.data()+0x50)==0.f,"head eye attachment transform or mouse aim");
    matrices[15]=60.f;matrices[23]=68.f;view(input,setup.data());
    Check(Near(origin(0),64.7f) && Near(origin(2),72.5f) && setups==2,"camera did not follow animated head translation");
    // Animated head orientation must not drag mouse aim or put the nose back into view.
    matrices[12]=0.f;matrices[13]=-1.f;matrices[16]=1.f;matrices[17]=0.f;view(input,setup.data());
    Check(Near(origin(0),64.7f) && Near(origin(1),100.f),"animation dragged the aim-aligned eye offset");
    matrices[12]=matrices[17]=1.f;matrices[13]=matrices[16]=0.f;
    Put(header.data(),0x148,0);view(input,setup.data());Check(Near(origin(0),64.7f),"model without eyes lost head camera");Put(header.data(),0x148,1);
    setupSucceeds=false;view(input,setup.data());Check(alpha(input)==.25f,"failed bones retained body FPV");setupSucceeds=true;
    matrices[15]=std::numeric_limits<float>::quiet_NaN();view(input,setup.data());Check(alpha(input)==.25f,"invalid transform applied");matrices[15]=60.f;
    Put(header.data(),0xf0,300);view(input,setup.data());Check(alpha(input)==.25f,"invalid model count accepted");Put(header.data(),0xf0,3);
    Put(player.data(),0x6ac,static_cast<void*>(nullptr));view(input,setup.data());Check(alpha(input)==.25f,"missing model retained head camera");Put(player.data(),0x6ac,header.data());
    Put(setup.data(),0x50,0.f);
    // The next model puts its head at index 0: no old bone ID may survive.
    std::array<uint8_t,0x600> changed=header;Put(changed.data(),0x1a8,int(0x3a0-0x1a8));Put(changed.data(),0x1ac,-1);memcpy(changed.data()+0x1a8+0x58,bind.data(),48);Put(changed.data(),0x1a8+0x88,0x100);Put(changed.data(),0xf0,1);Put(changed.data(),0x408,0);
    matrices[0]=matrices[5]=matrices[10]=1.f;matrices[3]=80.f;matrices[7]=90.f;matrices[11]=65.f;
    Put(player.data(),0x6ac,changed.data());view(input,setup.data());Check(Near(origin(0),84.7f) && Near(origin(2),69.5f),"model change retained stale head index");Put(player.data(),0x6ac,header.data());
    // Pitch/yaw use native camera angles, never entity/combat angles.
    Put(setup.data(),0x50,90.f);Put(setup.data(),0x54,0.f);view(input,setup.data());
    Check(Near(origin(0),64.5f) && Near(origin(1),100.f) && Near(origin(2),63.3f),"look-down camera did not stay at the posed nose");
    const auto unposed=matrices;
    const auto draw=reinterpret_cast<int(__thiscall*)(void*,int,int)>(module.base+0x92970);
    testDrawPose=true;
    Check(draw(player.data(),1,7)==42,"native draw result lost");
    Check(memcmp(matrices.data(),unposed.data(),sizeof(matrices))==0,"native cache not restored after drawing");
    // Cached render without another SetupBones call still gets a posed head.
    drawBones=false;Check(draw(player.data(),1,7)==42,"cached draw result");drawBones=true;
    Check(memcmp(matrices.data(),unposed.data(),sizeof(matrices))==0,"cached draw leaked head pose");
    drawException=true;Check(ExceptionDraw(module.base+0x92970,player.data()),"draw exception not propagated");drawException=false;
    Check(memcmp(matrices.data(),unposed.data(),sizeof(matrices))==0,"exception leaked head pose");
    testDrawPose=false;
    using SetupCall=bool(__thiscall*)(void*,void*,int,int,float,int);
    Check(reinterpret_cast<SetupCall>(nativeBoneEntry)(expectedRenderable,renderOutput.data(),3,0x100,1.25f,0),"ordinary bone query");
    Check(memcmp(renderOutput.data(),unposed.data(),sizeof(matrices))==0,"ordinary gameplay query received posed bones");
    Put(setup.data(),0x50,0.f);Put(setup.data(),0x54,90.f);view(input,setup.data());
    Check(Near(origin(0),60.f) && Near(origin(1),104.7f) && Near(origin(2),72.5f),"yaw did not move head camera with mouse");
    Put(setup.data(),0x54,0.f);view(input,setup.data());
    Put(header.data(),headRow+4,1);view(input,setup.data());Check(alpha(input)==.25f,"cyclic skeleton accepted");Put(header.data(),headRow+4,0);
    Put(header.data(),childRow+4,9);view(input,setup.data());Check(alpha(input)==.25f,"out-of-range bone parent accepted");Put(header.data(),childRow+4,1);
    Put(header.data(),headRow+0x58,2.f);view(input,setup.data());Check(alpha(input)==.25f,"invalid inverse bind accepted");memcpy(header.data()+headRow+0x58,bind.data(),48);
    Put(setup.data(),0x50,std::numeric_limits<float>::quiet_NaN());view(input,setup.data());Check(alpha(input)==.25f,"invalid aim accepted");Put(setup.data(),0x50,0.f);
    view(input,setup.data());globals[1]=1.f;Check(draw(player.data(),1,7)==42 && memcmp(matrices.data(),unposed.data(),sizeof(matrices))==0,"stale camera frame posed a model");globals[1]=0.f;
    std::array<uint8_t,0x1700> npc{};Check(draw(npc.data(),1,7)==42,"NPC draw result");
    Check(memcmp(matrices.data(),unposed.data(),sizeof(matrices))==0,"NPC draw changed player pose");
    Put(setup.data(),0x5c,.25f);nativeView(input,setup.data());Check(plane(0x5c)==.25f,"tighter existing near clip increased");
    for (const float invalid:{0.f,-1.f,std::numeric_limits<float>::quiet_NaN(),std::numeric_limits<float>::infinity()}) {
        Put(setup.data(),0x5c,invalid);nativeView(input,setup.data());
        Check(std::isnan(invalid)?std::isnan(plane(0x5c)):plane(0x5c)==invalid,"invalid near clip overwritten");
    }
    Put(setup.data(),0x5c,8.f);Put(setup.data(),0x60,4.f);nativeView(input,setup.data());Check(plane(0x5c)==8.f,"unordered planes changed");
    Put(setup.data(),0x60,std::numeric_limits<float>::infinity());nativeView(input,setup.data());Check(plane(0x5c)==8.f,"nonfinite far plane changed near clip");Put(setup.data(),0x60,28400.f);
    setupSucceeds=false;view(input,setup.data());Check(plane(0x5c)==8.f,"failed head view retained body near clip");setupSucceeds=true;
    for (const auto offset:{0x88u,0x15e8u,0x16e0u}) {
        Put(player.data(),offset,1);view(input,setup.data());Check(alpha(input)==.25f && plane(0x5c)==8.f,"script camera priority");Put(player.data(),offset,0);
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
    view(input,setup.data());Check(plane(0x5c)==8.f && plane(0x64)==1.f,"native first person clip defaults changed");
    change(currentWeapon);Check(input[0xf8]==0,"melee equip forced third person in native mode");
    input[0xf0]=1;change(currentWeapon);Check(input[0xf8]==1,"manual third-person camera not retained");input[0xf0]=0;
    input[0xf0]=1;ToggleEntry(module.base+0xff87f,input,player.data());
    Check(!holsters && input[0xf0]==1 && input[0xf8]==1,"native camera toggle holstered melee");input[0xf0]=0;
    Put(player.data(),0x16e0,1);change(currentWeapon);Check(input[0xf8]==1,"scripted camera flag suppressed");
    set(0,0);think(input);Check(input[0xf8]==1,"preference change rewrote cinematic camera");Put(player.data(),0x16e0,0);think(input);
    Check(input[0xf8]==1 && !caine::FirstPersonMeleeEnabled(),"disable did not restore original melee camera");
    ToggleEntry(module.base+0xff87f,input,player.data());
    Check(holsters==1 && input[0xf0]==0 && input[0xf8]==0,"disabled camera toggle changed vanilla holster behavior");
    set(0,1);set(1,1);think(input);view(input,setup.data());Check(alpha(input)==1.f,"body mode recovery");
    caine::InitializeFrameworkPreferences(config);Check(caine::FirstPersonMeleeEnabled() && caine::MeleeBodyCamera(),"restart persistence");
    Check(GetPrivateProfileIntW(L"Unrelated",L"Sentinel",0,config.c_str())==73,"unrelated config changed");
    Check(!caine::WriteFrameworkOption(config,options[0],std::numeric_limits<double>::quiet_NaN()),"nonfinite camera setting");
    std::filesystem::remove(config);std::cout<<"CAINE_MELEE_NATIVE_OK: installed-binary hooks, configuration, camera priority and restoration\n";
    return 0;
 } catch(const std::exception& e) {std::cerr<<e.what()<<'\n';return 1;}
}
