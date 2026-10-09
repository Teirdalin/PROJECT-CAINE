// Dependency acceptance on the installed interpreter, without launching/modifying the game.
#include <windows.h>
#include <filesystem>
#include <iostream>
#include <stdexcept>
template<class T> T Load(HMODULE module, const char* name) {
    const auto value = GetProcAddress(module, name);
    if (!value) throw std::runtime_error(name);
    return reinterpret_cast<T>(value);
}
int wmain(int argc, wchar_t** argv) {
    try {
        if (argc != 2) throw std::runtime_error("Supply the installed vampire_python21.dll path");
        const auto directory = std::filesystem::path(argv[1]).parent_path();
        SetDllDirectoryW(directory.c_str());
        const auto module = LoadLibraryW(argv[1]);
        if (!module) throw std::runtime_error("Cannot load installed Python interpreter");
        auto initialize = Load<void(__cdecl*)()>(module, "Py_Initialize");
        auto run = Load<int(__cdecl*)(const char*)>(module, "PyRun_SimpleString");
        auto finalize = Load<void(__cdecl*)()>(module, "Py_Finalize");
        *Load<int*>(module, "Py_NoSiteFlag") = 1;
        std::cerr << "Initializing installed interpreter\n" << std::flush;
        initialize();
        std::cerr << "Testing embedded snapshot pickle\n" << std::flush;
        // Actual native pickle behavior and dictionary scalar isolation, not an emulated serializer.
        const auto result = run(
            "import cPickle\n"
            "G = {'Story_State': 15, 'CAINE_State_v1': '{\"schema_version\":1,\"text\":\"NPC history\"}'}\n"
            "first = cPickle.dumps(G)\n"
            "G['CAINE_State_v1'] = 'later branch'\n"
            "second = cPickle.dumps(G)\n"
            "a = cPickle.loads(first)\n"
            "b = cPickle.loads(second)\n"
            "assert a['CAINE_State_v1'] != b['CAINE_State_v1']\n"
            "assert a['Story_State'] == b['Story_State'] == 15\n"
            "assert 'NPC history' in a['CAINE_State_v1']\n");
        finalize();
        if (result) throw std::runtime_error("Installed Python serialization test failed");
        std::cout << "CAINE_PYTHON21_PICKLE_OK: installed interpreter restores independent embedded strings\n";
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
