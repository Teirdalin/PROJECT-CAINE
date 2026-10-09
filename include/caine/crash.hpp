// Copyright (c) 2026 Teirdalin. SPDX-License-Identifier: LicenseRef-JDL-1
#pragma once
#include <windows.h>
#include <filesystem>
namespace caine {
// Bootstrap only, outside DllMain. The observer and helper have process lifetime.
bool InstallCrashReporting(const std::filesystem::path& helper,
                          const std::filesystem::path& directory,
                          const std::filesystem::path& logFile) noexcept;
void CrashBreadcrumb(const char* message) noexcept;
}
