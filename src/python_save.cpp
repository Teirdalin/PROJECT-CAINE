#include <caine/python_save.hpp>
#include <caine/core.hpp>
#include <algorithm>
#include <array>
#include <atomic>
#include <climits>
namespace caine {
namespace {
PythonObject* (__cdecl* makeString)(const char*,int){};
void (__cdecl* setError)(PythonObject*,const char*){};
PythonObject** ioError{};
Hooks* readerHooks{}; // Process lifetime; never unload an executing detour.
void* readerOriginal{};
std::atomic<bool> readerReady{false};
template<class T> T Export(HMODULE module,const char* name) {
    const auto value=GetProcAddress(module,name);
    if (!value) throw std::runtime_error("Required Python long-string API absent");
    return reinterpret_cast<T>(value);
}
PythonObject* __cdecl ReadLine(PythonSaveFile* file,PythonObject*) noexcept {
    try {
        const auto line=ReadPythonSaveLine(file);
        return makeString(line.data(),static_cast<int>(line.size()));
    } catch (const std::exception& error) { setError(*ioError,error.what()); }
    catch (...) { setError(*ioError,"Native Python save line could not be read"); }
    return nullptr;
}
}
std::string ReadPythonSaveLine(PythonSaveFile* file) {
    if (!file || !file->reader || file->end < 0) throw std::runtime_error("Invalid native Python save reader");
    auto table = *reinterpret_cast<void***>(file->reader);
    const auto tell = reinterpret_cast<int(__thiscall*)(void*)>(table[0]);
    const auto seek = reinterpret_cast<void(__thiscall*)(void*,int)>(table[1]);
    const auto read = reinterpret_cast<int(__thiscall*)(void*,void*,int,int)>(table[0x4c/sizeof(void*)]);
    std::string line;
    bool ending = false;
    for (;;) {
        const int position = tell(file->reader);
        if (position < 0 || position > file->end) throw std::runtime_error("Python save cursor out of block");
        if (position == file->end) break;
        std::array<char,4096> buffer{};
        const int requested = std::min(file->end-position,static_cast<int>(buffer.size()));
        const int received = read(file->reader,buffer.data(),requested,requested);
        if (received <= 0 || received > requested) throw std::runtime_error("Truncated native Python save block");
        for (int i=0; i<received; ++i) {
            const char byte = buffer[static_cast<size_t>(i)];
            const bool newline = byte == '\n' || byte == '\r';
            if (ending) {
                if (newline) continue;
                seek(file->reader,position+i);
                return line;
            }
            // Protocol 0 may escape each legacy snapshot byte into four chars.
            if (line.size() >= MaxPythonSaveLine) throw std::runtime_error("Python save line exceeds supported bound");
            line.push_back(byte);
            ending = newline;
        }
    }
    return line;
}
bool LongStringReaderReady() { return readerReady.load(); }
bool InstallLongStringReader(HMODULE gameHandle,HMODULE python,const std::function<void(const std::string&)>& log) {
    if (readerReady.load()) return true;
    try {
        const auto game=Module::Inspect(gameHandle);
        constexpr char hash[]="996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8";
        if (game.sha256!=hash || Module::Inspect(python).sha256!="9e65e6d091bfed21dd98ed0b99397c217a793e0b80be4db2fdb9bbb12b9e116d")
            throw std::runtime_error("Unsupported game/Python long-string profile");
        makeString=Export<decltype(makeString)>(python,"PyString_FromStringAndSize");
        setError=Export<decltype(setError)>(python,"PyErr_SetString");
        ioError=Export<PythonObject**>(python,"PyExc_IOError");
        readerHooks=new Hooks();
        std::string error;
        if (!readerHooks->Install(game,{"python.readline",hash,0x19bc80,
            {0x83,0xec,0x0c,0x53,0x56,0x57,0x33,0xff,0x68,0x80,0,0,0}},reinterpret_cast<void*>(ReadLine),&readerOriginal,error))
            throw std::runtime_error(error);
        readerReady.store(true);
        log("CAINE_LONG_STRINGS_READY: guarded Python save reader now grows dynamically; independent of enabled mods");
        return true;
    } catch (const std::exception& error) { log(std::string("CAINE_LONG_STRINGS_UNAVAILABLE: ")+error.what()); return false; }
}
}
