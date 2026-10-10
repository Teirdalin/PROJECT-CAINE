#pragma once
#include <caine/core.hpp>
#include <functional>

namespace caine {
// Process-lifetime, exact-profile client hooks. All entity access stays in the
// native camera/weapon/render callbacks; head posing is restored after each
// local-player draw. Configuration changes only publish atomics.
bool InstallMeleeCamera(const Module& client, const std::function<void(const std::string&)>& log);
bool MeleeCameraAvailable();
}
