#pragma once
#include <caine/mod_api.h>
#include <caine/core.hpp>
#include <functional>
namespace caine {
bool InitializePythonBridge(const std::function<void(const std::string&)>& log);
int ReadScriptScalar(uint32_t kind,const char* name,CaineScalarV1& output);
}
