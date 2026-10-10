#include <caine/melee_camera.hpp>
#include <caine/preferences.hpp>
#include <caine/logging.hpp>
#include <algorithm>
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
using SetupBonesCall=bool(__thiscall*)(void*,void*,int,int,float,int);
SetupBonesCall setupBones{};
int(__thiscall* drawModel)(void*,int,int){};
float viewAim[3]{};
int viewFrame{};
constexpr float NoseForward=2.5f;
struct HeadRig {
    uint8_t* header{};
    int count{},head=-1,flags{};
    bool eyes{},selected[256]{};
    float local[3]{4.5f,2.2f,0.f},bind[12]{};
};
struct HeadRender {
    void* player{};
    HeadRig rig{};
    float* matrices{};
    float original[256][12]{};
    float aim[3]{};
    bool posed{};
};
thread_local HeadRender* renderingHead{};

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
bool Rigid(const float* matrix) {
    for (size_t i=0;i<12;++i) if (!std::isfinite(matrix[i])) return false;
    for (size_t a=0;a<3;++a) for (size_t b=a;b<3;++b) {
        float dot{};for (size_t r=0;r<3;++r) dot+=matrix[r*4+a]*matrix[r*4+b];
        if (std::abs(dot-(a==b?1.f:0.f))>.02f) return false;
    }
    const float determinant=matrix[0]*(matrix[5]*matrix[10]-matrix[6]*matrix[9])-
        matrix[1]*(matrix[4]*matrix[10]-matrix[6]*matrix[8])+matrix[2]*(matrix[4]*matrix[9]-matrix[5]*matrix[8]);
    return determinant>.98f && determinant<1.02f;
}
// Independently recovered IDST 2531 layout: 160-byte bones, 60-byte
// attachments. The inverse bind pose supplies each model's anatomical axes;
// the eyes attachment stores a position, not a camera-facing orientation.
bool ResolveHead(void* player,HeadRig& rig) noexcept {
    __try {
        rig=HeadRig{};rig.header=modelHeader(player,-1);
        const auto header=rig.header;
        if (!header || Read<uint32_t>(header,0)!=0x54534449u || Read<uint32_t>(header,4)!=0x9e3u) return false;
        const auto length=Read<uint32_t>(header,0x8c);
        rig.count=Read<int>(header,0xf0);const auto index=Read<int>(header,0xf4);
        if (length<0x1a8 || length>64u*1024u*1024u || rig.count<=0 || rig.count>256 || index<0x1a8 ||
            static_cast<uint64_t>(index)+static_cast<uint64_t>(rig.count)*160>length) return false;
        for (int i=0;i<rig.count;++i) if (Named(header,length,static_cast<size_t>(index)+i*160,"Bip01 Head")) { rig.head=i;break; }
        if (rig.head<0) return false;
        const auto row=static_cast<size_t>(index)+rig.head*160;
        std::memcpy(rig.bind,header+row+0x58,sizeof(rig.bind));
        if (!Rigid(rig.bind)) return false;
        // Validate the entire parent graph before touching a transform. Include
        // eyes/hair/jaw descendants, while arms, torso and weapon remain native.
        for (int i=0;i<rig.count;++i) {
            int current=i;
            for (int depth=0;current!=-1;++depth) {
                if (current<0 || current>=rig.count || depth>=rig.count) return false;
                if (current==rig.head) rig.selected[i]=true;
                current=Read<int>(header,static_cast<size_t>(index)+current*160+4);
            }
            if (rig.selected[i]) rig.flags|=Read<int>(header,static_cast<size_t>(index)+i*160+0x88)&0xfffc;
        }
        if (!rig.flags) return false;
        const auto attachments=Read<int>(header,0x148),attachmentIndex=Read<int>(header,0x14c);
        if (attachments<0 || attachments>256 || (attachments && (attachmentIndex<0x1a8 ||
            static_cast<uint64_t>(attachmentIndex)+static_cast<uint64_t>(attachments)*60>length))) return false;
        for (int i=0;i<attachments;++i) {
            const auto attachment=static_cast<size_t>(attachmentIndex)+i*60;
            if (Read<int>(header,attachment+8)!=rig.head || !Named(header,length,attachment,"eyes")) continue;
            for (size_t j=0;j<3;++j) rig.local[j]=Read<float>(header,attachment+0x18+j*16);
            rig.eyes=true;break;
        }
        for (const auto value:rig.local) if (!std::isfinite(value) || std::abs(value)>20.f) return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool AimHead(const HeadRig& rig,const float* animated,const float* angles,float* posed,float* forward) {
    if (!Rigid(animated)) return false;
    for (size_t i=0;i<3;++i) if (!std::isfinite(angles[i]) || std::abs(angles[i])>360000.f) return false;
    constexpr float radians=3.14159265358979323846f/180.f;
    const float sp=std::sin(angles[0]*radians),cp=std::cos(angles[0]*radians);
    const float sy=std::sin(angles[1]*radians),cy=std::cos(angles[1]*radians);
    const float sr=std::sin(angles[2]*radians),cr=std::cos(angles[2]*radians);
    const float left[3]{sr*sp*cy-cr*sy,sr*sp*sy+cr*cy,sr*cp};
    const float up[3]{cr*sp*cy+sr*sy,cr*sp*sy-sr*cy,cr*cp};
    forward[0]=cp*cy;forward[1]=cp*sy;forward[2]=-sp;
    // Bloodlines player models face model -Y; model +X is camera left.
    // Rotate the rest head into mouse aim, retaining its animated pivot.
    for (size_t r=0;r<3;++r) {
        for (size_t c=0;c<3;++c) posed[r*4+c]=left[r]*rig.bind[c*4]-forward[r]*rig.bind[c*4+1]+up[r]*rig.bind[c*4+2];
        posed[r*4+3]=animated[r*4+3];
    }
    return Rigid(posed);
}
bool NativeBones(void* player,const HeadRig& rig) noexcept {
    __try {
        const auto globals=Read<uint8_t*>(base,0x2b8494);
        if (!globals) return false;
        const auto time=Read<float>(globals,0xc);
        if (!std::isfinite(time)) return false;
        const auto renderable=static_cast<uint8_t*>(player)+4;
        const auto setup=Read<void**>(renderable,0)[0x3c/4];
        return setup==boneSetupEntry && RefreshBones(setup,renderable,rig.flags,time);
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
bool HeadView(void* view,int& bone,bool& eyes) noexcept {
    if (buildingBones || renderingHead) return false;
    __try {
        const auto player=Read<uint8_t*>(base,0x4a0d50);
        HeadRig rig{};if (!ResolveHead(player,rig) || !NativeBones(player,rig)) return false;
        bone=rig.head;eyes=rig.eyes;
        const auto matrices=Read<float*>(player,0x6e8);
        if (!matrices) return false;
        float matrix[12]{},posed[12]{},aim[3]{},forward[3]{},origin[3]{};
        std::memcpy(matrix,matrices+rig.head*12,sizeof(matrix));
        std::memcpy(aim,static_cast<uint8_t*>(view)+0x50,sizeof(aim));
        if (!AimHead(rig,matrix,aim,posed,forward)) return false;
        for (size_t row=0;row<3;++row) {
            origin[row]=posed[row*4+3]+forward[row]*NoseForward;
            for (size_t column=0;column<3;++column) origin[row]+=posed[row*4+column]*rig.local[column];
            const auto previous=Read<float>(view,0x38+row*4);
            if (!std::isfinite(origin[row]) || !std::isfinite(previous) || std::abs(origin[row]-previous)>256.f) return false;
        }
        const auto globals=Read<uint8_t*>(base,0x2b8494);
        viewFrame=Read<int>(globals,4);std::memcpy(viewAim,aim,sizeof(aim));
        std::memcpy(static_cast<uint8_t*>(view)+0x38,origin,sizeof(origin));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
void RestoreHead(HeadRender& render) noexcept {
    if (!render.posed) return;
    __try {
        for (int i=0;i<render.rig.count;++i) if (render.rig.selected[i])
            std::memcpy(render.matrices+i*12,render.original[i],sizeof(render.original[i]));
    } __except(EXCEPTION_EXECUTE_HANDLER) {}
    render.posed=false;
}
bool PoseHead(HeadRender& render) noexcept {
    __try {
        if (modelHeader(render.player,-1)!=render.rig.header) return false;
        const auto matrices=Read<float*>(render.player,0x6e8);
        if (!matrices) return false;
        float posed[12]{},forward[3]{},delta[9]{};
        if (!AimHead(render.rig,matrices+render.rig.head*12,render.aim,posed,forward)) return false;
        // Finish all validation and calculations before mutating the cache.
        float transformed[256][12]{};
        const auto head=matrices+render.rig.head*12;
        for (size_t r=0;r<3;++r) for (size_t c=0;c<3;++c)
            for (size_t k=0;k<3;++k) delta[r*3+c]+=posed[r*4+k]*head[c*4+k];
        for (int i=0;i<render.rig.count;++i) if (render.rig.selected[i]) {
            const auto original=matrices+i*12;
            if (!Rigid(original)) return false;
            std::memcpy(render.original[i],original,sizeof(render.original[i]));
            for (size_t r=0;r<3;++r) {
                for (size_t c=0;c<3;++c) for (size_t k=0;k<3;++k)
                    transformed[i][r*4+c]+=delta[r*3+k]*original[k*4+c];
                transformed[i][r*4+3]=head[r*4+3];
                for (size_t k=0;k<3;++k) transformed[i][r*4+3]+=delta[r*3+k]*(original[k*4+3]-head[k*4+3]);
            }
            if (!Rigid(transformed[i])) return false;
        }
        render.matrices=matrices;render.posed=true;
        for (int i=0;i<render.rig.count;++i) if (render.rig.selected[i])
            std::memcpy(matrices+i*12,transformed[i],sizeof(transformed[i]));
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) { RestoreHead(render);return false; }
}
bool __fastcall Bones(void* self,void*,void* output,int maximum,int flags,float time,int force) {
    const auto render=renderingHead;
    const bool scoped=render && self==static_cast<uint8_t*>(render->player)+4 && !buildingBones;
    if (scoped) RestoreHead(*render);
    const bool result=setupBones(self,output,maximum,flags,time,force);
    if (scoped && result && BodyEligible(input) && PoseHead(*render) && output && maximum>=render->rig.count) {
        // SetupBones copies its cache into a renderer-owned output buffer.
        // Keep that draw buffer in sync without posing other callers' output.
        for (int i=0;i<render->rig.count;++i) if (render->rig.selected[i])
            std::memcpy(static_cast<float*>(output)+i*12,render->matrices+i*12,48);
    }
    return result;
}
bool BeginHeadRender(void* player,HeadRender& render) noexcept {
    if (renderingHead || !headViewActive || !BodyEligible(input)) return false;
    __try {
        if (player!=Read<void*>(base,0x4a0d50)) return false;
        const auto globals=Read<uint8_t*>(base,0x2b8494);
        if (!globals || viewFrame!=Read<int>(globals,4)) return false;
        render.player=player;std::memcpy(render.aim,viewAim,sizeof(viewAim));
        return ResolveHead(player,render.rig) && NativeBones(player,render.rig) && PoseHead(render);
    } __except(EXCEPTION_EXECUTE_HANDLER) { RestoreHead(render);return false; }
}
__declspec(noinline) int DrawHead(void* player,int flags,int instance) {
    HeadRender render{};
    if (!BeginHeadRender(player,render)) return drawModel(player,flags,instance);
    renderingHead=&render;
    __try { return drawModel(player,flags,instance); }
    __finally { RestoreHead(render);renderingHead=nullptr; }
}
bool LocalHeadDraw(void* player) noexcept {
    if (!headViewActive || renderingHead || GetCurrentThreadId()!=cameraThread.load()) return false;
    __try { return player==Read<void*>(base,0x4a0d50) && BodyEligible(input); }
    __except(EXCEPTION_EXECUTE_HANDLER) { return false; }
}
int __fastcall Draw(void* player,void*,int flags,int instance) {
    // No rig parsing, allocation or large stack clearing for ordinary NPC,
    // disabled, third-person or cinematic draws.
    return LocalHeadDraw(player)?DrawHead(player,flags,instance):drawModel(player,flags,instance);
}
float ReduceNearPlane(void* view) noexcept {
    __try {
        // The native view builder at 191710 owns a CViewSetup at +10 and
        // initializes world near/far at +6c/+70 (view +5c/+60). Stock world
        // near is 8; viewmodels already use 1 at view +64. Change only this
        // frame's valid body view, retaining any tighter existing plane.
        const auto nearPlane=Read<float>(view,0x5c),farPlane=Read<float>(view,0x60);
        if (!std::isfinite(nearPlane) || !std::isfinite(farPlane) || nearPlane<=0.f || farPlane<=nearPlane) return 0.f;
        const float reduced=std::min(nearPlane,1.f);
        std::memcpy(static_cast<uint8_t*>(view)+0x5c,&reduced,sizeof(reduced));
        return reduced;
    } __except(EXCEPTION_EXECUTE_HANDLER) { return 0.f; }
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
        const float nearPlane=headViewActive?ReduceNearPlane(view):0.f;
        const auto now=GetTickCount64();
        if (!headLogged || now-headLogged>=5000) {
            headLogged=now;
            TraceLog("CAINE_MELEE_HEAD_VIEW: active="+std::to_string(headViewActive)+" bone="+std::to_string(bone)+" eyes_attachment="+std::to_string(eyes)+" near_clip="+std::to_string(nearPlane)+" nose_forward="+std::to_string(NoseForward));
        }
        // The animated pivot follows movement; head orientation follows mouse aim.
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
        !Bytes(client,0x92970,{0x83,0xec,0x7c,0x53,0x56,0x8b,0xf1,0x8b,0x46,0x40}) ||
        !Bytes(client,0x91fae,{0x85,0xed,0x74,0x2a,0x8b,0x87,0x30,0x07,0,0}) ||
        !Bytes(client,0x19179f,{0xc7,0x46,0x6c,0,0,0,0x41,0xc7,0x46,0x74,0,0,0x80,0x3f}) ||
        !Bytes(client,0x1917e1,{0x8d,0x5e,0x10,0xc7,0x03,0,0,0,0}) ||
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
        {{"melee.toggle_tail",ClientHash,0xff87f,{0x8a,0x86,0xf8,0,0,0,0x84,0xc0,0x74,0x65}},reinterpret_cast<void*>(ToggleTail),&toggleContinuation},
        {{"melee.head_bones",ClientHash,0x919c0,{0xb8,0x30,0x27,0,0,0xe8,0x36,0xf5,0x13,0}},reinterpret_cast<void*>(Bones),reinterpret_cast<void**>(&setupBones)},
        {{"melee.head_draw",ClientHash,0x92970,{0x83,0xec,0x7c,0x53,0x56,0x8b,0xf1,0x8b,0x46,0x40}},reinterpret_cast<void*>(Draw),reinterpret_cast<void**>(&drawModel)}
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
void TestMeleeBoneSetup(void* setup) { boneSetupEntry=setup;setupBones=reinterpret_cast<SetupBonesCall>(setup); }
void TestMeleeDraw(void* draw) { drawModel=reinterpret_cast<decltype(drawModel)>(draw); }
#endif
}
