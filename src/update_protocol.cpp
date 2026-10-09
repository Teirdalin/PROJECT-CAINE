#include <caine/update.hpp>
#include <json.hpp>
#include <algorithm>
#include <array>
#include <cctype>
#include <limits>

namespace caine {
std::wstring UpdateWide(const std::string& text) {
    if (text.empty()) return {};
    const int count=MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0);
    if (!count) throw std::runtime_error("Invalid UTF-8 update text");
    std::wstring result(static_cast<size_t>(count),L'\0');
    MultiByteToWideChar(CP_UTF8,MB_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count);
    return result;
}
std::string UpdateUtf8(const std::wstring& text) {
    if (text.empty()) return {};
    const int count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),nullptr,0,nullptr,nullptr);
    if (!count) throw std::runtime_error("Invalid Unicode update text");
    std::string result(static_cast<size_t>(count),'\0');
    WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),static_cast<int>(text.size()),result.data(),count,nullptr,nullptr);
    return result;
}
std::wstring QuoteUpdateArgument(const std::wstring& text) {
    std::wstring result=L"\""; size_t slashes{};
    for (wchar_t c:text) {
        if (c==L'\\') { ++slashes;continue; }
        result.append(slashes*(c==L'\"'?2:1),L'\\');slashes=0;
        if (c==L'\"') result+=L'\\';
        result+=c;
    }
    result.append(slashes*2,L'\\');return result+L'\"';
}
namespace {
struct Version { std::array<unsigned,3> parts{}; bool preview{}; };
Version ParseVersion(std::string text) {
    if (!text.empty() && text.front()=='v') text.erase(0,1);
    Version result;
    const auto suffix=text.find('-');
    if (suffix!=std::string::npos) {
        const auto label=text.substr(suffix);
        if (label!="-framework-dev") throw std::runtime_error("Unsupported CAINE version suffix");
        result.preview=true;text.resize(suffix);
    }
    size_t start{};
    for(size_t i=0;i<3;++i) {
        const auto end=text.find('.',start);
        const auto part=text.substr(start,end==std::string::npos?end:end-start);
        if (part.empty() || part.size()>6 || !std::all_of(part.begin(),part.end(),[](unsigned char c){return std::isdigit(c)!=0;}))
            throw std::runtime_error("Malformed CAINE release version");
        result.parts[i]=static_cast<unsigned>(std::stoul(part));
        if ((i<2)==(end==std::string::npos)) throw std::runtime_error("Malformed CAINE release version");
        start=end+1;
    }
    return result;
}
}
bool NewerUpdateVersion(const std::string& candidate,const std::string& current) {
    const auto a=ParseVersion(candidate),b=ParseVersion(current);
    return a.parts>b.parts || (a.parts==b.parts && b.preview && !a.preview);
}
bool TrustedUpdateUrl(const std::string& url,bool api) {
    if (url.size()>2048 || url.find_first_of("\\\r\n\t #")!=std::string::npos) return false;
    if (api) return url=="https://api.github.com/repos/"+std::string(UpdateRepository)+"/releases?per_page=20";
    const std::string prefix="https://github.com/"+std::string(UpdateRepository)+"/releases/download/";
    if (url.compare(0,prefix.size(),prefix)==0) {
        const auto tagEnd=url.find('/',prefix.size());
        return tagEnd!=std::string::npos && tagEnd>prefix.size() && url.substr(tagEnd+1)==UpdateAsset &&
            url.substr(prefix.size(),tagEnd-prefix.size()).find_first_of("?%:")==std::string::npos;
    }
    // Only GitHub's signed binary asset host is accepted during redirect traversal.
    return url.rfind("https://release-assets.githubusercontent.com/",0)==0 ||
           url.rfind("https://objects.githubusercontent.com/",0)==0;
}
ReleaseUpdate SelectReleaseUpdate(const std::string& text,const std::string& current,bool preview) {
    if(text.size()>2*1024*1024) throw std::runtime_error("Release feed exceeds size limit");
    const auto releases=nlohmann::json::parse(text);
    if(!releases.is_array() || releases.size()>20) throw std::runtime_error("Unexpected release feed");
    ReleaseUpdate selected;
    for (const auto& release:releases) {
        if(release.value("draft",true) || (!preview && release.value("prerelease",true))) continue;
        auto version=release.value("tag_name",std::string{});
        if(!version.empty() && version.front()=='v')version.erase(0,1);
        try {
            if(!NewerUpdateVersion(version,current) || (!selected.version.empty() && !NewerUpdateVersion(version,selected.version))) continue;
        } catch(const std::exception&) { continue; } // Unrelated/malformed release tags are not commands.
        for(const auto& asset:release.at("assets")) {
            if(asset.value("name",std::string{})!=UpdateAsset || asset.value("state",std::string{})!="uploaded") continue;
            const auto url=asset.value("browser_download_url",std::string{});
            const auto digest=asset.value("digest",std::string{});
            const auto size=asset.value("size",uint64_t{});
            const std::string prefix="https://github.com/"+std::string(UpdateRepository)+"/releases/download/";
            if(!TrustedUpdateUrl(url) || url.compare(0,prefix.size(),prefix)!=0 || size==0 || size>128ull*1024*1024 ||
               digest.size()!=71 || digest.compare(0,7,"sha256:")!=0 ||
               !std::all_of(digest.begin()+7,digest.end(),[](char c){return (c>='0' && c<='9') || (c>='a' && c<='f');}))
                throw std::runtime_error("Release asset is missing a valid SHA-256 digest, URL or size");
            selected={version,url,digest.substr(7),size};break;
        }
    }
    return selected;
}
}
