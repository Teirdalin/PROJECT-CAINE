#include <caine/python_bridge.hpp>
#include <caine/native_types.hpp>
#include <atomic>
#include <cmath>
#include <cstring>
#include <stdexcept>
namespace {
using Object=caine::native::PythonObject;
std::atomic<bool> ready{};
std::atomic<bool> attempted{};
uint8_t* game{};
HWND window{};
struct Api {
    Object*(__cdecl* module)(const char*){};
    Object*(__cdecl* method)(Object*,const char*,const char*,...){};
    Object*(__cdecl* get)(Object*,const char*){};
    Object*(__cdecl* attribute)(Object*,const char*){};
    long(__cdecl* integer)(Object*){};
    double(__cdecl* number)(Object*){};
    int(__cdecl* bytes)(Object*,char**,int*){};
    int(__cdecl* initialized)(){};
    void(__cdecl* fetch)(Object**,Object**,Object**){};
    void(__cdecl* restore)(Object*,Object*,Object*){};
    void(__cdecl* clear)(){};
    void *dict{},*intType{},*floatType{},*stringType{},*none{};
} py;
template<class T> T Export(HMODULE module,const char* name) {
    const auto value=GetProcAddress(module,name);if (!value) throw std::runtime_error("Missing Python 2.1 scalar adapter export");return reinterpret_cast<T>(value);
}
BOOL CALLBACK Window(HWND value,LPARAM) {
    DWORD process{};GetWindowThreadProcessId(value,&process);
    if (process==GetCurrentProcessId() && IsWindowVisible(value) && !GetWindow(value,GW_OWNER)) { window=value;return FALSE; }return TRUE;
}
struct Errors {
    Object *type{},*value{},*trace{};
    Errors() { py.fetch(&type,&value,&trace); }
    ~Errors() { py.clear();py.restore(type,value,trace); }
};
int Scalar(Object* value,CaineScalarV1& output) {
    if (!value) return 0;
    output={};output.size=sizeof(output);
    if (value==py.none) { output.kind=CAINE_SCALAR_NULL;return 1; }
    if (value->type==py.intType) { output.kind=CAINE_SCALAR_INT32;output.integer=py.integer(value);return 1; }
    if (value->type==py.floatType) { output.kind=CAINE_SCALAR_FLOAT64;output.number=py.number(value);return std::isfinite(output.number)?1:-2; }
    if (value->type!=py.stringType) return -2;
    char* bytes{};int count{};
    if (py.bytes(value,&bytes,&count) || count<0) return -2;
    if (count>16383) return -1;
    const int length=MultiByteToWideChar(1252,0,bytes,count,nullptr,0);
    std::wstring wide(length,L'\0');MultiByteToWideChar(1252,0,bytes,count,wide.data(),length);
    const int size=WideCharToMultiByte(CP_UTF8,0,wide.data(),length,nullptr,0,nullptr,nullptr);
    if (size>=static_cast<int>(sizeof(output.text))) return -1;
    WideCharToMultiByte(CP_UTF8,0,wide.data(),length,output.text,size,nullptr,nullptr);
    output.textBytes=static_cast<uint32_t>(size);output.kind=CAINE_SCALAR_UTF8;return 1;
}
}
namespace caine {
bool InitializePythonBridge(const std::function<void(const std::string&)>& log) {
    if (ready.load()) return true;
    if (attempted.exchange(true)) return false;
    try {
        const auto binary=Module::Inspect(GetModuleHandleW(L"vampire.dll"));
        const auto python=GetModuleHandleW(L"vampire_python21.dll");
        if (binary.sha256!="996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8" ||
            Module::Inspect(python).sha256!="9e65e6d091bfed21dd98ed0b99397c217a793e0b80be4db2fdb9bbb12b9e116d") throw std::runtime_error("Unsupported game/Python profile");
        game=binary.base;
        py.module=Export<decltype(py.module)>(python,"PyImport_AddModule");
        py.method=Export<decltype(py.method)>(python,"PyObject_CallMethod");
        py.get=Export<decltype(py.get)>(python,"PyDict_GetItemString");
        py.attribute=Export<decltype(py.attribute)>(python,"PyObject_GetAttrString");
        py.integer=Export<decltype(py.integer)>(python,"PyInt_AsLong");
        py.number=Export<decltype(py.number)>(python,"PyFloat_AsDouble");
        py.bytes=Export<decltype(py.bytes)>(python,"PyString_AsStringAndSize");
        py.initialized=Export<decltype(py.initialized)>(python,"Py_IsInitialized");
        py.fetch=Export<decltype(py.fetch)>(python,"PyErr_Fetch");
        py.restore=Export<decltype(py.restore)>(python,"PyErr_Restore");
        py.clear=Export<decltype(py.clear)>(python,"PyErr_Clear");
        py.dict=Export<void*>(python,"PyDict_Type");py.intType=Export<void*>(python,"PyInt_Type");
        py.floatType=Export<void*>(python,"PyFloat_Type");py.stringType=Export<void*>(python,"PyString_Type");
        py.none=Export<void*>(python,"_Py_NoneStruct");
        ready.store(true);log("CAINE_PYTHON_BRIDGE_READY: read-only flags, quests, inventory, typed scalars; native exceptions preserved");return true;
    } catch (const std::exception& failure) { log(std::string("CAINE_PYTHON_BRIDGE_UNAVAILABLE: ")+failure.what());return false; }
}
int ReadScriptScalar(uint32_t kind,const char* name,CaineScalarV1& output) {
    if (!ready.load() || output.size<sizeof(output) || kind>CAINE_SCRIPT_PLAYER_INFO) return 0;
    if (!window || !IsWindow(window)) EnumWindows(Window,0);
    if (!window || GetWindowThreadProcessId(window,nullptr)!=GetCurrentThreadId() || !py.initialized()) return 0;
    const auto count=name?strnlen_s(name,1025):0;
    if (kind!=CAINE_SCRIPT_PLAYER_PRESENT && (!count || count>1024)) return 0;
    Errors errors;
    if (kind==CAINE_SCRIPT_GLOBAL) {
        const auto globals=*reinterpret_cast<Object**>(game+0x72b370);
        MEMORY_BASIC_INFORMATION memory{};
        if (!globals || !VirtualQuery(globals,&memory,sizeof(memory)) || memory.State!=MEM_COMMIT ||
            (memory.Protect&(PAGE_NOACCESS|PAGE_GUARD)) ||
            reinterpret_cast<uintptr_t>(globals)+sizeof(Object)>reinterpret_cast<uintptr_t>(memory.BaseAddress)+memory.RegionSize || globals->type!=py.dict) return 0;
        return Scalar(py.get(globals,name),output);
    }
    const auto main=py.module("__main__");
    const auto player=main?py.method(main,"FindPlayer",nullptr):nullptr;
    if (!player) return 0;
    if (kind==CAINE_SCRIPT_PLAYER_PRESENT) {
        output={};output.size=sizeof(output);output.kind=CAINE_SCALAR_INT32;output.integer=player!=py.none;
        native::Release(player);return 1;
    }
    if (player==py.none) { native::Release(player);return 0; }
    Object* value{};
    if (kind==CAINE_SCRIPT_PLAYER_PROPERTY) {
        // Names observed in installed scripts; never an arbitrary property expression.
        if (!strcmp(name,"clan") || !strcmp(name,"humanity") || !strcmp(name,"health") || !strcmp(name,"base_bloodpool")) value=py.attribute(player,name);
        else { native::Release(player);return -2; }
    } else if (kind==CAINE_SCRIPT_PLAYER_INFO) {
        const char* method=!strcmp(name,"name")?"GetName":!strcmp(name,"money")?"CurrentMoney":
            !strcmp(name,"masquerade")?"GetMasqueradeLevel":!strcmp(name,"male")?"IsMale":nullptr;
        if (method) value=py.method(player,method,nullptr);
        else { native::Release(player);return -2; }
    } else value=py.method(player,kind==CAINE_SCRIPT_QUEST?"GetQuestState":"HasItem","s",name);
    const auto result=Scalar(value,output);native::Release(value);native::Release(player);return result;
}
}
