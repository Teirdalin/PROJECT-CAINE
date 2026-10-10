#include <caine/melee_camera.hpp>
#include <caine/preferences.hpp>
#include <caine/logging.hpp>
#include <atomic>
#include <cstring>

namespace caine {
namespace {
constexpr char ClientHash[]="9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01";
uint8_t* base{};
void* input{};
std::atomic<bool> ready{};
std::atomic<DWORD> cameraThread{};
void(__thiscall* weaponSwitch)(void*){};
void(__thiscall* cameraThink)(void*){};
void(__thiscall* cameraView)(void*,void*){};
float(__thiscall* modelAlpha)(void*){};
void* toggleContinuation{};
void* toggleReturn{};
using GetWeapon=void*(__cdecl*)();
using GetData=uint8_t*(__thiscall*)(void*);
GetWeapon getWeapon{};
GetData getData{};
bool lastEnabled{},lastBody{};

template<class T> T Read(const void* object,size_t offset) {
    T value{};std::memcpy(&value,static_cast<const uint8_t*>(object)+offset,sizeof(value));return value;
}
// No entities survive a callback. The current weapon is resolved again after
// every load/equip. The original client owns handle/serial validation.
bool Ordinary() noexcept {
    if (!ready.load() || GetCurrentThreadId()!=cameraThread.load()) return false;
    __try {
        const auto player=Read<uint8_t*>(base,0x4a0d50);
        if (!player || Read<int>(player,0x88)!=0 || Read<int>(player,0x15e8)!=0 || Read<int>(player,0x16e0)!=0) return false;
        if (Read<int>(player,0x16f0)>0 || (Read<uint32_t>(player,0x16f4)&0x410u)) return false;
        // Same native movement query as CAM_Think (ff163): ladders use type 10
        // and must retain their own camera/animation route.
        const auto component=player+0xc;
        using MoveType=int(__thiscall*)(void*);
        const auto movement=reinterpret_cast<MoveType>(Read<void**>(component,0)[8]);
        if (movement(component)==10) return false;
        return Read<float>(input,0x138)==0.f && Read<float>(input,0x100)==0.f;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool Eligible(void* weapon=nullptr,bool respectPreference=true) noexcept {
    if (!FirstPersonMeleeEnabled() || !Ordinary()) return false;
    __try {
        // The player's explicit third-person preference always wins.
        if (respectPreference && Read<uint8_t>(input,0xf0)) return false;
        const auto current=getWeapon();
        if (!current || (weapon && current!=weapon)) return false;
        const auto data=getData(current);
        if (!data || Read<int>(data,0x2440)!=16) return false;
        // force_3rd shares the same numeric camera class as melee. Require the
        // independently loaded item_type token too; a ranged forced camera is
        // outside this feature's scope. Never rewrite the shared definition.
        const auto type=reinterpret_cast<const char*>(data+0x2450);
        const auto length=strnlen_s(type,128);
        if (!length || length>=128) return false;
        constexpr char token[]="weapon_melee";
        for (size_t i=0;i+sizeof(token)-1<=length;++i)
            if ((!i || type[i-1]==' ' || type[i-1]=='\t') &&
                std::memcmp(type+i,token,sizeof(token)-1)==0 &&
                (i+sizeof(token)-1==length || type[i+sizeof(token)-1]==' ' || type[i+sizeof(token)-1]=='\t')) return true;
        return false;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool __cdecl KeepMeleeEquipped(void* self) noexcept { return self==input && Eligible(nullptr,false); }
// Native ToggleCamera has already selected the new preference and run weapon
// camera selection. Its remaining forced-camera branch may issue inven_holster
// and erase that preference. Skip only this tail for eligible opt-in melee.
__declspec(naked) void ToggleTail() {
    __asm {
        pushfd
        pushad
        push esi
        call KeepMeleeEquipped
        add esp,4
        test al,al
        jnz keep
        popad
        popfd
        jmp dword ptr [toggleContinuation]
    keep:
        popad
        popfd
        jmp dword ptr [toggleReturn]
    }
}
bool BodyEligible(void* self) {
    return self==input && MeleeBodyCamera() && Read<uint8_t>(input,0xf8)!=0 && Eligible();
}
void __fastcall Switch(void* weapon,void*) {
    weaponSwitch(weapon);
    // Leave native camera preferences and weapon data untouched. Only the
    // ordinary melee's forced-third-person flag is suppressed in native mode.
    if (!MeleeBodyCamera() && Eligible(weapon)) static_cast<uint8_t*>(input)[0xf8]=0;
}
void __fastcall Think(void* self,void*) {
    if (self==input) {
        cameraThread.store(GetCurrentThreadId());
        const bool enabled=FirstPersonMeleeEnabled(),body=MeleeBodyCamera();
        if ((enabled!=lastEnabled || (enabled && body!=lastBody)) && Ordinary()) {
            lastEnabled=enabled;lastBody=body;
            // Apply a menu change on the native camera thread. Re-evaluate the
            // weapon's existing preference route; never issue console commands.
            if (const auto weapon=getWeapon()) Switch(weapon,nullptr);
            TraceLog("CAINE_MELEE_CAMERA_CHANGED: enabled="+std::to_string(enabled)+" body="+std::to_string(body));
        }
    }
    cameraThink(self);
}
void __fastcall View(void* self,void*,void* view) {
    // This boundary runs after the game has calculated its eye position and
    // aim angles. Preserve those for body FPV; native third-person rendering
    // still supplies the existing character and melee animation.
    if (BodyEligible(self)) return;
    cameraView(self,view);
}
float __fastcall Alpha(void* self,void*) {
    if (BodyEligible(self)) return 1.f;
    return modelAlpha(self);
}
bool Bytes(const Module& module,size_t rva,std::initializer_list<uint8_t> bytes) {
    return module.Executable(rva,bytes.size()) && std::memcmp(module.base+rva,bytes.begin(),bytes.size())==0;
}
}
bool MeleeCameraAvailable() { return ready.load(); }
bool InstallMeleeCamera(const Module& client,const std::function<void(const std::string&)>& log) {
    if (ready.load()) return true;
    if (client.sha256!=ClientHash) { log("CAINE_MELEE_CAMERA_UNAVAILABLE: unsupported client profile");return false; }
    base=client.base;input=base+0x2ea6c8;
    std::vector<uint8_t> getter{0x8b,0x0d,0,0,0,0,0x85,0xc9,0x75,0x03,0x33,0xc0,0xc3};
    const auto playerAddress=reinterpret_cast<uint32_t>(base+0x4a0d50);
    std::memcpy(getter.data()+2,&playerAddress,sizeof(playerAddress));
    if (Read<uint8_t*>(input,0)!=base+0x224d4c || Read<void*>(base,0x27ba58)!=input ||
        !Bytes(client,0x7b160,{0x66,0x8b,0x81,0x5e,0x09,0,0,0x50}) ||
        !client.Executable(0x9b210,getter.size()) || std::memcmp(base+0x9b210,getter.data(),getter.size()) ||
        Read<uint8_t*>(base,0x224d4c+0x7c)!=base+0xffb00 ||
        Read<uint8_t*>(base,0x224d4c+0xd4)!=base+0xffaf0 ||
        !Bytes(client,0xff8ee,{0x5f,0x5e,0x5d,0xc3})) {
        log("CAINE_MELEE_CAMERA_UNAVAILABLE: native camera/weapon contract mismatch");return false;
    }
    getWeapon=reinterpret_cast<GetWeapon>(base+0x9b210);getData=reinterpret_cast<GetData>(base+0x7b160);
    toggleReturn=base+0xff8ee;
    auto hooks=new Hooks();std::string error;
    const std::vector<HookRequest> batch{
        {{"melee.weapon_camera",ClientHash,0x9c250,{0x56,0xe8,0x0a,0xef,0xfd,0xff,0x8b,0xb0,0x40,0x24,0,0}},reinterpret_cast<void*>(Switch),reinterpret_cast<void**>(&weaponSwitch)},
        {{"melee.camera_think",ClientHash,0xff130,{0x83,0xec,0x48,0x56,0x57,0x8b,0x3d,0,0,0,0}},reinterpret_cast<void*>(Think),reinterpret_cast<void**>(&cameraThink)},
        {{"melee.body_view",ClientHash,0xffb00,{0x56,0x8b,0x74,0x24,0x08,0x57,0xd9,0x46,0x38}},reinterpret_cast<void*>(View),reinterpret_cast<void**>(&cameraView)},
        {{"melee.body_alpha",ClientHash,0xffaf0,{0xd9,0x81,0x04,0x01,0,0,0xc3,0x90}},reinterpret_cast<void*>(Alpha),reinterpret_cast<void**>(&modelAlpha)},
        {{"melee.toggle_tail",ClientHash,0xff87f,{0x8a,0x86,0xf8,0,0,0,0x84,0xc0,0x74,0x65}},reinterpret_cast<void*>(ToggleTail),&toggleContinuation}
    };
    auto requests=batch;
    const auto address=reinterpret_cast<uint32_t>(base+0x4a0d50);
    std::memcpy(requests[1].spec.expected.data()+7,&address,sizeof(address));
    if (!hooks->InstallBatch(client,requests,error)) {
        if (!hooks->Count()) delete hooks;
        log("CAINE_MELEE_CAMERA_UNAVAILABLE: "+error);return false;
    }
    ready.store(true);
    log("CAINE_MELEE_CAMERA_READY: opt-in Gameplay setting; body FPV/native camera; scripted cameras retain priority");return true;
}
#ifdef CAINE_MELEE_CAMERA_TEST
void TestMeleeOriginals(void* think,void* view,void* alpha) {
    cameraThink=reinterpret_cast<decltype(cameraThink)>(think);
    cameraView=reinterpret_cast<decltype(cameraView)>(view);
    modelAlpha=reinterpret_cast<decltype(modelAlpha)>(alpha);
}
#endif
}
