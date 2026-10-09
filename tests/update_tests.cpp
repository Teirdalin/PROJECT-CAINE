#include <caine/update.hpp>
#include <shellapi.h>
#include <iostream>
#include <stdexcept>
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(){try{
    Check(caine::NewerUpdateVersion("0.3.10-framework-dev","0.3.9-framework-dev"),"numeric version order");
    Check(!caine::NewerUpdateVersion("0.3.9-framework-dev","0.3.9-framework-dev"),"same version");
    Check(caine::NewerUpdateVersion("v0.3.9","0.3.9-framework-dev"),"stable promotion");
    Check(!caine::NewerUpdateVersion("0.3.9-framework-dev","0.3.9"),"preview downgrade");
    const auto url="https://github.com/Teirdalin/PROJECT-CAINE/releases/download/v0.3.10-framework-dev/PROJECT-CAINE-update.zip";
    Check(caine::TrustedUpdateUrl(url),"own release URL");
    for(const auto* bad:{"http://github.com/Teirdalin/PROJECT-CAINE/releases/download/v1/PROJECT-CAINE-update.zip",
       "https://github.com.evil.test/Teirdalin/PROJECT-CAINE/releases/download/v1/PROJECT-CAINE-update.zip",
       "https://release-assets.githubusercontent.com.evil.test/file","https://github.com/other/PROJECT-CAINE/releases/download/v1/PROJECT-CAINE-update.zip",
       "https://github.com/Teirdalin/PROJECT-CAINE/releases/download/v1/evil.exe"})Check(!caine::TrustedUpdateUrl(bad),"untrusted URL");
    Check(caine::TrustedUpdateUrl("https://release-assets.githubusercontent.com/a/b?token=abc"),"signed asset redirect");
    const std::string asset="{\"name\":\"PROJECT-CAINE-update.zip\",\"state\":\"uploaded\",\"size\":500,\"digest\":\"sha256:"+std::string(64,'a')+"\",\"browser_download_url\":\""+url+"\"}";
    const std::string feed="[{\"tag_name\":\"v0.3.10-framework-dev\",\"draft\":false,\"prerelease\":true,\"assets\":["+asset+"]}]";
    Check(caine::SelectReleaseUpdate(feed,caine::UpdateVersion,true).version=="0.3.10-framework-dev","preview feed");
    Check(caine::SelectReleaseUpdate(feed,caine::UpdateVersion,false).version.empty(),"stable excludes preview");
    Check(caine::SelectReleaseUpdate(feed,"0.3.10-framework-dev",true).version.empty(),"no same-version update");
    auto invalid=feed;invalid.replace(invalid.find("sha256:"),7,"sha512:");bool rejected{};
    try{caine::SelectReleaseUpdate(invalid,caine::UpdateVersion,true);}catch(...){rejected=true;}Check(rejected,"missing SHA256 must fail closed");
    for(const auto& value:{std::wstring(L"F:\\Game Path\\"),std::wstring(L"quoted \"argument\""),std::wstring(L""),std::wstring(L"-game Unofficial_Patch")}) {
        const auto cmd=L"test.exe "+caine::QuoteUpdateArgument(value);int count{};auto args=CommandLineToArgvW(cmd.c_str(),&count);
        Check(args && count==2 && value==args[1],"Windows argument quote round trip");LocalFree(args);
    }
    Check(caine::UpdateWide(caine::UpdateUtf8(L"CAINE \u00e9 \u4e16"))==L"CAINE \u00e9 \u4e16","Unicode round trip");
    std::cout<<"CAINE_UPDATE_PROTOCOL_OK: versions, channels, digest validation, trusted hosts and restart argument quoting\n";return 0;
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}}
