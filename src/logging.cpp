#include <caine/logging.hpp>
#include <windows.h>
#include <atomic>
#include <mutex>
#include <stdexcept>

namespace {
struct LogState {
    std::mutex mutex;
    HANDLE current{INVALID_HANDLE_VALUE},session{INVALID_HANDLE_VALUE};
    ULONGLONG started{},sequence{};
    caine::LogBreadcrumb breadcrumb{};
    bool warned{};
};
// Process lifetime: no cleanup or lock acquisition under DLL detach/loader lock.
LogState& State() { static auto* state=new LogState();return *state; }
std::atomic<bool> ready{},verbose{};
void Close(LogState& state) {
    for (const auto handle:{state.current,state.session}) if (handle!=INVALID_HANDLE_VALUE) CloseHandle(handle);
    state.current=state.session=INVALID_HANDLE_VALUE;
}
bool Write(HANDLE handle,const std::string& line) {
    if (handle==INVALID_HANDLE_VALUE) return true;
    DWORD written{};
    return WriteFile(handle,line.data(),static_cast<DWORD>(line.size()),&written,nullptr) && written==line.size();
}
void Record(const std::string& message,bool detail) noexcept {
    if (!ready.load() || (detail && !verbose.load())) return;
    try {
        auto& state=State();std::lock_guard<std::mutex> lock(state.mutex);
        if (state.session==INVALID_HANDLE_VALUE) return;
        // One event per line. Bound third-party messages without splitting UTF-8.
        constexpr size_t limit=65536;
        auto count=message.size();
        if (count>limit) { count=limit;while (count && (static_cast<unsigned char>(message[count])&0xc0)==0x80) --count; }
        std::string safe;safe.reserve(count+64);
        for (size_t i=0;i<count;++i) {
            const auto c=static_cast<unsigned char>(message[i]);
            if (c=='\n') safe+="\\n";else if (c=='\r') safe+="\\r";else if (c=='\0') safe+="\\0";
            else if (c<32 && c!='\t') safe+='?';else safe+=static_cast<char>(c);
        }
        if (count<message.size()) safe+=" [message bounded at 64 KiB]";
        SYSTEMTIME now{};GetSystemTime(&now);char header[192]{};
        sprintf_s(header,"%04u-%02u-%02uT%02u:%02u:%02u.%03uZ seq=%llu uptime_ms=%llu pid=%lu thread=%lu %s ",
            now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,now.wMilliseconds,
            ++state.sequence,GetTickCount64()-state.started,GetCurrentProcessId(),GetCurrentThreadId(),detail?"TRACE":"EVENT");
        const std::string line=std::string(header)+safe+"\n";
        if (state.breadcrumb) state.breadcrumb(safe.c_str());
        const bool session=Write(state.session,line),current=Write(state.current,line);
        if ((!session || !current) && !state.warned) {
            state.warned=true;OutputDebugStringA("CAINE_LOG_WRITE_FAILED: diagnostic disk write failed; runtime continues\n");
        }
        OutputDebugStringA(line.c_str());
    } catch (...) { OutputDebugStringA("CAINE_LOG_WRITE_FAILED: diagnostic formatting failed; runtime continues\n"); }
}
}
namespace caine {
bool OpenDebugLog(const std::filesystem::path& directory,bool detail,LogBreadcrumb breadcrumb) noexcept {
    try {
        auto& state=State();bool currentUnavailable{};
        {
        std::lock_guard<std::mutex> lock(state.mutex);
        ready.store(false);Close(state);std::filesystem::create_directories(directory);
        const auto session=directory/(L"CAINE-"+std::to_wstring(GetCurrentProcessId())+L".log");
        state.session=CreateFileW(session.c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        if (state.session==INVALID_HANDLE_VALUE) throw std::runtime_error("Session log unavailable");
        // An already running instance keeps its current-log write ownership.
        // A second instance can still diagnose itself through its own PID log.
        state.current=CreateFileW((directory/L"CAINE.log").c_str(),GENERIC_WRITE,FILE_SHARE_READ|FILE_SHARE_DELETE,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
        currentUnavailable=state.current==INVALID_HANDLE_VALUE;
        state.started=GetTickCount64();state.sequence=0;state.warned=false;state.breadcrumb=breadcrumb;
        verbose.store(detail);ready.store(true);
        }
        if (currentUnavailable) WriteLog("CAINE_LOG_CURRENT_UNAVAILABLE: current file locked or unwritable; fresh PID log remains active");
        return true;
    } catch (...) { OutputDebugStringA("CAINE_LOG_OPEN_FAILED: file logging unavailable; runtime continues\n");return false; }
}
void WriteLog(const std::string& message) noexcept { Record(message,false); }
void TraceLog(const std::string& message) noexcept { Record(message,true); }
void SetVerboseLogging(bool enabled) noexcept { verbose.store(enabled); }
void CloseDebugLog() noexcept {
    try { auto& state=State();std::lock_guard<std::mutex> lock(state.mutex);ready.store(false);Close(state); }
    catch (...) {}
}
}
