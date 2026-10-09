#include <caine/native_bridge.hpp>
#include <caine/native_types.hpp>
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace {
constexpr char GameHash[]="996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8";
constexpr char ClientHash[]="9ce1a59fd3f5175a155c5276cb6d092e25a009835585a371f93b923dfc134f01";
using D=caine::native::DialogueFields;using H=caine::native::HudDialogueFields;
using PacketFn=void(__thiscall*)(void*,void*);
using VoidFn=void(__thiscall*)(void*);
using ActiveFn=void(__thiscall*)(void*,bool);
using PickFn=void(__thiscall*)(void*,int,bool);
using FilenameFn=const char*(__thiscall*)(void*);
PacketFn packetOriginal{};VoidFn releaseOriginal{},paintOriginal{};
ActiveFn activeOriginal{};PickFn pickOriginal{};
FilenameFn filename{};
caine::Hooks* hooks{};
uint8_t* gameBase{};
std::recursive_mutex mutex;
std::function<void(const std::string&)> logger;
std::atomic<bool> ready{},attempted{};
struct Live {
    CaineDialogueV1 copied{};
    void* dialog{};
    void* hud{};
    DWORD thread{};
    ULONGLONG painted{};
    void* owner{};
    std::optional<int> pick;
    std::string rawOpening;
    std::vector<std::string> rawResponses;
} live;
uint64_t nextToken{};
bool Readable(const void* address,size_t count) {
    if (!address || !count) return false;
    auto start=reinterpret_cast<uintptr_t>(address);
    if (start>UINTPTR_MAX-count) return false;
    const auto end=start+count;
    while (start<end) {
        MEMORY_BASIC_INFORMATION info{};
        if (!VirtualQuery(reinterpret_cast<void*>(start),&info,sizeof(info)) || info.State!=MEM_COMMIT ||
            (info.Protect&(PAGE_NOACCESS|PAGE_GUARD))) return false;
        const auto next=reinterpret_cast<uintptr_t>(info.BaseAddress)+info.RegionSize;
        if (next<=start) return false;start=next;
    }
    return true;
}
template<class T> T Field(const void* object,size_t offset) {
    T value{};memcpy(&value,static_cast<const uint8_t*>(object)+offset,sizeof(value));return value;
}
std::string Text(const void* object,size_t offset,size_t capacity) {
    const auto text=reinterpret_cast<const char*>(static_cast<const uint8_t*>(object)+offset);
    const auto count=strnlen_s(text,capacity);
    if (count==capacity) throw std::runtime_error("Unterminated native dialogue text");
    return {text,count};
}
std::string Utf8(const std::string& text) {
    if (text.empty()) return {};
    const auto count=static_cast<int>(text.size());
    const auto length=MultiByteToWideChar(1252,0,text.data(),count,nullptr,0);
    std::wstring wide(length,L'\0');MultiByteToWideChar(1252,0,text.data(),count,wide.data(),length);
    const auto bytes=WideCharToMultiByte(CP_UTF8,0,wide.data(),length,nullptr,0,nullptr,nullptr);
    std::string result(bytes,'\0');WideCharToMultiByte(CP_UTF8,0,wide.data(),length,result.data(),bytes,nullptr,nullptr);
    return result;
}
bool HandleExists(uint32_t handle) {
    if (handle==UINT32_MAX) return false;
    const auto entries=Field<const uint8_t*>(gameBase,0x566458);
    const auto slot=static_cast<size_t>(caine::native::EntityHandle{handle}.Index())*12;
    if (!Readable(entries,slot+12)) return false;
    const auto object=Field<const void*>(entries,slot+4);
    return object && Field<uint32_t>(entries,slot+8)==caine::native::EntityHandle{handle}.Serial() && Readable(object,4);
}
bool Current() {
    if (!live.dialog || !live.copied.token || live.thread!=GetCurrentThreadId() ||
        !Readable(live.dialog,0x2838) || !HandleExists(live.copied.npcHandle) || !HandleExists(live.copied.playerHandle)) return false;
    return Field<uint32_t>(live.dialog,D::Npc)==live.copied.npcHandle &&
        Field<uint32_t>(live.dialog,D::Player)==live.copied.playerHandle &&
        Field<int>(live.dialog,D::Line)==live.copied.line &&
        Field<int>(live.dialog,D::Count)==static_cast<int>(live.copied.responseCount) &&
        Field<void*>(live.dialog,D::Data)!=nullptr;
}
bool Visible() {
    if (!Current() || !live.hud || !Readable(live.hud,0x324c) || !Field<uint8_t>(live.hud,H::Active) ||
        Field<int>(live.hud,H::Count)!=static_cast<int>(live.copied.responseCount)) return false;
    if (Text(live.hud,H::Opening,2048)!=live.rawOpening) return false;
    for (size_t i=0;i<live.rawResponses.size();++i)
        if (Text(live.hud,H::Responses+i*2048,2048)!=live.rawResponses[i]) return false;
    return true;
}
void Invalidate() { live={};++nextToken; }
void Capture(void* self,void* packet) {
    Invalidate();
    if (!Readable(self,0x2838) || !Readable(packet,0x2804)) return;
    const int count=Field<int>(self,D::Count);
    // Auto-end floats have no interactive player choices; preserve their native flow.
    if (count<1 || count>4 || !Field<void*>(self,D::Data)) return;
    Live next;next.dialog=self;next.thread=GetCurrentThreadId();
    auto& copy=next.copied;copy.size=sizeof(copy);copy.responseCount=static_cast<uint32_t>(count);
    copy.npcHandle=Field<uint32_t>(self,D::Npc);copy.playerHandle=Field<uint32_t>(self,D::Player);
    if (!HandleExists(copy.npcHandle) || !HandleExists(copy.playerHandle)) return;
    copy.line=Field<int>(self,D::Line);
    const char* path=filename(self);
    if (!Readable(path,260)) return;
    const auto source=Utf8(Text(path,0,260));
    strcpy_s(copy.source,source.c_str());
    next.rawOpening=Text(packet,0,2048);strcpy_s(copy.opening,Utf8(next.rawOpening).c_str());
    for (int i=0;i<count;++i) {
        next.rawResponses.push_back(Text(packet,D::PacketResponses+static_cast<size_t>(i)*2048,2048));
        strcpy_s(copy.responses[i],Utf8(next.rawResponses.back()).c_str());
    }
    copy.token=++nextToken;const auto token=copy.token;const auto line=copy.line;live=std::move(next);
    logger("CAINE_NATIVE_DIALOGUE_PACKET: token="+std::to_string(token)+" line="+std::to_string(line)+" responses="+std::to_string(count));
}
void __fastcall Packet(void* self,void*,void* packet) {
    packetOriginal(self,packet);
    try { std::lock_guard<std::recursive_mutex> lock(mutex);Capture(self,packet); }
    catch (...) { std::lock_guard<std::recursive_mutex> lock(mutex);Invalidate();logger("CAINE_NATIVE_DIALOGUE_REJECTED: invalid native packet"); }
}
void __fastcall Release(void* self,void*) {
    { std::lock_guard<std::recursive_mutex> lock(mutex);if (live.dialog==self) Invalidate(); }
    releaseOriginal(self);
}
void __fastcall Active(void* self,void*,bool enabled) {
    { std::lock_guard<std::recursive_mutex> lock(mutex);if (!enabled && live.hud==self) Invalidate(); }
    activeOriginal(self,enabled);
}
void __fastcall Paint(void* self,void*) {
    bool suppress=false;
    try {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        live.hud=self;live.painted=GetTickCount64();
        if (live.owner && Visible()) {
            suppress=true;
            if (live.pick && (*live.pick==-2 || !Field<uint8_t>(self,H::Waiting))) {
                const auto index=*live.pick;live.pick.reset();live.owner=nullptr;
                logger("CAINE_NATIVE_DIALOGUE_PICK: token="+std::to_string(live.copied.token)+" index="+std::to_string(index));
                pickOriginal(self,index,true);
            }
        }
    } catch (...) { std::lock_guard<std::recursive_mutex> lock(mutex);Invalidate(); }
    if (!suppress) paintOriginal(self);
}
void __fastcall Pick(void* self,void*,int index,bool force) {
    { std::lock_guard<std::recursive_mutex> lock(mutex);try { if (live.owner && live.hud==self && Visible()) return; } catch (...) { Invalidate(); } }
    pickOriginal(self,index,force);
}
std::vector<uint8_t> Absolute(const caine::Module& module,std::vector<uint8_t> bytes,std::initializer_list<size_t> fixups) {
    const auto delta=reinterpret_cast<uint32_t>(module.base)-0x10000000u;
    for (const auto offset:fixups) { uint32_t operand{};memcpy(&operand,bytes.data()+offset,4);operand+=delta;memcpy(bytes.data()+offset,&operand,4); }
    return bytes;
}
}
namespace caine {
bool InstallNativeBridge(const std::function<void(const std::string&)>& log) {
    if (attempted.exchange(true)) return ready.load();
    try {
        const auto game=Module::Inspect(GetModuleHandleW(L"vampire.dll")),client=Module::Inspect(GetModuleHandleW(L"client.dll"));
        if (game.sha256!=GameHash || client.sha256!=ClientHash) throw std::runtime_error("Unsupported native dialogue profile");
        gameBase=game.base;logger=log;auto owner=new Hooks();std::string error;
        const auto fileGuard=Absolute(game,{0xa1,0x80,0x36,0x9f,0x10,0x56,0x57,0x8b,0,0x8d,0x14,0x40},{1});
        if (memcmp(game.base+0xe7310,fileGuard.data(),fileGuard.size())) throw std::runtime_error("Dialogue filename helper has changed");
        filename=reinterpret_cast<FilenameFn>(game.base+0xe7310);
        if (!owner->InstallBatch(game,{
            {{"native.dialogue.packet",GameHash,0xe7da0,Absolute(game,{0x51,0xa1,0x80,0x36,0x9f,0x10,0x8b,0x15,0x20,0x36,0x9f,0x10},{2,8})},reinterpret_cast<void*>(Packet),reinterpret_cast<void**>(&packetOriginal)},
            {{"native.dialogue.release",GameHash,0xe5240,Absolute(game,{0xa1,0x80,0x36,0x9f,0x10,0x8b,0x15,0x20,0x36,0x9f,0x10},{1,7})},reinterpret_cast<void*>(Release),reinterpret_cast<void**>(&releaseOriginal)}},error)) { delete owner;throw std::runtime_error(error); }
        if (!owner->InstallBatch(client,{
            {{"native.dialogue.paint",ClientHash,0x534d0,Absolute(client,{0xa1,0x28,0x31,0x1e,0x10,0x8b,0x15,0x24,0x31,0x1e,0x10},{1,7})},reinterpret_cast<void*>(Paint),reinterpret_cast<void**>(&paintOriginal)},
            {{"native.dialogue.active",ClientHash,0x55360,Absolute(client,{0xa1,0x28,0x31,0x1e,0x10,0x8b,0x15,0x24,0x31,0x1e,0x10},{1,7})},reinterpret_cast<void*>(Active),reinterpret_cast<void**>(&activeOriginal)},
            {{"native.dialogue.pick",ClientHash,0x549f0,Absolute(client,{0x83,0xec,0x20,0xa1,0x28,0x31,0x1e,0x10,0x8b,0x15,0x24,0x31,0x1e,0x10},{4,10})},reinterpret_cast<void*>(Pick),reinterpret_cast<void**>(&pickOriginal)}},error)) { owner->RemoveAll(error);delete owner;throw std::runtime_error(error); }
        hooks=owner;ready.store(true);log("CAINE_NATIVE_BRIDGE_READY: copied dialogue context, serial-validated entity handles, deferred native choices; gameplay acceptance pending");return true;
    } catch (const std::exception& failure) { log(std::string("CAINE_NATIVE_BRIDGE_UNAVAILABLE: ")+failure.what());return false; }
}
bool ReadNativeDialogue(CaineDialogueV1& output) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!ready.load() || GetTickCount64()-live.painted>250 || !Visible()) return false;
    output=live.copied;return true;
}
bool ClaimNativeDialogue(void* owner,uint64_t token,bool enabled) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!enabled) {
        if (live.owner && live.owner!=owner) return false;
        live.owner=nullptr;live.pick.reset();return true;
    }
    if (!owner || !ready.load() || token!=live.copied.token || !Visible() || (live.owner && live.owner!=owner)) return false;
    live.owner=owner;return true;
}
bool QueueNativeDialoguePick(void* owner,uint64_t token,int index) {
    std::lock_guard<std::recursive_mutex> lock(mutex);
    if (!owner || owner!=live.owner || token!=live.copied.token || !Visible() || live.pick ||
        (index!=-2 && (index<0 || index>=static_cast<int>(live.copied.responseCount)))) return false;
    live.pick=index;return true;
}
void InvalidateNativeDialogue() { std::lock_guard<std::recursive_mutex> lock(mutex);Invalidate(); }
#ifdef CAINE_NATIVE_BRIDGE_TEST
void TestNativeOriginals(void* packet,void* release,void* paint,void* active,void* pick,void* file) {
    packetOriginal=reinterpret_cast<PacketFn>(packet);releaseOriginal=reinterpret_cast<VoidFn>(release);
    paintOriginal=reinterpret_cast<VoidFn>(paint);activeOriginal=reinterpret_cast<ActiveFn>(active);
    pickOriginal=reinterpret_cast<PickFn>(pick);filename=reinterpret_cast<FilenameFn>(file);
}
#endif
}
