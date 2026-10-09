#include <caine/intro_skip.hpp>
#include <iostream>
#include <stdexcept>
#include <fstream>
#include <cmath>
#include <cstring>
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int wmain(int argc,wchar_t** argv) {
    try {
        using caine::IntroHold;
        Check(caine::IsOpeningLevel("sp_theatre") && caine::IsOpeningLevel("maps/sp_theatre.bsp") && caine::IsOpeningLevel("MAPS\\SP_THEATRE.BSP"),"opening level name formats");
        for (const auto* name:{"sp_tutorial_1","sm_haven_1","sp_theatre_extra","../sp_theatre.bsp","maps/not_sp_theatre.bsp",""}) Check(!caine::IsOpeningLevel(name),"skip escaped the opening level");
        IntroHold hold;
        for (uint64_t t=0;t<=2000;t+=100) Check(!hold.Update(true,true,true,t),"held Escape at scene entry must not skip");
        hold.Update(true,true,false,2100);
        for (uint64_t t=2200;t<3700;t+=100) Check(!hold.Update(true,true,true,t),"short hold fired");
        Check(hold.Progress()>0.9f && hold.Progress()<1,"hold progress missing");
        Check(hold.Update(true,true,true,3700),"full hold did not fire");
        Check(hold.Requested() && hold.Progress()==1,"skip latch missing");
        for (uint64_t t=3800;t<5500;t+=100) Check(!hold.Update(true,true,t%200==0,t),"duplicate skip request");
        hold.Update(false,false,false,5500);
        Check(!hold.Requested() && hold.Progress()==0,"map exit retained skip request");
        hold.Update(true,true,false,5600);
        hold.Update(true,true,true,5700);hold.Update(true,true,true,5800);
        hold.Update(true,true,false,5900);
        Check(hold.Progress()==0,"release did not cancel hold");
        for (uint64_t t=6000;t<7500;t+=100) Check(!hold.Update(true,true,true,t),"release failed to restart timer");
        Check(hold.Update(true,true,true,7500),"second intro could not skip");
        for (int reason=0;reason<3;++reason) {
            IntroHold cancel;
            cancel.Update(true,true,false,0);cancel.Update(true,true,true,100);cancel.Update(true,true,true,200);
            if (reason==0) cancel.Update(true,false,true,300); // focus, pause or console
            if (reason==1) cancel.Update(true,true,true,1000); // loading/render stall
            if (reason==2) cancel.Update(true,true,true,50); // clock reversal
            for (uint64_t t=1100;t<3500;t+=100) Check(!cancel.Update(true,true,true,t),"cancelled hold resumed while key was still held");
            cancel.Update(true,true,false,3500);
            for (uint64_t t=3600;t<5100;t+=100) Check(!cancel.Update(true,true,true,t),"fresh hold completed too soon");
            Check(cancel.Update(true,true,true,5100),"fresh press after cancellation failed");
        }
        IntroHold shortDuration(0),longDuration(99999);
        shortDuration.Update(true,true,false,0);longDuration.Update(true,true,false,0);
        shortDuration.Update(true,true,true,100);longDuration.Update(true,true,true,100);
        for (uint64_t t=200;t<600;t+=100) Check(!shortDuration.Update(true,true,true,t),"minimum duration clamp");
        Check(shortDuration.Update(true,true,true,600),"minimum duration");
        for (uint64_t t=200;t<5100;t+=100) Check(!longDuration.Update(true,true,true,t),"maximum duration clamp");
        Check(longDuration.Update(true,true,true,5100),"maximum duration");
        Check(std::string(caine::IntroSkipCommands)=="vskip_intro\nent_fire embrace_o_matic Start\n","native skip command ordering");
        const auto path=std::filesystem::temp_directory_path()/(L"caine-intro-"+std::to_wstring(GetCurrentProcessId())+L".bsp");
        auto write=[&](const std::string& entities,uint32_t magic=0x50534256u,uint32_t extra=0) {
            uint32_t h[]={magic,17,16,static_cast<uint32_t>(entities.size())+extra};
            std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(h),sizeof(h));out<<entities;
        };
        const std::string scene="{\n\"classname\" \"logic_choreographed_scene\"\n\"targetname\"\t\"embrace_o_matic\"\n}\n";
        const std::string change="{\"classname\" \"trigger_changelevel\"\n\"targetname\" \"tutorial_change\"\n\"map\" \"sp_tutorial_1\"\n\"landmark\" \"tutorial\"}";
        write(scene+change);Check(caine::IntroMapSupported(path),"valid entity/transition contract rejected");
        write(scene);Check(!caine::IntroMapSupported(path),"absent tutorial transition accepted");
        write(change);Check(!caine::IntroMapSupported(path),"absent scene accepted");
        write(scene+change,0);Check(!caine::IntroMapSupported(path),"bad BSP signature accepted");
        write(scene+change,0x50534256u,1);Check(!caine::IntroMapSupported(path),"truncated entity lump accepted");
        std::filesystem::remove(path);
        for (int i=1;i<argc;++i) if (std::filesystem::exists(argv[i])) Check(caine::IntroMapSupported(argv[i]),"installed opening map contract changed");
        std::cout<<"CAINE_INTRO_HOLD_OK: fresh press, full hold, release, focus/pause/console/stall cancellation, one request, map reset, bounded settings and BSP contracts passed\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
