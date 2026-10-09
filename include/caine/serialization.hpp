#pragma once
#include <string>
#include <vector>
namespace caine {
// CAINE-owned JSON envelopes, not native save blocks or in-memory C++ images.
// Unknown extension members survive validation/canonicalization unchanged.
std::vector<std::string> SerializationTypes();
std::string SerializeOwned(const std::string& input);
}
