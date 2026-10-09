// Executes the exact installed save-file reader in an isolated host, never the game.
#include <caine/core.hpp>
#include <caine/python_save.hpp>
#include <unscripted/snapshot_codec.hpp>
#include <algorithm>
#include <cstring>
#include <fstream>
#include <iostream>
#include <map>
#include <stdexcept>
using unscripted::native::Object;
using PickleFile = caine::PythonSaveFile;
namespace {
unscripted::native::SnapshotCodec codec;
void Check(bool value,const char* message) { if (!value) throw std::runtime_error(message); }
template<class T> T Export(HMODULE module,const char* name) {
    const auto value=GetProcAddress(module,name); Check(value!=nullptr,name); return reinterpret_cast<T>(value);
}
std::map<void*,size_t> allocations;
bool overwritten{};
void __cdecl NoEngineConsole() {}
void __cdecl PythonFatal(const char* message) { std::cerr << "PYTHON_FATAL: " << message << std::endl; ExitProcess(2); }
void* __cdecl GuardAllocate(size_t size) {
    // Padding records the stock reader's overrun without corrupting the test heap.
    auto pointer=static_cast<unsigned char*>(malloc(size+8192)); Check(pointer!=nullptr,"test allocation");
    memset(pointer,0xcd,size+8192); allocations[pointer]=size; return pointer;
}
void __cdecl GuardFree(void* pointer) {
    if (!pointer) return;
    const auto entry=allocations.find(pointer); Check(entry!=allocations.end(),"unknown native test allocation");
    const auto bytes=static_cast<unsigned char*>(pointer);
    for (size_t i=entry->second;i<entry->second+8192;++i) if (bytes[i]!=0xcd) overwritten=true;
    allocations.erase(entry); free(pointer);
}
struct Reader {
    void** table;
    std::string data;
    int position{};
    size_t calls{};
};
int __fastcall Tell(Reader* self,void*) { return self->position; }
void __fastcall Seek(Reader* self,void*,int value) { Check(value>=0 && static_cast<size_t>(value)<=self->data.size(),"test seek"); self->position=value; }
int __fastcall Read(Reader* self,void*,void* out,int capacity,int count) {
    Check(count>=0 && count<=capacity,"native read arguments");
    ++self->calls;
    const auto available=self->data.size()-static_cast<size_t>(self->position);
    count=static_cast<int>(std::min(available,static_cast<size_t>(count)));
    memcpy(out,self->data.data()+self->position,static_cast<size_t>(count)); self->position+=count; return count;
}
void BindPythonImports(uint8_t* base,HMODULE python) {
    auto dos=reinterpret_cast<IMAGE_DOS_HEADER*>(base);
    auto nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    auto descriptor=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(base+nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
    for (;descriptor->Name;++descriptor) {
        if (_stricmp(reinterpret_cast<char*>(base+descriptor->Name),"vampire_python21.dll")) continue;
        auto names=reinterpret_cast<IMAGE_THUNK_DATA32*>(base+descriptor->OriginalFirstThunk);
        auto slots=reinterpret_cast<IMAGE_THUNK_DATA32*>(base+descriptor->FirstThunk);
        for (;names->u1.AddressOfData;++names,++slots) {
            Check(!IMAGE_SNAP_BY_ORDINAL32(names->u1.Ordinal),"ordinal Python import");
            const auto imported=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(base+names->u1.AddressOfData);
            const auto value=GetProcAddress(python,imported->Name); Check(value!=nullptr,"unresolved Python import");
            DWORD before{},after{}; Check(VirtualProtect(slots,sizeof(*slots),PAGE_READWRITE,&before)!=0,"IAT write");
            slots->u1.Function=reinterpret_cast<DWORD>(value); VirtualProtect(slots,sizeof(*slots),before,&after);
        }
        return;
    }
    throw std::runtime_error("Python import descriptor absent");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        std::cerr << "SNAPSHOT_TEST_START" << std::endl;
        Check(argc==3,"usage: unscripted_snapshot_tests vampire.dll vampire_python21.dll");
        const auto pythonPath=std::filesystem::absolute(argv[2]); SetDllDirectoryW(pythonPath.parent_path().c_str());
        const auto python=LoadLibraryW(pythonPath.c_str()); Check(python!=nullptr,"load installed Python");
        *Export<int*>(python,"Py_NoSiteFlag")=1;
        const auto pythonInfo=caine::Module::Inspect(python);
        caine::Hooks fatalHooks; void* fatalOriginal{}; std::string fatalError;
        std::vector<uint8_t> fatalBytes{0xa1,0,0,0,0,0x56,0x57,0x8b,0x7c,0x24,0x0c};
        const auto operand=reinterpret_cast<uint32_t>(pythonInfo.base)+0x71108;
        memcpy(fatalBytes.data()+1,&operand,4);
        bool installed=fatalHooks.Install(pythonInfo,{"test.python_fatal","9e65e6d091bfed21dd98ed0b99397c217a793e0b80be4db2fdb9bbb12b9e116d",0x53760,fatalBytes},reinterpret_cast<void*>(PythonFatal),&fatalOriginal,fatalError);
        Check(installed,fatalError.c_str());
        // The embedded interpreter normally redirects CRT output into an engine
        // console pipe. There is no engine in this host; retain its normal stdout.
        std::vector<uint8_t> pipeBytes{0x8b,0x0d,0,0,0,0,0x83,0xc8,0xff};
        const auto pipeOperand=reinterpret_cast<uint32_t>(pythonInfo.base)+0x859c8;
        memcpy(pipeBytes.data()+2,&pipeOperand,4);
        void* pipeOriginal{};
        installed=fatalHooks.Install(pythonInfo,{"test.console_pipe","9e65e6d091bfed21dd98ed0b99397c217a793e0b80be4db2fdb9bbb12b9e116d",0x2bff0,pipeBytes},reinterpret_cast<void*>(NoEngineConsole),&pipeOriginal,fatalError);
        Check(installed,fatalError.c_str());
        const auto library=pythonPath.parent_path().parent_path()/L"Vampire"/L"python"/L"lib";
        SetEnvironmentVariableW(L"PYTHONPATH",library.c_str());
        std::cerr << "Python initialize" << std::endl;
        Export<void(__cdecl*)()>(python,"Py_Initialize")(); codec.Initialize(python);
        auto path=Export<Object*(__cdecl*)(const char*)>(python,"PySys_GetObject")("path");
        auto libraryName=codec.String(library.u8string());
        Check(Export<int(__cdecl*)(Object*,Object*)>(python,"PyList_Append")(path,libraryName)==0,"installed stdlib path");
        codec.Release(libraryName);
        std::cerr << "Python ready" << std::endl;
        auto run=Export<int(__cdecl*)(const char*)>(python,"PyRun_SimpleString");
        auto module=Export<Object*(__cdecl*)(const char*)>(python,"PyImport_AddModule")("__main__");
        auto globals=Export<Object*(__cdecl*)(Object*)>(python,"PyModule_GetDict")(module);
        auto get=Export<Object*(__cdecl*)(Object*,const char*)>(python,"PyDict_GetItemString");
        auto set=Export<int(__cdecl*)(Object*,const char*,Object*)>(python,"PyDict_SetItemString");
        auto bytes=Export<int(__cdecl*)(Object*,char**,int*)>(python,"PyString_AsStringAndSize");
        auto asString=[&](Object* object) { char* data{};int length{}; Check(object && bytes(object,&data,&length)==0,"Python string result");return std::string(data,static_cast<size_t>(length)); };
        // DONT_RESOLVE: no engine entry point, DllMain or dependency startup runs.
        std::cerr << "Mapping native reader" << std::endl;
        const auto gamePath=std::filesystem::absolute(argv[1]);
        const auto mapped=LoadLibraryExW(gamePath.c_str(),nullptr,DONT_RESOLVE_DLL_REFERENCES); Check(mapped!=nullptr,"map installed reader image");
        const auto game=caine::Module::Inspect(mapped);
        constexpr char hash[]="996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8";
        std::cerr << "Native image inspected" << std::endl;
        Check(game.sha256==hash,"installed game profile"); BindPythonImports(game.base,python);
        caine::Hooks hooks; std::string error; void *newOriginal{},*freeOriginal{};
        installed=hooks.InstallBatch(game,{
            {{"fixture.new",hash,0x4312d5,{0x6a,1,0xff,0x74,0x24,8,0xe8,0x29,0x0e,0,0}},reinterpret_cast<void*>(GuardAllocate),&newOriginal},
            {{"fixture.delete",hash,0x430964,{0xff,0x74,0x24,4,0xe8,0xd4,0x0c,0,0}},reinterpret_cast<void*>(GuardFree),&freeOriginal}},error);
        Check(installed,error.c_str());
        std::cerr << "Guard allocator installed" << std::endl;
        void* table[20]{}; table[0]=reinterpret_cast<void*>(Tell);table[1]=reinterpret_cast<void*>(Seek);table[19]=reinterpret_cast<void*>(Read);
        using Line=Object*(__cdecl*)(PickleFile*,Object*);
        const auto nativeLine=reinterpret_cast<Line>(game.base+0x19bc80);
        {
            Reader reader{table,std::string(422,'x')+"\n"}; PickleFile file{{},&reader,static_cast<int>(reader.data.size())};
            auto value=nativeLine(&file,nullptr); Check(value!=nullptr,"stock reader result");
            const auto text=asString(value); codec.Release(value);
            Check(overwritten && text!=reader.data,"stock long-line corruption was not reproduced");
            Check(allocations.empty(),"stock test allocation leak");
            std::cout << "STOCK_READER_BUG_REPRODUCED: 422-byte line loses data and overruns 256-byte allocation\n";
        }
        const auto makeFile=reinterpret_cast<Object*(__cdecl*)(void*,int)>(game.base+0x19bb10);
        // Prove newly written chunks are safe even through the unpatched reader.
        const std::string compatible(4096,'\xff');
        auto compatibleValue=codec.Encode(compatible); Check(set(globals,"blob",compatibleValue)==0,"stock chunk fixture");codec.Release(compatibleValue);
        Check(run("import cPickle\nwire = cPickle.dumps({'Story_State':15,'Unscripted_State_v1':blob})\nassert max(map(len,wire.split('\\n'))) < 128\n")==0,"stock chunk pickle");
        Reader compatibleReader{table,asString(get(globals,"wire"))};
        auto compatibleFile=makeFile(&compatibleReader,static_cast<int>(compatibleReader.data.size()));
        Check(compatibleFile && set(globals,"reader",compatibleFile)==0,"stock file wrapper");codec.Release(compatibleFile);
        overwritten=false;
        Check(run("restored = cPickle.load(reader)\nassert restored['Story_State'] == 15\nroundtrip = restored['Unscripted_State_v1']\n")==0,"stock chunk restore");
        Check(codec.Decode(get(globals,"roundtrip"))==compatible && !overwritten && allocations.empty(),"stock chunk corruption");
        Check(caine::InstallLongStringReader(mapped,python,[](const std::string& message) { std::cout<<message<<'\n'; }),"production CAINE reader hook");
        Check(caine::LongStringReaderReady(),"CAINE readiness");
        for (const size_t length:{0u,1u,127u,128u,129u,255u,256u,257u,422u,4096u,1024u*1024u,64u*1024u*1024u}) {
            Reader reader{table,std::string(length,'z')+"\r\n\nNEXT\n"};
            PickleFile file{{},&reader,static_cast<int>(reader.data.size())};
            auto value=nativeLine(&file,nullptr); Check(asString(value)==std::string(length,'z')+"\r","patched long line mismatch");codec.Release(value);
            value=nativeLine(&file,nullptr);Check(asString(value)=="NEXT\n","newline/cursor mismatch");codec.Release(value);
            value=nativeLine(&file,nullptr);Check(asString(value).empty(),"EOF mismatch");codec.Release(value);
            Check(reader.calls<=length/4096+7,"long reader fell back to per-byte IO");
        }
        for (const size_t length:{0u,1u,47u,48u,49u,96u,97u,1024u*1024u}) {
            const std::string text(length,'\x8a');
            auto object=codec.Encode(text);
            Check(codec.Decode(object)==text,"chunk boundary roundtrip");codec.Release(object);
        }
        auto occurred=Export<Object*(__cdecl*)()>(python,"PyErr_Occurred");
        auto clear=Export<void(__cdecl*)()>(python,"PyErr_Clear");
        Reader truncated{table,"abc"}; PickleFile truncatedFile{{},&truncated,10};
        Check(nativeLine(&truncatedFile,nullptr)==nullptr && occurred(),"truncated block did not raise Python error");clear();
        Check(nativeLine(nullptr,nullptr)==nullptr && occurred(),"invalid reader did not raise Python error");clear();
        std::string tooLarge(unscripted::native::MaxSnapshot+1,'x');
        bool oversized=false;try {codec.Encode(tooLarge);}catch(const std::exception&) {oversized=true;}
        Check(oversized,"oversized snapshot accepted");tooLarge.clear();tooLarge.shrink_to_fit();
        // Exercise the game's actual Python file object plus installed cPickle.load.
        std::string payload;
        for (int i=0;i<8192;++i) payload+="Quotes '\"\\ and Unicode \xc3\xa9\xe6\xbc\xa2\xf0\x9f\xa7\x9b\n\r\t";
        payload.push_back('\0'); payload+="tail";
        auto encoded=codec.Encode(payload); Check(set(globals,"blob",encoded)==0,"set fixture blob");codec.Release(encoded);
        Check(run("import cPickle\nwire = cPickle.dumps({'Story_State':15, 'Unscripted_State_v1':blob})\nassert max(map(len,wire.split('\\n'))) < 128\n")==0,"bounded pickle lines");
        Reader chunkReader{table,asString(get(globals,"wire"))};
        auto file=makeFile(&chunkReader,static_cast<int>(chunkReader.data.size())); Check(file!=nullptr,"native Python file object");
        Check(set(globals,"reader",file)==0,"set native file");codec.Release(file);
        Check(run("restored = cPickle.load(reader)\nassert restored['Story_State'] == 15\nroundtrip = restored['Unscripted_State_v1']\n")==0,"native chunk restore");
        Check(codec.Decode(get(globals,"roundtrip"))==payload,"Unicode/control/large chunk roundtrip");
        auto legacy=codec.String(payload);Check(set(globals,"blob",legacy)==0,"set legacy blob");codec.Release(legacy);
        Check(run("wire = cPickle.dumps({'Story_State':15, 'Unscripted_State_v1':blob})\n")==0,"legacy pickle");
        Reader oldReader{table,asString(get(globals,"wire"))};file=makeFile(&oldReader,static_cast<int>(oldReader.data.size()));
        Check(set(globals,"reader",file)==0,"set old reader");codec.Release(file);
        Check(run("restored = cPickle.load(reader)\nassert restored['Story_State'] == 15\nroundtrip = restored['Unscripted_State_v1']\n")==0,"native legacy restore");
        Check(codec.Decode(get(globals,"roundtrip"))==payload,"legacy long Unicode/control snapshot roundtrip");
        for(const char* expression:{"[]","123","['unknown']","['Unscripted.hex.v1','a']","['Unscripted.hex.v1','gg']","['Unscripted.hex.v1',123]","['Unscripted.hex.v1','aa','bb']","['Unscripted.hex.v1','a'*98]"}) {
            Check(run((std::string("invalid = ")+expression+"\n").c_str())==0,"invalid fixture");
            bool rejected=false;try {codec.Decode(get(globals,"invalid"));}catch(...){rejected=true;}
            Check(rejected,"malformed chunk accepted");
        }
        Check(run("del reader\n")==0,"release native reader");
        Export<void(__cdecl*)()>(python,"Py_Finalize")();
        std::cout << "UNSCRIPTED_LONG_STRINGS_OK: production CAINE hook, 64 MiB line, stock-compatible chunks, native cPickle roundtrips, legacy, Unicode, control bytes, malformed and oversized rejection\n";
        return 0;
    } catch (const std::exception& error) { std::cerr<<error.what()<<'\n'; return 1; }
}
