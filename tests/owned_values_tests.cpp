#include <caine/serialization.hpp>
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
int main() {
    try {
        for (const auto& value:{R"({"kind":"null","value":null})",R"({"kind":"int32","value":-2147483648})",R"({"kind":"float64","value":1.25})",R"({"kind":"utf8","value":"long\u0000text"})",R"({"kind":"enum","value":{"domain":"clan","value":7}})"}) {
            const auto result=caine::SerializeOwned(std::string("{\"type\":\"caine.scalar\",\"version\":1,\"value\":")+value+"}");
            Check(caine::SerializeOwned(result)==result,"typed scalar round trip");
        }
        const std::string text(1024*1024,'x');
        const auto longValue=caine::SerializeOwned("{\"type\":\"caine.value\",\"version\":1,\"future\":{\"data\":[1,true,null]},\"value\":\""+text+"\"}");
        Check(caine::SerializeOwned(longValue)==longValue && longValue.find("future")!=std::string::npos,"long strings/unknown fields preserved");
        for (const auto& value:{R"({"type":"caine.entity","version":1,"value":{"map":"maps/sm_hub.bsp","identity":"npc.jeanette"}})",R"({"type":"caine.dialogue-context","version":1,"value":{"source":"dlg/a.dlg","line":1,"future":true}})"})
            Check(caine::SerializeOwned(caine::SerializeOwned(value))==caine::SerializeOwned(value),"stable persistent context");
        for (const auto& value:{R"({"type":"unknown","version":1,"value":null})",R"({"type":"caine.value","version":2,"value":null})",R"({"type":"caine.value","version":1,"value":0,"value":1})",R"({"type":"caine.scalar","version":1,"value":{"kind":"int32","value":18446744073709551615}})",R"({"type":"caine.entity","version":1,"value":{"map":"sm_hub","identity":"npc.a","handle":123}})",R"({"type":"caine.dialogue-context","version":1,"value":{"source":"dlg/a.dlg","line":1,"token":7}})"}) {
            bool rejected{};try { caine::SerializeOwned(value); } catch (...) { rejected=true; }
            Check(rejected,"incompatible/ambiguous/ephemeral data accepted");
        }
        std::string nested="0";for (int i=0;i<34;++i) nested="["+nested+"]";
        bool rejected{};try { caine::SerializeOwned("{\"type\":\"caine.value\",\"version\":1,\"value\":"+nested+"}"); } catch (...) { rejected=true; }
        Check(rejected,"nesting bound");
        std::cout<<"CAINE_OWNED_VALUES_OK: typed scalars, enums, nested collections, embedded NUL, long strings, stable references, unknown fields, incompatible versions and ephemeral reference rejection\n";
        return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
