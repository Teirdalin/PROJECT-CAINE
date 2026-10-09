#include <caine/python_bridge.hpp>
#include <caine/native_types.hpp>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#include <filesystem>
namespace {
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> T Export(HMODULE module,const char* name) {const auto value=GetProcAddress(module,name);Check(value!=nullptr,name);return reinterpret_cast<T>(value);}
}
int wmain(int argc,wchar_t** argv) {
    try {
        Check(argc==3,"supply installed game and Python DLLs");
        SetDllDirectoryW(std::filesystem::path(argv[2]).parent_path().c_str());
        const auto game=LoadLibraryExW(argv[1],nullptr,DONT_RESOLVE_DLL_REFERENCES);
        const auto python=LoadLibraryW(argv[2]);Check(game && python,"load native profiles");
        Check(caine::InitializePythonBridge([](const auto&){}),"production Python bridge profile");
        using Object=caine::native::PythonObject;
        *Export<int*>(python,"Py_NoSiteFlag")=1;
        Export<void(__cdecl*)()>(python,"Py_Initialize")();
        const auto run=Export<int(__cdecl*)(const char*)>(python,"PyRun_SimpleString");
        Check(run("class Player:\n clan=5\n humanity=7\n health=100\n base_bloodpool=15\n def CurrentMoney(self):\n  return 12.5\n def GetQuestState(self, name):\n  return 7\n def HasItem(self, name):\n  return 1\n\ndef FindPlayer():\n return Player()\n\nG = {'integer': 2147483647, 'number': 1.25, 'none': None, 'text': 'a\\x00\\xe9', 'large': '\\xe9'*9000, 'object': []}\n")==0,"native Python fixture");
        const auto main=Export<Object*(__cdecl*)(const char*)>(python,"PyImport_AddModule")("__main__");
        const auto get=Export<Object*(__cdecl*)(Object*,const char*)>(python,"PyObject_GetAttrString");
        auto globals=get(main,"G");Check(globals!=nullptr,"native global dictionary");
        memcpy(reinterpret_cast<uint8_t*>(game)+0x72b370,&globals,sizeof(globals));
        const auto window=CreateWindowExW(0,L"STATIC",L"CAINE isolated bridge fixture",WS_POPUP|WS_VISIBLE,-30000,-30000,1,1,nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
        Check(window!=nullptr,"fixture game thread window");
        CaineScalarV1 scalar{};scalar.size=sizeof(scalar);
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_PLAYER_PRESENT,nullptr,scalar)==1 && scalar.integer==1,"native player adapter");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_PLAYER_PROPERTY,"clan",scalar)==1 && scalar.integer==5,"verified player property");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_PLAYER_INFO,"money",scalar)==1 && scalar.number==12.5,"verified player method");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_PLAYER_PROPERTY,"__dict__",scalar)==-2 && caine::ReadScriptScalar(CAINE_SCRIPT_PLAYER_INFO,"SetQuest",scalar)==-2,"unapproved player reader");
        for (const auto kind:{CAINE_SCRIPT_QUEST,CAINE_SCRIPT_HAS_ITEM})
            Check(caine::ReadScriptScalar(kind,"known",scalar)==1 && scalar.integer==(kind==CAINE_SCRIPT_QUEST?7:1),"native quest/inventory method ownership");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"integer",scalar)==1 && scalar.integer==INT32_MAX,"native int32");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"number",scalar)==1 && scalar.number==1.25,"native float64");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"none",scalar)==1 && scalar.kind==CAINE_SCALAR_NULL,"native None");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"text",scalar)==1 && scalar.textBytes==4 && std::string(scalar.text,scalar.textBytes)==std::string("a\0\xc3\xa9",4),"length-delimited UTF-8 and NUL");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"large",scalar)==-1,"expanded UTF-8 silently truncated");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"object",scalar)==-2,"unsupported native object");
        Check(caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"absent",scalar)==0,"missing flag semantics");
        const auto setError=Export<void(__cdecl*)(Object*,const char*)>(python,"PyErr_SetString");
        const auto error=*Export<Object**>(python,"PyExc_IOError");setError(error,"preserved exception");
        caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"integer",scalar);
        Check(Export<Object*(__cdecl*)()>(python,"PyErr_Occurred")()==error,"native exception preservation");
        Export<void(__cdecl*)()>(python,"PyErr_Clear")();
        int wrongThread=1;std::thread other([&]{CaineScalarV1 value{};value.size=sizeof(value);wrongThread=caine::ReadScriptScalar(CAINE_SCRIPT_GLOBAL,"integer",value);});other.join();Check(wrongThread==0,"wrong-thread Python access");
        caine::native::Release(globals);DestroyWindow(window);Export<void(__cdecl*)()>(python,"Py_Finalize")();
        std::cout<<"CAINE_PYTHON_BRIDGE_OK: installed CPython 2.1, exact game profile, scalar types, owned/borrowed references, embedded NUL, UTF-8 bounds, quest/inventory adapters, exception and thread isolation; live player acceptance pending\n";return 0;
    } catch(const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
