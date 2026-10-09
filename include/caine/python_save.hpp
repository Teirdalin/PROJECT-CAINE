#pragma once
#include <windows.h>
#include <functional>
#include <string>
#include <caine/native_types.hpp>
namespace caine {
using PythonObject=native::PythonObject;
// Exact-profile CPython 2.1 x86 file wrapper owned by Bloodlines.
using PythonSaveFile=native::PythonSaveFile;
constexpr size_t MaxPythonSaveLine = 64u*1024u*1024u*4u+1024u;
std::string ReadPythonSaveLine(PythonSaveFile* file);
bool InstallLongStringReader(HMODULE game, HMODULE python, const std::function<void(const std::string&)>& log);
bool LongStringReaderReady();
}
