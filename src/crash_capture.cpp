// Copyright (c) 2026 Teirdalin. SPDX-License-Identifier: LicenseRef-JDL-1
#include <caine/crash.hpp>
#include "crash_protocol.hpp"
#include <algorithm>
#include <string>
namespace {
caine::crash::Shared* shared{};
HANDLE request{}, complete{}, helperProcess{};
bool Severe(DWORD code) noexcept {
    return code == EXCEPTION_ACCESS_VIOLATION || code == EXCEPTION_IN_PAGE_ERROR ||
           code == EXCEPTION_ILLEGAL_INSTRUCTION || code == EXCEPTION_INT_DIVIDE_BY_ZERO ||
           code == EXCEPTION_STACK_OVERFLOW || code == 0xc0000374u || code == 0xc0000409u;
}
LONG CALLBACK Observe(EXCEPTION_POINTERS* info) noexcept {
    // Never acquire game/CRT locks, allocate, walk stacks, or call DbgHelp here.
    // Bloodlines has its own exception handler; always let it continue unchanged.
    auto data = shared;
    if (!data || !info || !Severe(info->ExceptionRecord->ExceptionCode) ||
        data->sequence >= static_cast<LONG>(data->reportLimit) ||
        WaitForSingleObject(helperProcess, 0) != WAIT_TIMEOUT ||
        InterlockedCompareExchange(&data->busy, 1, 0) != 0) return EXCEPTION_CONTINUE_SEARCH;
    ResetEvent(complete);
    data->thread = GetCurrentThreadId();
    data->captured = GetTickCount64();
    data->exception = *info->ExceptionRecord;
    data->exception.ExceptionRecord = nullptr;
    data->context = *info->ContextRecord;
    InterlockedIncrement(&data->sequence);
    SetEvent(request);
    // Bounded wait: the reporter lives outside the game's possibly corrupt heap.
    // On timeout, keep busy set so a later exception cannot overwrite its input.
    HANDLE waits[]{complete, helperProcess};
    if (WaitForMultipleObjects(2, waits, FALSE, 8000) == WAIT_OBJECT_0)
        InterlockedExchange(&data->busy, 0);
    return EXCEPTION_CONTINUE_SEARCH;
}
}
namespace caine {
bool InstallCrashReporting(const std::filesystem::path& helper,
                          const std::filesystem::path& directory,
                          const std::filesystem::path& logFile) noexcept {
    if (shared) return true;
    HANDLE mapping{}, process{};
    crash::Shared* data{};
    try {
        if (!std::filesystem::is_regular_file(helper) || directory.wstring().size() >= 1024 || logFile.wstring().size() >= 1024) return false;
        std::filesystem::create_directories(directory);
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        mapping = CreateFileMappingW(INVALID_HANDLE_VALUE, &security, PAGE_READWRITE, 0, sizeof(crash::Shared), nullptr);
        request = CreateEventW(&security, FALSE, FALSE, nullptr);
        complete = CreateEventW(&security, FALSE, FALSE, nullptr);
        if (!mapping || !request || !complete || !DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), GetCurrentProcess(), &process,
            PROCESS_QUERY_INFORMATION | PROCESS_VM_READ | PROCESS_DUP_HANDLE | SYNCHRONIZE, TRUE, 0)) throw 1;
        data = static_cast<crash::Shared*>(MapViewOfFile(mapping, FILE_MAP_ALL_ACCESS, 0, 0, sizeof(crash::Shared)));
        if (!data) throw 1;
        data->magic = crash::Magic; data->pid = GetCurrentProcessId(); data->started = GetTickCount64();
        data->reportLimit = 16; data->dumpEnabled = 1;
        wcscpy_s(data->directory, directory.c_str()); wcscpy_s(data->logFile, logFile.c_str());
        wchar_t executable[1024]{};
        const auto length = GetModuleFileNameW(nullptr, executable, 1024);
        if (!length || length >= 1024) throw 1;
        wcscpy_s(data->gameRoot, std::filesystem::path(executable).parent_path().c_str());
        const auto config = helper.parent_path() / L"CAINE.ini";
        data->reportLimit = std::clamp<DWORD>(GetPrivateProfileIntW(L"Crash", L"MaxReports", 16, config.c_str()), 1, 64);
        data->dumpEnabled = GetPrivateProfileIntW(L"Crash", L"Dump", 1, config.c_str()) != 0;
        // Restrict inherited handles; never expose unrelated game/service handles.
        SIZE_T bytes{}; InitializeProcThreadAttributeList(nullptr, 1, 0, &bytes);
        auto attributes = static_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(HeapAlloc(GetProcessHeap(), 0, bytes));
        if (!attributes) throw 1;
        HANDLE handles[]{mapping, request, complete, process};
        STARTUPINFOEXW startup{}; startup.StartupInfo.cb = sizeof(startup); startup.lpAttributeList = attributes;
        PROCESS_INFORMATION child{};
        bool initialized = InitializeProcThreadAttributeList(attributes, 1, 0, &bytes) != FALSE;
        bool launched = false;
        if (initialized && UpdateProcThreadAttribute(attributes, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, handles, sizeof(handles), nullptr, nullptr)) {
            std::wstring command = L"\"" + helper.wstring() + L"\"";
            for (auto handle : handles) command += L" " + std::to_wstring(reinterpret_cast<uintptr_t>(handle));
            launched = CreateProcessW(helper.c_str(), command.data(), nullptr, nullptr, TRUE,
                CREATE_NO_WINDOW | EXTENDED_STARTUPINFO_PRESENT, nullptr, helper.parent_path().c_str(), &startup.StartupInfo, &child) != FALSE;
        }
        if (initialized) DeleteProcThreadAttributeList(attributes);
        HeapFree(GetProcessHeap(), 0, attributes);
        if (!launched) throw 1;
        CloseHandle(child.hThread); helperProcess = child.hProcess;
        HANDLE waits[]{complete, helperProcess};
        if (WaitForMultipleObjects(2, waits, FALSE, 5000) != WAIT_OBJECT_0 || data->ready != 1) throw 1;
        shared = data;
        if (!AddVectoredExceptionHandler(0, Observe)) { shared = nullptr; throw 1; }
        CloseHandle(mapping); CloseHandle(process);
        return true;
    } catch (...) {
        if (data) { InterlockedExchange(&data->shutdown, 1); if (request) SetEvent(request); }
        if (data) UnmapViewOfFile(data);
        for (auto handle : {mapping, process, request, complete, helperProcess}) if (handle) CloseHandle(handle);
        request = complete = helperProcess = nullptr;
        return false;
    }
}
void CrashBreadcrumb(const char* message) noexcept {
    auto data = shared;
    if (!data || !message || InterlockedCompareExchange(&data->breadcrumbLock, 1, 0) != 0) return;
    const auto index = static_cast<unsigned long>(data->breadcrumbCount) % crash::BreadcrumbCount;
    auto& entry = data->breadcrumbs[index]; entry.tick = GetTickCount64(); entry.thread = GetCurrentThreadId();
    size_t i{};
    for (; i + 1 < sizeof(entry.message) && message[i]; ++i) entry.message[i] = message[i] == '\n' || message[i] == '\r' ? ' ' : message[i];
    entry.message[i] = 0;
    InterlockedIncrement(&data->breadcrumbCount);
    InterlockedExchange(&data->breadcrumbLock, 0);
}
}
