#include <caine/melee_camera.hpp>
#include <caine/preferences.hpp>
#include <caine/logging.hpp>
#include <atomic>
#include <cmath>
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
std::atomic<bool> headViewActive{};
DWORD64 headLogged{};
void* boneSetupEntry{};
using ModelHeader=uint8_t*(__thiscall*)(void*,int);
ModelHeader modelHeader{};
thread_local bool buildingBones{};

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
bool Named(const uint8_t* header,uint32_t length,size_t row,const char* name) {
    const auto offset=Read<int>(header,row);
    if (offset<=0 || static_cast<uint64_t>(row)+static_cast<uint32_t>(offset)>=length) return false;
    const auto start=row+static_cast<uint32_t>(offset);
    const auto count=std::strlen(name);
    if (start+count>=length) return false;
    for (size_t i=0;i<count;++i) {
        const auto a=header[start+i],b=static_cast<uint8_t>(name[i]);
        if ((a>='A' && a<='Z'?a+32:a)!=(b>='A' && b<='Z'?b+32:b)) return false;
    }
    return header[start+count]==0;
}
bool RefreshBones(void* setup,void* renderable,int flags,float time) noexcept {
    buildingBones=true;
    __try {
        using Setup=bool(__thiscall*)(void*,void*,int,int,float,int);
        return reinterpret_cast<Setup>(setup)(renderable,nullptr,-1,flags,time,0);
    } __finally { buildingBones=false; }
}
// The older Bloodlines studio format has 160-byte bones and 60-byte
// attachments. Resolve the current model each callback, never a saved bone ID.
// SetupBones refreshes the native animation cache for this frame; inspecting
// its old matrix alone can leave the camera behind the animated character.
bool HeadView(void* view,int& bone,bool& eyes) noexcept {
    if (buildingBones) return false;
    __try {
        const auto player=Read<uint8_t*>(base,0x4a0d50);
        const auto header=modelHeader(player,-1);
        if (!header || Read<uint32_t>(header,0)!=0x54534449u || Read<uint32_t>(header,4)!=0x9e3u) return false;
        const auto length=Read<uint32_t>(header,0x8c);
        const auto count=Read<int>(header,0xf0),index=Read<int>(header,0xf4);
        if (length<0x1a8 || length>64u*1024u*1024u || count<=0 || count>256 || index<0x1a8 ||
            static_cast<uint64_t>(index)+static_cast<uint64_t>(count)*160>length) return false;
        bone=-1;
        for (int i=0;i<count;++i) if (Named(header,length,static_cast<size_t>(index)+i*160,"Bip01 Head")) { bone=i;break; }
        if (bone<0) return false;
        const auto flags=Read<int>(header,static_cast<size_t>(index)+bone*160+0x88)&0xfffc;
        if (!flags) return false;
        float local[3]{4.5f,2.2f,0.f}; // stock head-space eye offset when an armor model omits eyes
        eyes=false;
        const auto attachments=Read<int>(header,0x148),attachmentIndex=Read<int>(header,0x14c);
        if (attachments<0 || attachments>256 || (attachments && (attachmentIndex<0x1a8 ||
            static_cast<uint64_t>(attachmentIndex)+static_cast<uint64_t>(attachments)*60>length))) return false;
        for (int i=0;i<attachments;++i) {
            const auto row=static_cast<size_t>(attachmentIndex)+i*60;
            if (Read<int>(header,row+8)!=bone || !Named(header,length,row,"eyes")) continue;
            for (size_t j=0;j<3;++j) local[j]=Read<float>(header,row+0x18+j*16);
            eyes=true;break;
        }
        for (const auto value:local) if (!std::isfinite(value) || std::abs(value)>20.f) return false;
        const auto globals=Read<uint8_t*>(base,0x2b8494);
        if (!globals) return false;
        const auto time=Read<float>(globals,0xc);
        if (!std::isfinite(time)) return false;
        const auto renderable=player+4;
        const auto setup=Read<void**>(renderable,0)[0x3c/4];
        if (setup!=boneSetupEntry) return false;
        if (!RefreshBones(setup,renderable,flags,time)) return false;
        const auto matrices=Read<uint8_t*>(player,0x6e8);
        if (!matrices) return false;
        float matrix[12]{};std::memcpy(matrix,matrices+bone*48,sizeof(matrix));
        for (const auto value:matrix) if (!std::isfinite(value)) return false;
        for (size_t column=0;column<3;++column) {
            const auto norm=matrix[column]*matrix[column]+matrix[4+column]*matrix[4+column]+matrix[8+column]*matrix[8+column];
            if (norm<.01f || norm>100.f) return false;
        }
        float origin[3]{};
        for (size_t row=0;row<3;++row) {
            origin[row]=matrix[row*4+3];
            for (size_t column=0;column<3;++column) origin[row]+=matrix[row*4+column]*local[column];
            // Reject stale/invalid transforms without changing the view.
            const auto previous=Read<float>(view,0x38+row*4);
            if (!std::isfinite(origin[row]) || !std::isfinite(previous) || std::abs(origin[row]-previous)>256.f) return false;
        }
        std::memcpy(static_cast<uint8_t*>(view)+0x38,origin,sizeof(origin));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void __fastcall Switch(void* weapon,void*) {
    weaponSwitch(weapon);
    // Leave native camera preferences and weapon data untouched. Only the
    // ordinary melee's forced-third-person flag is suppressed in native mode.
    if (!MeleeBodyCamera() && Eligible(weapon)) static_cast<uint8_t*>(input)[0xf8]=0;
}
void __fastcall Think(void* self,void*) {
    if (self==input) {
        headViewActive=false;
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
    headViewActive=false;
    if (BodyEligible(self)) {
        int bone=-1;bool eyes=false;
        headViewActive=HeadView(view,bone,eyes);
        const auto now=GetTickCount64();
        if (!headLogged || now-headLogged>=5000) {
            headLogged=now;
            TraceLog("CAINE_MELEE_HEAD_VIEW: active="+std::to_string(headViewActive)+" bone="+std::to_string(bone)+" eyes_attachment="+std::to_string(eyes));
        }
        // Only the origin follows animation. Mouse aim remains authoritative.
        if (headViewActive) return;
    }
    cameraView(self,view);
}
float __fastcall Alpha(void* self,void*) {
    if (headViewActive && BodyEligible(self)) return 1.f;
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
        !Bytes(client,0x8f900,{0x8b,0x44,0x24,0x04,0x56,0x85,0xc0,0x8b,0xf1}) ||
        !Bytes(client,0x919c0,{0xb8,0x30,0x27,0,0,0xe8,0x36,0xf5,0x13,0}) ||
        !Bytes(client,0xff8ee,{0x5f,0x5e,0x5d,0xc3})) {
        log("CAINE_MELEE_CAMERA_UNAVAILABLE: native camera/weapon contract mismatch");return false;
    }
    getWeapon=reinterpret_cast<GetWeapon>(base+0x9b210);getData=reinterpret_cast<GetData>(base+0x7b160);
    modelHeader=reinterpret_cast<ModelHeader>(base+0x8f900);boneSetupEntry=base+0x919c0;
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
void TestMeleeBoneSetup(void* setup) { boneSetupEntry=setup; }
#endif
}
