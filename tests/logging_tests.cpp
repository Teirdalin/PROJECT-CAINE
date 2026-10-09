#include <caine/logging.hpp>
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>
#include <stdexcept>

namespace {
std::atomic<unsigned> breadcrumbs{};
void Breadcrumb(const char*) noexcept { ++breadcrumbs; }
void Check(bool value,const char* reason) { if (!value) throw std::runtime_error(reason); }
std::string Read(const std::filesystem::path& path) { std::ifstream input(path,std::ios::binary);return {std::istreambuf_iterator<char>(input),{}}; }
}
int main() {
    try {
        const auto root=std::filesystem::temp_directory_path()/(L"caine-log-test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
        std::filesystem::create_directories(root);
        const auto current=root/L"CAINE.log",session=root/(L"CAINE-"+std::to_wstring(GetCurrentProcessId())+L".log");
        { std::ofstream(current,std::ios::binary)<<"OLD_CURRENT_SENTINEL\n";std::ofstream(session,std::ios::binary)<<"OLD_PID_SENTINEL\n";std::ofstream(root/L"unrelated.log",std::ios::binary)<<"KEEP\n"; }
        Check(caine::OpenDebugLog(root,true,Breadcrumb),"open debug log");
        caine::WriteLog("CAINE_START_TEST: UTF-8 \xe2\x82\xac\nsecond line\r\n");
        std::vector<std::thread> workers;
        for (int thread=0;thread<4;++thread) workers.emplace_back([thread] { for (int i=0;i<50;++i) caine::TraceLog("WORKER: "+std::to_string(thread)+" item="+std::to_string(i)); });
        for (auto& worker:workers) worker.join();
        const auto content=Read(current);
        Check(content==Read(session),"current and PID logs diverged");
        Check(content.find("OLD_")==std::string::npos && Read(root/L"unrelated.log")=="KEEP\n","startup reset affected wrong files");
        Check(content.find("\\nsecond line\\r\\n")!=std::string::npos && content.find("\xe2\x82\xac")!=std::string::npos,"escaped one-line UTF-8 event");
        std::istringstream lines(content);std::string line;unsigned count{};
        while (std::getline(lines,line)) {
            ++count;Check(line.find("seq="+std::to_string(count)+" ")!=std::string::npos,"interleaved or missing log sequence");
            Check(line.find("Z seq=")!=std::string::npos && line.find("uptime_ms=")!=std::string::npos && line.find("pid=")!=std::string::npos && line.find("thread=")!=std::string::npos,"event context missing");
        }
        Check(count==201 && breadcrumbs==201,"concurrent events or breadcrumbs dropped");
        caine::WriteLog(std::string(65535,'x')+"\xe2\x82\xac suffix");
        const auto bounded=Read(current);Check(bounded.find("[message bounded at 64 KiB]")!=std::string::npos && bounded.find("\xe2 [message")==std::string::npos,"bounded message split UTF-8");
        Check(caine::OpenDebugLog(root,false),"restart log");
        caine::TraceLog("HIDDEN_TRACE");caine::WriteLog("NEW_START_ONLY");
        const auto fresh=Read(current);Check(fresh.find("NEW_START_ONLY")!=std::string::npos && fresh.find("WORKER:")==std::string::npos && fresh.find("HIDDEN_TRACE")==std::string::npos && fresh==Read(session),"restart/reset or verbosity failed");
        caine::CloseDebugLog();
        const auto lock=CreateFileW(current.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        Check(lock!=INVALID_HANDLE_VALUE,"current log lock");
        Check(caine::OpenDebugLog(root,true),"locked current log disabled PID logging");caine::WriteLog("PID_FALLBACK");
        Check(Read(session).find("PID_FALLBACK")!=std::string::npos,"PID fallback missing");
        caine::CloseDebugLog();CloseHandle(lock);
        Check(!caine::OpenDebugLog(root/L"unrelated.log"/L"invalid"),"invalid log root accepted");
        caine::WriteLog("UNAVAILABLE_DISK_MUST_NOT_THROW");caine::TraceLog("UNAVAILABLE_TRACE_MUST_NOT_THROW");
        Check(Read(root/L"unrelated.log")=="KEEP\n","failure altered unrelated file");
        std::cout<<"CAINE_LOGGING_OK: fresh current/PID logs, 200 concurrent trace events, sequence/thread/UTC/uptime, UTF-8 boundaries, breadcrumbs, verbosity and locked/failed-file resilience\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n';return 1; }
}
