// Copyright (c) 2026 Teirdalin.
// SPDX-License-Identifier: LicenseRef-JDL-1
// Additional plugin-development permission: PLUGIN_API_PERMISSION.md.
#include <caine/mod_api.h>
namespace {
int __cdecl Start(const CaineHostV1* host) {
    host->log(host->context, "Hello from an independent PROJECT CAINE mod.");
    return 1;
}
const CaineModV1 descriptor{sizeof(CaineModV1), CAINE_MOD_ABI_V1, CAINE_FRAMEWORK_VERSION,
    "hello-caine", "Hello CAINE", "1.0.0", Start, nullptr, nullptr, nullptr, nullptr};
}
extern "C" const CaineModV1* __cdecl CaineMod_Query(uint32_t abi) {
    return abi == CAINE_MOD_ABI_V1 ? &descriptor : nullptr;
}

