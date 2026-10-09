#include <caine/native_bridge.hpp>
#include <caine/native_types.hpp>
#include <array>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace caine {
void TestNativeOriginals(void*,void*,void*,void*,void*,void*);
void TestNativeInteractionContinuation(void*);
void TestExpirePendingUse();void TestNativeFovConfig(int);void TestReloadFovConfig();
}
namespace {
unsigned paints{},picks{},releases{};int choice{};bool forced{};
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void __fastcall Packet(void*,void*,void*) {}
void __fastcall Release(void*,void*) { ++releases; }
void __fastcall Paint(void*,void*) { ++paints; }
void __fastcall Active(void*,void*,bool) {}
void __fastcall Pick(void*,void*,int index,bool force) { ++picks;choice=index;forced=force; }
const char* __fastcall Filename(void*,void*) { return "santa monica/jeanette.dlg"; }
__declspec(naked) void UseReturn() { __asm ret }
__declspec(naked) void __cdecl UseEntry(void*,void*,void*) {
    __asm {
        push esi
        push edi
        mov eax,dword ptr [esp+12]
        mov edi,dword ptr [esp+16]
        mov esi,dword ptr [esp+20]
        call eax
        pop edi
        pop esi
        ret
    }
}
template<class T> void Put(void* object,size_t offset,T value) { memcpy(static_cast<uint8_t*>(object)+offset,&value,sizeof(value)); }
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==3,"supply installed game and client DLLs");
        const auto game=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        const auto client=LoadLibraryExW(argv[2],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        Check(game && client,"map installed native modules");
        Check(caine::InstallNativeBridge([](const auto& message){std::cout<<message<<'\n';}),"exact production profile and hook batch");
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
        // Execute the real installed PlayerUse boundary with its inspected EDI /
        // ESI contract. Substitute the continuation, not the production detour.
        std::array<uint8_t,0xac> playerEntity{};std::array<uint8_t,0x3000> playerComponent{};
        Put(playerEntity.data(),0xa8,playerComponent.data());
        Put(playerComponent.data(),0,reinterpret_cast<uint8_t*>(game)+0x4a271c);
        // Reproduce the live game's absence of a console-command client. No
        // stubbed player-lookup function is used by the production resolver.
        Put(reinterpret_cast<uint8_t*>(game),0x70b25c,UINT32_MAX);
        Put(entities.data(),24+4,playerEntity.data());
        caine::TestNativeInteractionContinuation(reinterpret_cast<void*>(UseReturn));
        const auto window=CreateWindowExW(0,L"STATIC",L"CAINE native interaction test",0,0,0,10,10,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(window!=nullptr,"test game-thread window");
        const auto use=reinterpret_cast<uint8_t*>(game)+0x167aa6;
        auto ambient=[&] { UseEntry(use,&npc,playerComponent.data());caine::TestExpirePendingUse();caine::PulseNativeBridge(window); };
        caine::InvalidateNativeDialogue();caine::TestNativeFovConfig(0);
        ambient();Check(!caine::ReadNativeDialogue(output),"held or scripted use started ambient UI");
        Put(playerComponent.data(),0x208c,0x20u);UseEntry(use,&npc,playerComponent.data());
        caine::PulseNativeBridge(window);Check(!caine::ReadNativeDialogue(output),"ambient UI preempted native dialogue wait");
        caine::TestExpirePendingUse();caine::PulseNativeBridge(window);
        Check(caine::ReadNativeDialogue(output) && output.responseCount==0 && output.line==-1 && !*output.opening,"accepted ambient context");
        Check(caine::ClaimNativeDialogue(&owner,output.token,true),"ambient claim");
        const auto ambientPaints=paints,ambientPicks=picks;paint(hud.data());Check(paints==ambientPaints+1,"ambient UI suppressed unrelated HUD");
        Check(!caine::QueueNativeDialoguePick(&owner,output.token,0),"ambient interaction invented native quest action");
        Check(caine::QueueNativeDialoguePick(&owner,output.token,-2) && !caine::ReadNativeDialogue(output) && picks==ambientPicks,"ambient close called native HUD handler");
        UseEntry(use,&npc,playerComponent.data());Put(dialog.data(),0x2834,1);capture(dialog.data(),packet.data());
        reinterpret_cast<VoidCall>(reinterpret_cast<uint8_t*>(game)+0xe5240)(dialog.data());
        caine::TestExpirePendingUse();caine::PulseNativeBridge(window);
        Check(caine::ReadNativeDialogue(output) && output.responseCount==0,"auto-end native float swallowed accepted NPC use");
        caine::InvalidateNativeDialogue();
        UseEntry(use,&npc,playerComponent.data());Put(dialog.data(),0x2834,2);capture(dialog.data(),packet.data());paint(hud.data());
        caine::TestExpirePendingUse();caine::PulseNativeBridge(window);
        Check(caine::ReadNativeDialogue(output) && output.responseCount==2,"native quest dialogue lost priority");
        caine::InvalidateNativeDialogue();UseEntry(use,&npc,playerComponent.data());Put(entities.data(),12+8,3u);
        caine::TestExpirePendingUse();caine::PulseNativeBridge(window);Check(!caine::ReadNativeDialogue(output),"recycled pending NPC accepted");
        Put(entities.data(),12+8,2u);ambient();Check(caine::ReadNativeDialogue(output),"ambient recovery after serial change");
        Check(caine::ClaimNativeDialogue(&owner,output.token,true),"ambient re-claim");
        caine::ClaimNativeDialogue(&owner,0,false);Check(!caine::ReadNativeDialogue(output),"ambient release reopened conversation");
        UseEntry(use,&npc,playerComponent.data());caine::InvalidateNativeDialogue();caine::TestExpirePendingUse();caine::PulseNativeBridge(window);
        Check(!caine::ReadNativeDialogue(output),"save/load boundary retained pending use");
        UseEntry(use,&npc,playerComponent.data());caine::ClaimNativeDialogue(&owner,0,false);caine::TestExpirePendingUse();caine::PulseNativeBridge(window);
        Check(!caine::ReadNativeDialogue(output),"mod load-boundary release retained pending use");
        std::vector<std::string> commands;
        auto command=[&](const std::string& text) { commands.push_back(text);Put(playerComponent.data(),0x1e78,std::stoi(text.substr(4))); };
        caine::TestNativeFovConfig(135);caine::PulseNativeBridge(window);
        Check(*reinterpret_cast<int*>(playerComponent.data()+0x1e78)==0,"FOV wrote native memory without using the console route");
        caine::PulseNativeBridge(window,command);
        Check(commands.back()=="fov 135\n" && *reinterpret_cast<int*>(playerComponent.data()+0x1e78)==135,"saved FOV console route");
        Check(caine::SetNativeFieldOfView(127),"FOV preference persistence");caine::TestReloadFovConfig();
        Check(caine::NativeFieldOfView()==127,"FOV did not survive config reload");Sleep(251);caine::PulseNativeBridge(window,command);
        Check(commands.back()=="fov 127\n","reloaded FOV used wrong command");
        Check(*reinterpret_cast<int*>(playerComponent.data()+0x1e78)==127,"reloaded FOV preference not applied");
        // Match the read-only live snapshot: player slot 1, Jack slot 950,
        // serial zero, and the player component adapter pointing to itself.
        caine::InvalidateNativeDialogue();entities.fill(0);
        Put(entities.data(),12+4,playerComponent.data());Put(playerComponent.data(),0xa8,playerComponent.data());
        Put(entities.data(),950*12+4,&npc);caine::TestNativeFovConfig(0);
        ambient();Check(caine::ReadNativeDialogue(output) && output.playerHandle==1 && output.npcHandle==950,"actual playing layout rejected outside command context");
        Put(entities.data(),12+4,static_cast<void*>(nullptr));caine::PulseNativeBridge(window);
        Check(!caine::ReadNativeDialogue(output),"removed player retained ambient interaction");DestroyWindow(window);
        std::cout<<"CAINE_DIALOGUE_NATIVE_OK: installed DLL profile guards, production detours and x86 entry ABIs, UTF-8 context, native choices, serial reuse, closure/release and stale response rejection; full gameplay pending\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
