#include <caine/native_bridge.hpp>
#include <caine/native_types.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace caine { void TestNativeOriginals(void*,void*,void*,void*,void*,void*); }
namespace {
unsigned paints{},picks{},releases{};int choice{};bool forced{};
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void __fastcall Packet(void*,void*,void*) {}
void __fastcall Release(void*,void*) { ++releases; }
void __fastcall Paint(void*,void*) { ++paints; }
void __fastcall Active(void*,void*,bool) {}
void __fastcall Pick(void*,void*,int index,bool force) { ++picks;choice=index;forced=force; }
const char* __fastcall Filename(void*,void*) { return "santa monica/jeanette.dlg"; }
template<class T> void Put(void* object,size_t offset,T value) { memcpy(static_cast<uint8_t*>(object)+offset,&value,sizeof(value)); }
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==3,"supply installed game and client DLLs");
        const auto game=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        const auto client=LoadLibraryExW(argv[2],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        Check(game && client,"map installed native modules");
        Check(caine::InstallNativeBridge([](const auto&){}),"exact production profile and hook batch");
        caine::TestNativeOriginals(reinterpret_cast<void*>(Packet),reinterpret_cast<void*>(Release),reinterpret_cast<void*>(Paint),reinterpret_cast<void*>(Active),reinterpret_cast<void*>(Pick),reinterpret_cast<void*>(Filename));
        std::array<uint8_t,0x2838> dialog{};std::array<uint8_t,0x2804> packet{};std::array<uint8_t,0x6000> hud{};
        std::array<uint8_t,8192*12> entities{};int npc{},player{};
        Put(reinterpret_cast<uint8_t*>(game),0x566458,entities.data());
        Put(entities.data(),12+4,&npc);Put(entities.data(),12+8,2u);
        Put(entities.data(),24+4,&player);Put(entities.data(),24+8,3u);
        Put(dialog.data(),0,(2u<<13)|1u);Put(dialog.data(),4,(3u<<13)|2u);Put(dialog.data(),8,&npc);
        Put(dialog.data(),0x2830,1);Put(dialog.data(),0x2834,2);
        strcpy_s(reinterpret_cast<char*>(packet.data()),2048,"Hello \xe9.");
        strcpy_s(reinterpret_cast<char*>(packet.data()+0x804),2048,"Original quest action");
        strcpy_s(reinterpret_cast<char*>(packet.data()+0x1004),2048,"Leave");
        Put(hud.data(),0x1b4,uint8_t(1));Put(hud.data(),0x1228,2);
        memcpy(hud.data()+0xa28,packet.data(),2048);memcpy(hud.data()+0x122c,packet.data()+0x804,4096);
        using PacketCall=void(__thiscall*)(void*,void*);using VoidCall=void(__thiscall*)(void*);
        using PickCall=void(__thiscall*)(void*,int,bool);using ActiveCall=void(__thiscall*)(void*,bool);
        const auto capture=reinterpret_cast<PacketCall>(reinterpret_cast<uint8_t*>(game)+0xe7da0);
        const auto paint=reinterpret_cast<VoidCall>(reinterpret_cast<uint8_t*>(client)+0x534d0);
        const auto nativePick=reinterpret_cast<PickCall>(reinterpret_cast<uint8_t*>(client)+0x549f0);
        CaineDialogueV1 output{};output.size=sizeof(output);int owner{},other{};
        for (int i=0;i<128;++i) {
            capture(dialog.data(),packet.data());paint(hud.data());
            Check(caine::ReadNativeDialogue(output),"copied native dialogue unavailable");
            Check(std::string(output.opening)=="Hello \xc3\xa9." && output.responseCount==2 && output.line==1,"native text/row/count conversion");
            bool wrongThread{};std::thread thread([&]{CaineDialogueV1 value{};wrongThread=caine::ReadNativeDialogue(value);});thread.join();Check(!wrongThread,"cross-thread native access");
            Check(caine::ClaimNativeDialogue(&owner,output.token,true),"owner claim");
            Check(!caine::ClaimNativeDialogue(&other,output.token,true),"exclusive owner");
            const auto before=picks;nativePick(hud.data(),0,false);Check(picks==before,"native keyboard bypassed owned UI");
            Check(!caine::QueueNativeDialoguePick(&owner,output.token+1,0) && !caine::QueueNativeDialoguePick(&owner,output.token,2),"stale or unavailable choice accepted");
            Check(caine::QueueNativeDialoguePick(&owner,output.token,1),"queue canonical choice");
            Put(hud.data(),0x1b3,uint8_t(1));paint(hud.data());Check(picks==before,"choice ran while client was waiting");
            Put(hud.data(),0x1b3,uint8_t(0));paint(hud.data());Check(picks==before+1 && choice==1 && forced,"original native action index/ABI");
        }
        Check(caine::ClaimNativeDialogue(&owner,output.token,true),"claim before serial change");
        Check(caine::QueueNativeDialoguePick(&owner,output.token,0),"queue before serial change");
        const auto before=picks;Put(entities.data(),12+8,3u);paint(hud.data());
        Check(!caine::ReadNativeDialogue(output) && picks==before,"recycled entity executed stale action");
        Put(entities.data(),12+8,2u);capture(dialog.data(),packet.data());paint(hud.data());Check(caine::ReadNativeDialogue(output),"new packet recovery");
        reinterpret_cast<ActiveCall>(reinterpret_cast<uint8_t*>(client)+0x55360)(hud.data(),false);
        Check(!caine::ReadNativeDialogue(output),"closed HUD retained context");
        capture(dialog.data(),packet.data());paint(hud.data());Check(caine::ReadNativeDialogue(output),"dialogue recovery");
        reinterpret_cast<VoidCall>(reinterpret_cast<uint8_t*>(game)+0xe5240)(dialog.data());
        Check(releases==1 && !caine::ReadNativeDialogue(output),"dialogue release lifetime");
        capture(dialog.data(),packet.data());paint(hud.data());Put(dialog.data(),0x2834,0);capture(dialog.data(),packet.data());
        Check(!caine::ReadNativeDialogue(output),"auto-end packet retained previous context");
        std::cout<<"CAINE_DIALOGUE_NATIVE_OK: installed DLL profile guards, production detours and x86 entry ABIs, UTF-8 context, native choices, serial reuse, closure/release and stale response rejection; full gameplay pending\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
