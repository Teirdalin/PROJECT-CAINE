#pragma once
#include <filesystem>
#include <string>

namespace caine {
using LogBreadcrumb=void(*)(const char*) noexcept;
// Normal startup only, after loader-lock release. Both files start empty.
// CAINE.log is the current launch; CAINE-<pid>.log remains tied to crash reports.
bool OpenDebugLog(const std::filesystem::path& directory,bool verbose=true,LogBreadcrumb breadcrumb=nullptr) noexcept;
void WriteLog(const std::string& message) noexcept;
void TraceLog(const std::string& message) noexcept;
// Controlled tests only. Never called from DllMain or while hooks are executing.
void CloseDebugLog() noexcept;
}
