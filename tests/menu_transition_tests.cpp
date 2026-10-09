#include <caine/native_menu_state.hpp>
#include <array>
#include <limits>
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message) {if(!value)throw std::runtime_error(message);}
int main() {
    try {
        const int mainItems[]{0,1,4,10},pauseItems[]{3,4,9,11};
        Check(!caine::NativeMenuState::ConfirmQuit(10,mainItems,4),"main-menu Quit warned about nonexistent unsaved gameplay");
        Check(caine::NativeMenuState::ConfirmQuit(10,pauseItems,4) && caine::NativeMenuState::ConfirmQuit(13,pauseItems,4),"pause desktop exit lost its unsaved-progress guard");
        Check(caine::NativeMenuState::ConfirmQuit(9,pauseItems,4),"pause quit-to-main lost its unsaved-progress guard");
        Check(!caine::NativeMenuState::ConfirmQuit(10,mainItems,4),"returning to main menu retained a stale pause warning");
        std::array<uint8_t,0x2ec> native{};float alpha=83,secondary=19,target{};int pending{};
        for(const auto action:{9,10,11,-1}) {
            pending=action;memcpy(native.data()+0x2e8,&pending,4);
            for(const float value:{0.f,255.f}) {
                target=value;memcpy(native.data()+0x2dc,&target,4);memcpy(native.data()+0x2d8,&alpha,4);memcpy(native.data()+0x2e0,&secondary,4);
                caine::NativeMenuState::CompleteTransition(native.data());
                float primary{},other{};memcpy(&primary,native.data()+0x2d8,4);memcpy(&other,native.data()+0x2e0,4);
                Check(primary==target && other==target,"native animation did not complete immediately");
                Check(caine::NativeMenuState::ChildBusy(true,native.data())==(action==-1),"pending exit/resume mistaken for native child dialog");
                Check(!caine::NativeMenuState::ChildBusy(false,native.data()),"native child busy manufactured");
            }
        }
        target=std::numeric_limits<float>::quiet_NaN();memcpy(native.data()+0x2dc,&target,4);caine::NativeMenuState::CompleteTransition(native.data());
        float primary{};memcpy(&primary,native.data()+0x2d8,4);Check(std::isfinite(primary),"invalid native target propagated");
        std::cout<<"CAINE_MENU_TRANSITION_OK: production pending-action classification, immediate native alpha endpoints, return-to-menu/quit/resume and invalid target rejection; live art transitions pending\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
