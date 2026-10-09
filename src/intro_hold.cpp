#include <caine/intro_skip.hpp>
#include <algorithm>
#include <cctype>
#include <fstream>
#include <cstring>

namespace caine {
IntroHold::IntroHold(uint32_t milliseconds):milliseconds_(std::clamp(milliseconds,500u,5000u)) {}
bool IntroHold::Update(bool opening, bool eligible, bool down, uint64_t now) {
    if (!opening) { armed_=holding_=requested_=sampled_=false;progress_=0;return false; }
    const bool gap=sampled_ && (now<last_ || now-last_>250);
    last_=now;sampled_=true;
    if (!eligible || gap) { armed_=holding_=false;progress_=0;return false; }
    if (requested_) { progress_=1;return false; }
    // A held key at scene entry, after Alt-Tab or after a stall is not a press.
    if (!down) { armed_=true;holding_=false;progress_=0;return false; }
    if (!armed_) return false;
    if (!holding_) { started_=now;holding_=true; }
    progress_=std::min(1.0f,static_cast<float>(now-started_)/milliseconds_);
    if (progress_<1) return false;
    requested_=true;return true;
}
bool IsOpeningLevel(const std::string& name) {
    if (name.empty() || name.size()>260) return false;
    auto value=name;
    std::transform(value.begin(),value.end(),value.begin(),[](unsigned char c){return static_cast<char>(std::tolower(c));});
    if (value=="sp_theatre") return true;
    return value=="maps/sp_theatre.bsp" || value=="maps\\sp_theatre.bsp" || value=="sp_theatre.bsp";
}
bool IntroMapSupported(const std::filesystem::path& path) {
    std::ifstream input(path,std::ios::binary);
    uint32_t header[4]{};input.read(reinterpret_cast<char*>(header),sizeof(header));
    if (!input || header[0]!=0x50534256u || header[3]==0 || header[3]>2*1024*1024) return false;
    input.seekg(0,std::ios::end);const auto size=input.tellg();
    if (size<0 || static_cast<uint64_t>(header[2])+header[3]>static_cast<uint64_t>(size)) return false;
    std::string entities(header[3],'\0');input.seekg(header[2]);input.read(entities.data(),header[3]);
    if (!input) return false;
    bool scene=false,change=false;
    for (size_t begin=0;(begin=entities.find('{',begin))!=std::string::npos;) {
        const auto end=entities.find('}',begin);if (end==std::string::npos) return false;
        const auto block=entities.substr(begin,end-begin);begin=end+1;
        // Entity-lump strings are quoted pairs. Accept whitespace differences.
        auto pair=[&](const char* key,const char* value) {
            const auto marker=std::string("\"")+key+"\"";const auto at=block.find(marker);
            if (at==std::string::npos) return false;
            auto pos=at+marker.size();while (pos<block.size() && std::isspace(static_cast<unsigned char>(block[pos]))) ++pos;
            const auto expected=std::string("\"")+value+"\"";return block.compare(pos,expected.size(),expected)==0;
        };
        scene|=pair("classname","logic_choreographed_scene") && pair("targetname","embrace_o_matic");
        change|=pair("classname","trigger_changelevel") && pair("targetname","tutorial_change") && pair("map","sp_tutorial_1") && pair("landmark","tutorial");
    }
    return scene && change;
}
}
