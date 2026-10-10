// Copyright (c) 2026 Teirdalin. SPDX-License-Identifier: LicenseRef-JDL-1
#include "crash_protocol.hpp"
#include <dbghelp.h>
#include <psapi.h>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>
namespace {
template<class T> T Export(HMODULE library, const char* name) { return reinterpret_cast<T>(GetProcAddress(library, name)); }
std::string Utf8(const wchar_t* text) {
    const int count = WideCharToMultiByte(CP_UTF8, 0, text, -1, nullptr, 0, nullptr, nullptr);
    if (!count) return {};
    std::string result(static_cast<size_t>(count), 0);
    WideCharToMultiByte(CP_UTF8, 0, text, -1, result.data(), count, nullptr, nullptr);
    result.resize(static_cast<size_t>(count - 1)); return result;
}
struct Module { DWORD base, size; std::string path; };
std::vector<Module> Modules(HANDLE process) {
    HMODULE list[1024]{}; DWORD bytes{}; std::vector<Module> modules;
    if (!EnumProcessModules(process, list, sizeof(list), &bytes)) return modules;
    for (DWORD i = 0; i < std::min<DWORD>(bytes / sizeof(HMODULE), 1024); ++i) {
        MODULEINFO info{}; wchar_t path[1024]{};
        if (GetModuleInformation(process, list[i], &info, sizeof(info)) && GetModuleFileNameExW(process, list[i], path, 1024))
            modules.push_back({reinterpret_cast<DWORD>(info.lpBaseOfDll), info.SizeOfImage, Utf8(path)});
    }
    return modules;
}
std::string Address(DWORD64 address, const std::vector<Module>& modules) {
    std::ostringstream out; out << "0x" << std::hex << address;
    for (const auto& module : modules) if (address >= module.base && address - module.base < module.size) {
        const auto start = module.path.find_last_of("\\/");
        out << " (" << module.path.substr(start == std::string::npos ? 0 : start + 1) << "+0x" << address - module.base << ")"; break;
    }
    return out.str();
}
void Report(HANDLE process, caine::crash::Shared& data, HMODULE dbghelp, const std::filesystem::path& stem) {
    std::ofstream out(stem.wstring() + L".txt", std::ios::trunc);
    out << "PROJECT CAINE 0.3.18 x86 exception report\n"
        << "Stage: FIRST_CHANCE_CANDIDATE (may be handled by the game; not proof of a fatal crash)\n"
        << "PID=" << data.pid << " faulting_thread=" << data.thread << " sequence=" << data.sequence
        << " uptime_ms=" << data.captured - data.started << "\nRuntime log: " << Utf8(data.logFile) << '\n';
    wchar_t libraryPath[1024]{}; GetModuleFileNameW(dbghelp, libraryPath, 1024);
    out << "Reporter DbgHelp: " << Utf8(libraryPath) << '\n';
    const auto modules = Modules(process);
    const auto& exception = data.exception; auto context = data.context;
    out << "Exception=0x" << std::hex << exception.ExceptionCode << " flags=0x" << exception.ExceptionFlags
        << " address=" << Address(reinterpret_cast<DWORD>(exception.ExceptionAddress), modules) << '\n';
    if ((exception.ExceptionCode == EXCEPTION_ACCESS_VIOLATION || exception.ExceptionCode == EXCEPTION_IN_PAGE_ERROR) && exception.NumberParameters >= 2)
        out << "Memory access=" << (exception.ExceptionInformation[0] == 0 ? "read" : exception.ExceptionInformation[0] == 1 ? "write" : "execute")
            << " target=" << Address(exception.ExceptionInformation[1], modules) << '\n';
    out << "EIP=" << context.Eip << " ESP=" << context.Esp << " EBP=" << context.Ebp << " EFLAGS=" << context.EFlags
        << "\nEAX=" << context.Eax << " EBX=" << context.Ebx << " ECX=" << context.Ecx << " EDX=" << context.Edx
        << " ESI=" << context.Esi << " EDI=" << context.Edi << '\n';
    MEMORYSTATUSEX memory{sizeof(memory)}; GlobalMemoryStatusEx(&memory);
    out << std::dec << "System memory load=" << memory.dwMemoryLoad << "% available_physical=" << memory.ullAvailPhys << '\n';
    out << "\nRecent CAINE activity (truncated to 511 bytes per entry):\n";
    // Best effort if a fault interrupted a breadcrumb write. Never wait on the game.
    const LONG count = data.breadcrumbCount;
    for (LONG n = std::max<LONG>(0, count - static_cast<LONG>(caine::crash::BreadcrumbCount)); n < count; ++n) {
        const auto& entry = data.breadcrumbs[static_cast<unsigned long>(n) % caine::crash::BreadcrumbCount];
        out << '+' << entry.tick - data.started << "ms thread=" << entry.thread << ' ';
        out.write(entry.message, static_cast<std::streamsize>(strnlen_s(entry.message, sizeof(entry.message)))); out << '\n';
    }
    out << "\nStack frames (best effort; missing symbols/optimized frames can limit unwinding):\n";
    const auto initialize = Export<decltype(&SymInitialize)>(dbghelp, "SymInitialize");
    const auto cleanup = Export<decltype(&SymCleanup)>(dbghelp, "SymCleanup");
    const auto walk = Export<decltype(&StackWalk64)>(dbghelp, "StackWalk64");
    const auto table = Export<decltype(&SymFunctionTableAccess64)>(dbghelp, "SymFunctionTableAccess64");
    const auto base = Export<decltype(&SymGetModuleBase64)>(dbghelp, "SymGetModuleBase64");
    const auto options = Export<decltype(&SymSetOptions)>(dbghelp, "SymSetOptions");
    if (options) options(SYMOPT_DEFERRED_LOADS | SYMOPT_UNDNAME | SYMOPT_NO_PROMPTS | SYMOPT_FAIL_CRITICAL_ERRORS);
    const std::string search = Utf8(data.gameRoot) + ";" + Utf8(data.gameRoot) + "\\Bin\\loader;" + Utf8(data.gameRoot) + "\\Bin";
    if (initialize && cleanup && walk && table && base && initialize(process, search.c_str(), TRUE)) {
        STACKFRAME64 frame{}; frame.AddrPC = {context.Eip, 0, AddrModeFlat};
        frame.AddrStack = {context.Esp, 0, AddrModeFlat}; frame.AddrFrame = {context.Ebp, 0, AddrModeFlat};
        HANDLE thread = OpenThread(THREAD_QUERY_INFORMATION | THREAD_GET_CONTEXT, FALSE, data.thread);
        out << "0: " << Address(context.Eip, modules) << '\n';
        DWORD64 previous = context.Eip;
        for (int n = 1; thread && n < 64 && walk(IMAGE_FILE_MACHINE_I386, process, thread, &frame, &context, nullptr, table, base, nullptr); ++n) {
            if (!frame.AddrPC.Offset) break;
            if (frame.AddrPC.Offset == previous) { if (n == 1) continue; break; }
            previous = frame.AddrPC.Offset; out << n << ": " << Address(frame.AddrPC.Offset, modules) << '\n';
        }
        if (thread) CloseHandle(thread); cleanup(process);
    } else out << Address(context.Eip, modules) << "\nStack unwinding unavailable\n";
    out << "\nLoaded modules (actual runtime bases, not preferred PE bases):\n";
    for (const auto& module : modules) out << std::hex << "base=0x" << module.base << " size=0x" << module.size << ' ' << module.path << '\n';
    out << "\nFaulting stack bytes (bounded raw data, not an inferred call stack):\n";
    BYTE stack[512]{}; SIZE_T received{};
    if (ReadProcessMemory(process, reinterpret_cast<void*>(data.context.Esp), stack, sizeof(stack), &received)) {
        for (size_t i = 0; i < received; ++i) { if (i % 16 == 0) out << "\n" << std::hex << data.context.Esp + i << ": "; out << std::setw(2) << std::setfill('0') << static_cast<unsigned>(stack[i]) << ' '; }
        out << '\n';
    }
    const auto dump = Export<decltype(&MiniDumpWriteDump)>(dbghelp, "MiniDumpWriteDump");
    if (data.dumpEnabled && dump) {
        const auto path = stem.wstring() + L".dmp";
        const auto file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        EXCEPTION_POINTERS pointers{&data.exception, &data.context};
        MINIDUMP_EXCEPTION_INFORMATION info{data.thread, &pointers, FALSE};
        const auto type = static_cast<MINIDUMP_TYPE>(MiniDumpWithIndirectlyReferencedMemory | MiniDumpScanMemory | MiniDumpWithThreadInfo | MiniDumpWithUnloadedModules);
        const bool success = file != INVALID_HANDLE_VALUE && dump(process, data.pid, file, type, &info, nullptr, nullptr);
        const DWORD error = success ? 0 : GetLastError();
        if (file != INVALID_HANDLE_VALUE) CloseHandle(file);
        out << "\nMinidump: " << (success ? "written" : "FAILED") << " error=0x" << std::hex << error << '\n';
    } else out << "\nMinidump disabled or unavailable\n";
    out.flush();
    data.error = out ? 0 : ERROR_WRITE_FAULT;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 5) return 1;
    HANDLE handles[4]{};
    for (int i = 0; i < 4; ++i) { wchar_t* end{}; const auto value = wcstoul(argv[i + 1], &end, 10); if (!value || *end) return 2; handles[i] = reinterpret_cast<HANDLE>(value); }
    auto data = static_cast<caine::crash::Shared*>(MapViewOfFile(handles[0], FILE_MAP_ALL_ACCESS, 0, 0, sizeof(caine::crash::Shared)));
    if (!data || data->magic != caine::crash::Magic || GetProcessId(handles[3]) != data->pid) return 3;
    wchar_t system[1024]{}; GetSystemDirectoryW(system, 1024);
    // Never load Bloodlines' obsolete app-local dbghelp.dll.
    const auto library = LoadLibraryExW((std::filesystem::path(system) / L"dbghelp.dll").c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!library) { InterlockedExchange(&data->ready, -1); SetEvent(handles[2]); return 4; }
    InterlockedExchange(&data->ready, 1); SetEvent(handles[2]);
    std::vector<std::filesystem::path> reports;
    HANDLE waits[]{handles[1], handles[3]};
    for (;;) {
        const auto result = WaitForMultipleObjects(2, waits, FALSE, INFINITE);
        if (result == WAIT_OBJECT_0 + 1) break;
        if (result != WAIT_OBJECT_0 || data->shutdown) break;
        SYSTEMTIME time{}; GetSystemTime(&time); wchar_t filename[128]{};
        swprintf_s(filename, L"CAINE-%04u%02u%02u-%02u%02u%02u-%lu-%ld", time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, data->pid, data->sequence);
        const auto stem = std::filesystem::path(data->directory) / filename;
        try { Report(handles[3], *data, library, stem); reports.push_back(stem.wstring() + L".txt"); }
        catch (...) { data->error = ERROR_WRITE_FAULT; }
        SetEvent(handles[2]);
    }
    DWORD exitCode{};
    if (GetExitCodeProcess(handles[3], &exitCode) && exitCode != STILL_ACTIVE) for (const auto& path : reports) {
        std::ofstream out(path, std::ios::app); out << "\nObserved process exit code=0x" << std::hex << exitCode
            << " (exit alone does not prove this first-chance exception caused it)\n";
    }
    FreeLibrary(library); UnmapViewOfFile(data);
    for (auto handle : handles) CloseHandle(handle);
    return 0;
}
