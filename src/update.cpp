#include <caine/update.hpp>
#include <caine/logging.hpp>
#include <json.hpp>
#include <winhttp.h>
#include <objbase.h>
#include <algorithm>
#include <fstream>
#include <mutex>
#include <thread>

namespace caine {
namespace {
struct Internet { HINTERNET handle{}; ~Internet(){if(handle)WinHttpCloseHandle(handle);} };
std::mutex mutex;
UpdateSnapshot snapshot;
ReleaseUpdate release;
std::filesystem::path gameRoot, installRoot;
std::wstring originalCommand, originalDirectory;
std::function<void(const std::string&)> logger;
HANDLE wake{};
bool downloadRequested{}, restartClaimed{};
void Status(UpdatePhase phase,std::string message,float progress=0) {
    std::lock_guard<std::mutex> lock(mutex);
    const auto bucket=static_cast<int>(progress*10);
    if (snapshot.phase!=phase || static_cast<int>(snapshot.progress*10)!=bucket)
        TraceLog("CAINE_UPDATE_PROGRESS: phase="+std::to_string(static_cast<int>(phase))+" percent="+std::to_string(bucket*10));
    snapshot.phase=phase;snapshot.message=std::move(message);snapshot.progress=progress;
}
void Log(const std::string& line) { try{if(logger)logger(line);}catch(...){} }
std::string ReadJson(const std::filesystem::path& path) {
    if(std::filesystem::file_size(path)>2*1024*1024)throw std::runtime_error("Oversized local update metadata");
    std::ifstream input(path,std::ios::binary);
    if(!input)throw std::runtime_error("Cannot read local update metadata");
    return std::string(std::istreambuf_iterator<char>(input),{});
}
std::string Fetch(std::string url,bool api,const std::filesystem::path& output={},uint64_t expected=0) {
    Internet session{WinHttpOpen(L"PROJECT-CAINE/0.3.17",WINHTTP_ACCESS_TYPE_AUTOMATIC_PROXY,nullptr,nullptr,0)};
    if(!session.handle)throw std::runtime_error("Cannot initialize HTTPS update request");
    WinHttpSetTimeouts(session.handle,5000,5000,10000,15000);
    for(int hop=0;hop<6;++hop) {
        if(!TrustedUpdateUrl(url,api))throw std::runtime_error("Update redirected outside GitHub's permitted hosts");
        const auto wide=UpdateWide(url);
        URL_COMPONENTS parts{};parts.dwStructSize=sizeof(parts);parts.dwHostNameLength=static_cast<DWORD>(-1);
        parts.dwUrlPathLength=static_cast<DWORD>(-1);parts.dwExtraInfoLength=static_cast<DWORD>(-1);
        if(!WinHttpCrackUrl(wide.c_str(),0,0,&parts) || parts.nScheme!=INTERNET_SCHEME_HTTPS || parts.nPort!=443)
            throw std::runtime_error("Invalid HTTPS update URL");
        const std::wstring host(parts.lpszHostName,parts.dwHostNameLength);
        std::wstring path(parts.lpszUrlPath,parts.dwUrlPathLength);
        if(parts.dwExtraInfoLength)path.append(parts.lpszExtraInfo,parts.dwExtraInfoLength);
        Internet connection{WinHttpConnect(session.handle,host.c_str(),443,0)};
        Internet request{WinHttpOpenRequest(connection.handle,L"GET",path.c_str(),nullptr,nullptr,nullptr,WINHTTP_FLAG_SECURE)};
        DWORD disable=WINHTTP_DISABLE_REDIRECTS;
        if(!request.handle || !WinHttpSetOption(request.handle,WINHTTP_OPTION_DISABLE_FEATURE,&disable,sizeof(disable)))
            throw std::runtime_error("Cannot create update request");
        const wchar_t* headers=api?L"Accept: application/vnd.github+json\r\nX-GitHub-Api-Version: 2022-11-28\r\n":L"Accept: application/octet-stream\r\n";
        if(!WinHttpSendRequest(request.handle,headers,static_cast<DWORD>(-1),nullptr,0,0,0) || !WinHttpReceiveResponse(request.handle,nullptr))
            throw std::runtime_error("GitHub update connection failed (TLS, proxy, or network)");
        DWORD code{},length=sizeof(code);
        if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_STATUS_CODE|WINHTTP_QUERY_FLAG_NUMBER,nullptr,&code,&length,nullptr))
            throw std::runtime_error("Cannot read update response status");
        if(code==301 || code==302 || code==303 || code==307 || code==308) {
            length=0;WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_LOCATION,nullptr,nullptr,&length,nullptr);
            if(length==0 || length>8192)throw std::runtime_error("Invalid update redirect");
            std::wstring location(length/sizeof(wchar_t),L'\0');
            if(!WinHttpQueryHeaders(request.handle,WINHTTP_QUERY_LOCATION,nullptr,location.data(),&length,nullptr))throw std::runtime_error("Invalid update redirect");
            location.resize(wcslen(location.c_str()));url=UpdateUtf8(location);continue;
        }
        if(code!=200)throw std::runtime_error("GitHub update request returned HTTP "+std::to_string(code));
        std::ofstream file;
        if(!output.empty()){file.open(output,std::ios::binary|std::ios::trunc);if(!file)throw std::runtime_error("Cannot create staged download");}
        std::string body;uint64_t total{};char buffer[32768];
        for(;;) {
            DWORD read{};
            if(!WinHttpReadData(request.handle,buffer,sizeof(buffer),&read))throw std::runtime_error("Download was interrupted");
            if(!read)break;
            total+=read;
            if(total>(api?2ull*1024*1024:expected))throw std::runtime_error("Update response exceeds its declared size");
            if(api)body.append(buffer,read);
            else {file.write(buffer,read);if(!file)throw std::runtime_error("Cannot write staged update");
                Status(UpdatePhase::Downloading,"Downloading CAINE "+release.version,static_cast<float>(total)/static_cast<float>(expected));}
        }
        if(!api && total!=expected)throw std::runtime_error("Incomplete update download");
        return body;
    }
    throw std::runtime_error("Too many update redirects");
}
void VerifyLocalPath(const std::filesystem::path& path) {
    for(auto current=path;!current.empty() && current!=current.root_path();current=current.parent_path()) {
        const auto attributes=GetFileAttributesW(current.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&FILE_ATTRIBUTE_REPARSE_POINT))throw std::runtime_error("Refusing redirected update path");
    }
}
void StageHelper(const std::filesystem::path& zip) {
    const auto receipt=nlohmann::json::parse(ReadJson(installRoot/L"install.json"));
    if(receipt.value("product",std::string{})!="CAINE" || Sha256(installRoot.parent_path()/L"CAINE.asi")!=receipt.at("pluginSha256").get<std::string>())
        throw std::runtime_error("Installed CAINE differs from its ownership receipt. Update was stopped.");
    for(const auto* name:{L"Updater.exe",L"ApplyUpdate.ps1",L"Install-CAINE.ps1",L"Common.ps1"}) {
        const auto source=installRoot/name;VerifyLocalPath(source);
        const auto key="Bin/loader/CAINE/"+UpdateUtf8(name);
        if(Sha256(source)!=receipt.at("updaterFiles").at(key).get<std::string>())throw std::runtime_error("Installed updater differs from its receipt");
        std::filesystem::copy_file(source,zip.parent_path()/name);
    }
    FILETIME created{},exited{},kernel{},user{};
    if(!GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user))throw std::runtime_error("Cannot identify Bloodlines process");
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create updater handshake");
    wchar_t guid[40]{};StringFromGUID2(id,guid,40);
    const std::wstring eventName=L"Local\\CAINE.Update."+std::wstring(guid);
    HANDLE ready=CreateEventW(nullptr,TRUE,FALSE,eventName.c_str());
    if(!ready)throw std::runtime_error("Cannot create updater handshake");
    const auto requestPath=zip.parent_path()/L"request.json";
    nlohmann::json request={{"schema",1},{"gameRoot",UpdateUtf8(gameRoot.wstring())},{"zip",UpdateUtf8(zip.wstring())},
        {"sha256",release.sha256},{"version",release.version},{"gamePid",GetCurrentProcessId()},
        {"creationTime",(static_cast<uint64_t>(created.dwHighDateTime)<<32)|created.dwLowDateTime},
        {"commandLine",UpdateUtf8(originalCommand)},{"workingDirectory",UpdateUtf8(originalDirectory)},
        {"readyEvent",UpdateUtf8(eventName)}};
    {std::ofstream output(requestPath,std::ios::binary);output<<request.dump(2);if(!output){CloseHandle(ready);throw std::runtime_error("Cannot write update request");}}
    auto command=QuoteUpdateArgument((zip.parent_path()/L"Updater.exe").wstring())+L" --request "+QuoteUpdateArgument(requestPath.wstring());
    STARTUPINFOW startup{};startup.cb=sizeof(startup);PROCESS_INFORMATION process{};
    const BOOL started=CreateProcessW((zip.parent_path()/L"Updater.exe").c_str(),command.data(),nullptr,nullptr,FALSE,0,nullptr,zip.parent_path().c_str(),&startup,&process);
    if(!started){CloseHandle(ready);throw std::runtime_error("Cannot start CAINE installation helper");}
    CloseHandle(process.hThread);HANDLE waits[]{ready,process.hProcess};
    const DWORD result=WaitForMultipleObjects(2,waits,FALSE,15000);
    CloseHandle(ready);CloseHandle(process.hProcess);
    if(result!=WAIT_OBJECT_0)throw std::runtime_error("Updater helper did not confirm readiness. Bloodlines remains running.");
    Status(UpdatePhase::RestartReady,"Verified. Closing Bloodlines to install and restart...",1);
    Log("CAINE_UPDATE_STAGED: "+release.version+"; helper ready, graceful exit requested on menu thread");
}
void Download() {
    Status(UpdatePhase::Downloading,"Starting verified CAINE download...");
    wchar_t local[32768]{};
    if(!GetEnvironmentVariableW(L"LOCALAPPDATA",local,32768))throw std::runtime_error("LOCALAPPDATA is unavailable");
    GUID id{};if(FAILED(CoCreateGuid(&id)))throw std::runtime_error("Cannot create update staging directory");
    wchar_t guid[40]{};StringFromGUID2(id,guid,40);
    const auto stage=std::filesystem::path(local)/L"PROJECT CAINE"/L"Bloodlines"/L"updates"/guid;
    VerifyLocalPath(stage);std::filesystem::create_directories(stage);
    const auto zip=stage/UpdateWide(UpdateAsset);
    Fetch(release.url,false,zip,release.size);
    Status(UpdatePhase::Verifying,"Verifying the download's SHA-256 digest...",1);
    if(Sha256(zip)!=release.sha256)throw std::runtime_error("Downloaded update failed SHA-256 verification. Nothing installed.");
    Status(UpdatePhase::Staging,"Preparing the installation helper...",1);StageHelper(zip);
}
}
UpdateSnapshot ReadUpdate(){std::lock_guard<std::mutex> lock(mutex);return snapshot;}
void RequestUpdateInstall(){std::lock_guard<std::mutex> lock(mutex);if(wake && snapshot.available && !snapshot.Busy()){snapshot.phase=UpdatePhase::Downloading;snapshot.message="Starting verified CAINE download...";snapshot.progress=0;downloadRequested=true;SetEvent(wake);}}
bool ClaimUpdateRestart(){std::lock_guard<std::mutex> lock(mutex);if(snapshot.phase!=UpdatePhase::RestartReady || restartClaimed)return false;restartClaimed=true;return true;}
#ifdef CAINE_UPDATE_NETWORK_TEST
ReleaseUpdate ProbeUpdateNetwork(const std::filesystem::path& download) {
    const auto feed=Fetch("https://api.github.com/repos/"+std::string(UpdateRepository)+"/releases?per_page=20",true);
    release=SelectReleaseUpdate(feed,"0.3.8-framework-dev",true);
    if(release.version.empty())throw std::runtime_error("Published development update was not found");
    Fetch(release.url,false,download,release.size);
    if(Sha256(download)!=release.sha256)throw std::runtime_error("Public release download SHA-256 mismatch");
    if(!SelectReleaseUpdate(feed,release.version,true).version.empty())throw std::runtime_error("Same-version update was offered");
    return release;
}
#endif
void InitializeUpdates(const std::filesystem::path& game,const std::filesystem::path& config,const std::function<void(const std::string&)>& log) {
    if(!GetPrivateProfileIntW(L"Updates",L"Check",1,config.c_str()))return;
    gameRoot=game;installRoot=config.parent_path();logger=log;
    wchar_t cwd[32768]{};
    if(!GetCurrentDirectoryW(32768,cwd)){Log("CAINE_UPDATE_ERROR: cannot preserve launch working directory");return;}
    originalCommand=GetCommandLineW();originalDirectory=cwd;
    const bool preview=GetPrivateProfileIntW(L"Updates",L"Preview",1,config.c_str())!=0;
    const auto hours=std::clamp(GetPrivateProfileIntW(L"Updates",L"IntervalHours",6,config.c_str()),1u,168u);
    wake=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(!wake){Log("CAINE_UPDATE_ERROR: cannot create update worker");return;}
    // CAINE is pinned for process lifetime. Worker performs no game/D3D calls.
    std::thread([preview,hours] {
        bool check=true;
        for(;;) {
            try {
                if(check) {
                    Status(UpdatePhase::Checking,"Checking GitHub releases...");
                    auto candidate=SelectReleaseUpdate(Fetch("https://api.github.com/repos/"+std::string(UpdateRepository)+"/releases?per_page=20",true),UpdateVersion,preview);
                    {std::lock_guard<std::mutex> lock(mutex);release=std::move(candidate);snapshot.available=!release.version.empty();snapshot.version=release.version;}
                    Status(release.version.empty()?UpdatePhase::Idle:UpdatePhase::Available,release.version.empty()?"CAINE is up to date.":"CAINE "+release.version+" is available.");
                    Log("CAINE_UPDATE_CHECK: "+ReadUpdate().message);
                } else Download();
            }catch(const std::exception& error){Status(UpdatePhase::Error,error.what());Log(std::string("CAINE_UPDATE_ERROR: ")+error.what());}
            if(ReadUpdate().phase==UpdatePhase::RestartReady)return;
            const DWORD result=WaitForSingleObject(wake,hours*60u*60u*1000u);
            {std::lock_guard<std::mutex> lock(mutex);check=!(result==WAIT_OBJECT_0 && downloadRequested);downloadRequested=false;}
        }
    }).detach();
}
}
