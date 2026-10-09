#pragma once
#include <caine/core.hpp>
#include <functional>

namespace caine {
bool IsStartupSplash(const char* filename);
bool InstallStartupVideoSkip(HMODULE engine, const std::function<void(const std::string&)>& log);
}
