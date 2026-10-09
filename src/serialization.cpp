#include <caine/serialization.hpp>
#include <json.hpp>
#include <cmath>
#include <algorithm>
#include <stdexcept>
namespace caine {
namespace {
using Json=nlohmann::json;
constexpr size_t Maximum=64*1024*1024;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
void Value(const Json& value,size_t depth,size_t& count) {
    Check(depth<=32 && ++count<=1000000,"Owned value exceeds depth/item limits");
    Check(!value.is_binary() && !value.is_discarded(),"Unsupported owned value kind");
    if (value.is_number_float()) Check(std::isfinite(value.get<double>()),"Non-finite owned number");
    if (value.is_string()) Check(value.get_ref<const std::string&>().size()<=Maximum,"Owned string too large");
    if (value.is_structured()) for (const auto& entry:value) Value(entry,depth+1,count);
}
void Scalar(const Json& value) {
    Check(value.is_object() && value.contains("kind") && value.contains("value"),"Typed scalar requires kind/value");
    const auto kind=value.at("kind").get<std::string>();const auto& body=value.at("value");
    if (kind=="null") Check(body.is_null(),"Null scalar mismatch");
    else if (kind=="int32") Check(body.is_number_integer() && (!body.is_number_unsigned() || body.get<uint64_t>()<=INT32_MAX) && body.get<int64_t>()>=INT32_MIN && body.get<int64_t>()<=INT32_MAX,"Int32 scalar out of range");
    else if (kind=="float64") Check(body.is_number() && std::isfinite(body.get<double>()),"Float64 scalar mismatch");
    else if (kind=="utf8") Check(body.is_string(),"UTF-8 scalar mismatch");
    else if (kind=="enum") Check(body.is_object() && body.contains("domain") && body.at("domain").is_string() && !body.at("domain").get<std::string>().empty() && body.contains("value") && body.at("value").is_number_integer(),"Enum requires its domain and integer value");
    else throw std::runtime_error("Unsupported scalar kind");
}
void Entity(const Json& value) {
    Check(value.is_object() && value.contains("map") && value.at("map").is_string() && value.contains("identity") && value.at("identity").is_string(),"Persistent entity requires map and stable identity");
    const auto map=value.at("map").get<std::string>();const auto identity=value.at("identity").get<std::string>();
    Check(!map.empty() && map.size()<=260 && map.find("..") == std::string::npos && map.find_first_of("\\:")==std::string::npos && !identity.empty() && identity.size()<=1024,"Invalid persistent entity identity");
    Check(!value.contains("pointer") && !value.contains("address") && !value.contains("handle") && !value.contains("native_token"),"Native pointer/handle/token is not a persistent entity identity");
}
}
std::vector<std::string> SerializationTypes() { return {"caine.scalar","caine.value","caine.entity","caine.dialogue-context"}; }
std::string SerializeOwned(const std::string& input) {
    Check(!input.empty() && input.size()<=Maximum,"Owned envelope exceeds 64 MiB");
    // Reject duplicate members rather than silently reinterpret an ambiguous record.
    std::vector<std::vector<std::string>> keys;
    const auto record=Json::parse(input,[&](int,Json::parse_event_t event,Json& value) {
        if (event==Json::parse_event_t::object_start) keys.emplace_back();
        else if (event==Json::parse_event_t::object_end) keys.pop_back();
        else if (event==Json::parse_event_t::key) {
            const auto key=value.get<std::string>();auto& seen=keys.back();
            Check(std::find(seen.begin(),seen.end(),key)==seen.end(),"Duplicate owned field");seen.push_back(key);
        }
        return true;
    });
    Check(record.is_object() && record.contains("type") && record.contains("version") && record.contains("value"),"Owned type/version/value required");
    Check(record.at("version").is_number_integer() && record.at("version")==1,"Unsupported owned schema version");
    const auto type=record.at("type").get<std::string>();const auto& value=record.at("value");
    size_t count{};Value(record,0,count);
    if (type=="caine.scalar") Scalar(value);
    else if (type=="caine.entity") Entity(value);
    else if (type=="caine.dialogue-context") {
        Check(value.is_object() && value.contains("source") && value.at("source").is_string() && value.contains("line") && value.at("line").is_number_integer() && (!value.at("line").is_number_unsigned() || value.at("line").get<uint64_t>()<=INT32_MAX) && value.at("line").get<int64_t>()>=0 && value.at("line").get<int64_t>()<=INT32_MAX,"Dialogue source/row required");
        Check(!value.contains("token") && !value.contains("npcHandle") && !value.contains("playerHandle"),"Live dialogue capabilities cannot be persisted");
    } else Check(type=="caine.value","Unknown owned type");
    const auto result=record.dump();Check(result.size()<=Maximum,"Canonical owned envelope exceeds 64 MiB");return result;
}
}
