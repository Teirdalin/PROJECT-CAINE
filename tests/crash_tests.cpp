// Copyright (c) 2026 Teirdalin. SPDX-License-Identifier: LicenseRef-JDL-1
#include <caine/crash.hpp>
#include <dbghelp.h>
#include <chrono>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
namespace {
std::filesystem::path nativeFilterMarker;
void Check(bool value, const char* message) { if (!value) throw std::runtime_error(message); }
__declspec(noinline) void Fault() { volatile LONG* volatile pointer = nullptr; *pointer = 7; }
void HandledFault() { __try { Fault(); } __except (EXCEPTION_EXECUTE_HANDLER) {} }
LONG WINAPI NativeFilter(EXCEPTION_POINTERS*) {
    const auto file = CreateFileW(nativeFilterMarker.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file != INVALID_HANDLE_VALUE) { DWORD written{}; WriteFile(file, "native", 6, &written, nullptr); CloseHandle(file); }
    return EXCEPTION_EXECUTE_HANDLER;
}
void CheckDump(const std::filesystem::path& file) {
    std::ifstream input(file, std::ios::binary); MINIDUMP_HEADER header{};
    input.read(reinterpret_cast<char*>(&header), sizeof(header));
    Check(input && header.Signature == MINIDUMP_SIGNATURE, "invalid minidump header");
    std::vector<MINIDUMP_DIRECTORY> directories(header.NumberOfStreams);
    input.seekg(header.StreamDirectoryRva); input.read(reinterpret_cast<char*>(directories.data()), directories.size() * sizeof(MINIDUMP_DIRECTORY));
    bool exception = false, threads = false, modules = false;
    for (const auto& stream : directories) {
        threads |= stream.StreamType == ThreadListStream; modules |= stream.StreamType == ModuleListStream;
        if (stream.StreamType == ExceptionStream) {
            MINIDUMP_EXCEPTION_STREAM info{}; input.seekg(stream.Location.Rva); input.read(reinterpret_cast<char*>(&info), sizeof(info));
            Check(input && info.ExceptionRecord.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && info.ThreadId != 0, "dump lost the original exception");
            CONTEXT context{}; input.seekg(info.ThreadContext.Rva); input.read(reinterpret_cast<char*>(&context), sizeof(context));
            Check(input && context.Eip == info.ExceptionRecord.ExceptionAddress && context.Esp != 0, "dump contains reporter context instead of fault context");
            exception = true;
        }
    }
    Check(exception && threads && modules, "minidump missing exception, threads or modules");
}
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc == 5 && std::wstring(argv[1]) == L"child") {
            SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
            const std::filesystem::path root = argv[3]; const std::wstring mode = argv[4];
            nativeFilterMarker = root / L"native-filter.marker";
            SetUnhandledExceptionFilter(NativeFilter);
            const bool ready = caine::InstallCrashReporting(argv[2], root, root / L"runtime.log");
            if (mode == L"missing") return ready ? 10 : 0;
            Check(ready, "crash reporter handshake failed");
            caine::CrashBreadcrumb("CAINE_TEST_CHARACTER_CREATION_RESTORE");
            if (mode == L"cpp") { try { throw std::runtime_error("ordinary error"); } catch (...) {} return 0; }
            if (mode == L"handled" || mode == L"text" || mode == L"limit") { HandledFault(); if (mode == L"limit") HandledFault(); return 0; }
            Fault(); return 11;
        }
        Check(argc == 2, "usage: crash_tests CrashReporter.exe");
        wchar_t executable[1024]{}; GetModuleFileNameW(nullptr, executable, 1024);
        const auto lab = std::filesystem::temp_directory_path() / (L"caine-crash-\u00e9-" + std::to_wstring(GetCurrentProcessId()) + L"-" + std::to_wstring(std::chrono::steady_clock::now().time_since_epoch().count()));
        for (const auto mode : {L"fatal", L"handled", L"text", L"limit", L"cpp", L"missing"}) {
            const auto root = lab / mode; std::filesystem::create_directories(root);
            const auto helper = root / (std::wstring(mode) == L"missing" ? L"absent.exe" : L"CrashReporter.exe");
            if (std::wstring(mode) != L"missing") {
                std::filesystem::copy_file(argv[1], helper);
                // A broken app-local DbgHelp must never interfere with the system DLL.
                std::ofstream(root / L"dbghelp.dll") << "obsolete local DbgHelp placeholder";
                if (std::wstring(mode) == L"text") std::ofstream(root / L"CAINE.ini") << "[Crash]\nDump=0\n";
                if (std::wstring(mode) == L"limit") std::ofstream(root / L"CAINE.ini") << "[Crash]\nMaxReports=1\n";
            }
            std::wstring command = L"\"" + std::wstring(executable) + L"\" child \"" + helper.wstring() + L"\" \"" + root.wstring() + L"\" " + mode;
            STARTUPINFOW startup{sizeof(startup)}; PROCESS_INFORMATION child{};
            Check(CreateProcessW(executable, command.data(), nullptr, nullptr, FALSE, CREATE_NO_WINDOW, nullptr, nullptr, &startup, &child) != FALSE, "start crash fixture");
            Check(WaitForSingleObject(child.hProcess, 20000) == WAIT_OBJECT_0, "crash fixture timeout");
            DWORD result{}; GetExitCodeProcess(child.hProcess, &result); CloseHandle(child.hThread); CloseHandle(child.hProcess);
            const bool severe = std::wstring(mode) == L"fatal" || std::wstring(mode) == L"handled" || std::wstring(mode) == L"text" || std::wstring(mode) == L"limit";
            Check(std::wstring(mode) == L"fatal" ? result == EXCEPTION_ACCESS_VIOLATION : result == 0, "observer changed the exception outcome");
            std::vector<std::filesystem::path> texts, dumps;
            for (const auto& file : std::filesystem::directory_iterator(root)) {
                if (file.path().extension() == L".txt") texts.push_back(file.path());
                if (file.path().extension() == L".dmp") dumps.push_back(file.path());
            }
            Check(texts.size() == (severe ? 1u : 0u) && dumps.size() == (std::wstring(mode) == L"text" ? 0u : texts.size()), "crash artifact count");
            if (severe) {
                std::ifstream input(texts.front()); const std::string text((std::istreambuf_iterator<char>(input)), {});
                for (const auto marker : {"FIRST_CHANCE_CANDIDATE", "Exception=0xc0000005", "Memory access=write", "CAINE_TEST_CHARACTER_CREATION_RESTORE", "Loaded modules", "Stack frames", "Reporter DbgHelp:"})
                    Check(text.find(marker) != std::string::npos, "crash report lacks required detail");
                if (std::wstring(mode) != L"text") { Check(text.find("Minidump: written") != std::string::npos, "dump failed"); CheckDump(dumps.front()); }
            }
            Check(std::filesystem::exists(root / L"native-filter.marker") == (std::wstring(mode) == L"fatal"), "native unhandled filter was replaced or bypassed");
        }
        std::cout << "CAINE_CRASH_REPORTING_OK: fatal/handled AVs, original x86 dump context, native handler preserved, text-only mode, report cap, app-local DbgHelp ignored, C++ errors ignored, missing helper graceful; " << lab.string() << '\n';
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
