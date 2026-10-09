#include <caine/core.hpp>
#include <unscripted/persistence_abi.hpp>
#include <cstdint>
#include <iostream>
#include <stdexcept>

namespace {
volatile uintptr_t capturedSelf{}, capturedArgs[3]{};
unsigned cancelCount{}, pulseCount{}, resetCount{};
void Check(bool value, const char* message) {
    if (!value) throw std::runtime_error(message);
}
// Model the game's callee stack cleanup independently of C++ declarations.
__declspec(naked) void MockRestore() {
    __asm {
        mov capturedSelf, ecx
        mov eax, [esp+4]
        mov capturedArgs, eax
        mov eax, [esp+8]
        mov capturedArgs[4], eax
        mov eax, [esp+12]
        mov capturedArgs[8], eax
        ret 12
    }
}
__declspec(naked) void MockOneArgument() {
    __asm {
        mov capturedSelf, ecx
        mov eax, [esp+4]
        mov capturedArgs, eax
        ret 4
    }
}
void __cdecl MockReset() { ++resetCount; }

// Supply ECX and reverse-order stack arguments exactly as a native virtual
// caller does. Reset ESP defensively so a failing test can report the mismatch.
// /Oy- ensures these locals remain addressable after a wrong callee cleanup.
__declspec(noinline) intptr_t Probe(void* target, void* self,
                                   const uintptr_t* args, unsigned count) {
    uintptr_t before{}, after{};
    __asm {
        mov before, esp
        mov edi, args
        mov esi, count
    push_loop:
        test esi, esi
        jz invoke
        dec esi
        push [edi+esi*4]
        jmp push_loop
    invoke:
        mov ecx, self
        xor edx, edx
        mov eax, target
        call eax
        mov after, esp
        mov esp, before
    }
    return static_cast<intptr_t>(after - before);
}
}
namespace unscripted {
// Presentation is outside this isolated ABI test; use production persistence
// functions without creating an audio device, game world or AI service.
void CancelAudio() { ++cancelCount; }
void CancelDialogue() {}
void PulseAudio() { ++pulseCount; }
}
int wmain(int argc, wchar_t** argv) {
    try {
        using namespace unscripted::native;
        const uintptr_t options[]{0x11223344u, 0x24681357u, 0x13572468u};
        uint32_t manager[2]{};
        Check(Probe(reinterpret_cast<void*>(MockOneArgument), manager, options, 3) == -8,
              "Probe did not detect the old ret-4 versus ret-12 mismatch");
        for (bool reentrant : {false, true}) {
            auto detours = test::Bind(reinterpret_cast<SaveBlock>(MockOneArgument),
                reinterpret_cast<RestoreBlock>(MockRestore), MockReset,
                reinterpret_cast<GameFrame>(MockOneArgument), reentrant, reentrant);
            for (unsigned i = 0; i < 128; ++i) {
                Check(Probe(detours.restore, manager, options, 3) == 0,
                      "Restore detour did not consume all three native arguments");
                Check(capturedSelf == reinterpret_cast<uintptr_t>(manager) &&
                      capturedArgs[0] == options[0] && capturedArgs[1] == options[1] &&
                      capturedArgs[2] == options[2], "Restore lost self, reader or native options");
            }
            Check(Probe(detours.save, manager, options, 1) == 0 &&
                  capturedSelf == reinterpret_cast<uintptr_t>(manager) && capturedArgs[0] == options[0],
                  "Save one-argument ABI changed");
            Check(Probe(detours.frame, manager, options, 1) == 0 && capturedArgs[0] == options[0],
                  "GameFrame one-argument ABI changed");
            Check(Probe(detours.reset, nullptr, nullptr, 0) == 0, "ResetGlobals zero-argument ABI changed");
        }
        Check(cancelCount == 258 && pulseCount == 2 && resetCount == 2,
              "Persistence audio/lifecycle pass-through changed");
        std::cout << "UNSCRIPTED_PERSISTENCE_ABI_OK: production detours, native stack cleanup, argument forwarding, inactive and reentrant paths\n";
        if (argc == 2) {
            const auto mapped = LoadLibraryExW(argv[1], nullptr, DONT_RESOLVE_DLL_REFERENCES);
            Check(mapped != nullptr, "Cannot map installed vampire.dll");
            const auto game = caine::Module::Inspect(mapped);
            Check(game.sha256 == "996e024577290cf0c91d74fcccd144089f1db2a1f8ef207844d1fa078939c3c8",
                  "Unsupported native restore image");
            Check(game.base[0x19b2e4] == 0xc2 && game.base[0x19b2e5] == 12 && game.base[0x19b2e6] == 0,
                  "Installed Restore no longer returns ret 12");
            auto detours = test::Bind(reinterpret_cast<SaveBlock>(MockOneArgument),
                reinterpret_cast<RestoreBlock>(game.base + 0x19b130), MockReset,
                reinterpret_cast<GameFrame>(MockOneArgument), false, false);
            // Byte 4 == 0 takes the installed Restore's no-data early return.
            // This executes real relocated native instructions and the actual
            // production detour, without initializing DLL imports or world state.
            {
                caine::Hooks hooks;
                void* trampoline{};
                std::string error;
                Check(hooks.Install(game, {"test.python.restore", game.sha256, 0x19b130,
                      {0x51,0x8a,0x41,0x04,0x56,0x84,0xc0,0x0f,0x84,0xa5,0x01,0x00,0x00}},
                      detours.restore, &trampoline, error), error.c_str());
                test::Bind(reinterpret_cast<SaveBlock>(MockOneArgument),
                    reinterpret_cast<RestoreBlock>(trampoline), MockReset,
                    reinterpret_cast<GameFrame>(MockOneArgument), false, false);
                for (unsigned i = 0; i < 128; ++i)
                    Check(Probe(game.base + 0x19b130, manager, options, 3) == 0,
                          "Production detour/trampoline and installed Restore left ESP misaligned");
                Check(hooks.RemoveAll(error), error.c_str());
            }
            FreeLibrary(mapped);
            std::cout << "UNSCRIPTED_INSTALLED_RESTORE_ABI_OK: 128 calls through guarded hook, production detour, trampoline and installed native ret-12 path; gameplay acceptance pending\n";
        }
        return 0;
    } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
