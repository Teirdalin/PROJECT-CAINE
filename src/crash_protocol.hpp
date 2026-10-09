// Copyright (c) 2026 Teirdalin. SPDX-License-Identifier: LicenseRef-JDL-1
#pragma once
#include <windows.h>
#include <cstdint>
namespace caine::crash {
constexpr DWORD Magic = 0x4341494e;
constexpr size_t BreadcrumbCount = 64;
struct Breadcrumb { ULONGLONG tick; DWORD thread; char message[512]; };
// Same x86 layout in the game and the external helper. No heap pointers or STL.
struct Shared {
    DWORD magic, pid;
    volatile LONG ready, shutdown, busy, sequence, breadcrumbLock;
    DWORD thread, reportLimit, dumpEnabled, error;
    ULONGLONG started, captured;
    EXCEPTION_RECORD exception;
    CONTEXT context;
    wchar_t directory[1024], logFile[1024], gameRoot[1024];
    volatile LONG breadcrumbCount;
    Breadcrumb breadcrumbs[BreadcrumbCount];
};
}
