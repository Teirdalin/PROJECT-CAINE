#include <caine/core.hpp>
#include <caine/menu_view.hpp>
#include <caine/logging.hpp>
#include <atomic>

namespace {
constexpr char ShaderHash[] = "39dbd92d980cc969a33d7dedded57c4a77e6716917a21980e4f1ccab1858081a";
void* sceneContinuation{};
std::atomic<bool> ready{};
// Verified shaderapidx9.dll SwapBuffers: mesh flush and deactivation check have
// completed. ESI owns the shader API; +0x1c is IDirect3DDevice9*. The displaced
// instructions load that device immediately before its EndScene call.
void __cdecl Draw(void* shader) noexcept {
    if (!ready.load()) return;
    try {
        auto device = *reinterpret_cast<IDirect3DDevice9**>(static_cast<uint8_t*>(shader) + 0x1c);
        if (device) caine::PaintModernMenu(device);
    } catch (...) { caine::WriteLog("CAINE_MENU_RENDER_FAILED: exception at guarded render boundary"); }
}
__declspec(naked) void BeforeEndScene() {
    __asm {
        pushfd
        pushad
        push esi
        call Draw
        add esp, 4
        popad
        popfd
        jmp dword ptr [sceneContinuation]
    }
}
}
namespace caine {
bool InstallMenuRenderer(const std::function<void(const std::string&)>& log) {
    try {
        const auto shader = Module::Inspect(GetModuleHandleW(L"shaderapidx9.dll"));
        if (shader.sha256 != ShaderHash) { log("CAINE_MODERN_MENU_UNAVAILABLE: shader profile mismatch; native menu retained"); return false; }
        auto hooks = new Hooks(); // Process lifetime, like the native menu hooks.
        std::string error;
        if (!hooks->Install(shader, {"menu.render", ShaderHash, 0x1b2fc,
            {0x8b,0x46,0x1c,0x8b,0x08,0x50,0xff,0x91,0xa8,0,0,0}},
            reinterpret_cast<void*>(BeforeEndScene), &sceneContinuation, error)) {
            log("CAINE_MODERN_MENU_UNAVAILABLE: " + error); return false;
        }
        ready.store(true);
        log("CAINE_MODERN_MENU_HOOK: guarded shader SwapBuffers/EndScene boundary installed");
        return true;
    } catch (const std::exception& error) { log(error.what()); return false; }
}
}
