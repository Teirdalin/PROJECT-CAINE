#include <caine/python_blob.hpp>
#include <algorithm>
#include <array>
#include <climits>
#include <stdexcept>
namespace caine::native {
namespace {
template<class T> T Export(HMODULE module, const char* name) {
    const auto value = GetProcAddress(module, name);
    if (!value) throw std::runtime_error("Required Python snapshot API export absent");
    return reinterpret_cast<T>(value);
}
int Nibble(char value) {
    if (value >= '0' && value <= '9') return value-'0';
    if (value >= 'a' && value <= 'f') return value-'a'+10;
    throw std::runtime_error("Malformed NPC snapshot chunk");
}
}
void SnapshotCodec::Initialize(HMODULE python) {
    string_ = Export<decltype(string_)>(python, "PyString_FromStringAndSize");
    bytes_ = Export<decltype(bytes_)>(python, "PyString_AsStringAndSize");
    list_ = Export<decltype(list_)>(python, "PyList_New");
    size_ = Export<decltype(size_)>(python, "PyList_Size");
    get_ = Export<decltype(get_)>(python, "PyList_GetItem");
    set_ = Export<decltype(set_)>(python, "PyList_SetItem");
    error_ = Export<decltype(error_)>(python, "PyErr_SetString");
    exception_ = Export<Object**>(python, "PyExc_IOError");
    stringType_ = Export<void*>(python, "PyString_Type");
    listType_ = Export<void*>(python, "PyList_Type");
}
void SnapshotCodec::Release(Object* value) const {
    caine::native::Release(value);
}
Object* SnapshotCodec::String(const std::string& value) const {
    if (value.size() > INT_MAX) throw std::runtime_error("Python string exceeds x86 limit");
    const auto result = string_(value.data(),static_cast<int>(value.size()));
    if (!result) throw std::runtime_error("Cannot allocate NPC snapshot string");
    return result;
}
Object* SnapshotCodec::Error(const char* message) const noexcept { error_(*exception_, message); return nullptr; }
std::string SnapshotCodec::Bytes(Object* value, size_t maximum) const {
    char* buffer{}; int length{};
    if (!value || value->type != stringType_ || bytes_(value,&buffer,&length) != 0 || length < 0 || static_cast<size_t>(length) > maximum)
        throw std::runtime_error("Invalid embedded NPC state string");
    return {buffer,static_cast<size_t>(length)};
}
Object* SnapshotCodec::Encode(const std::string& value) const {
    if (value.size() > MaxSnapshot) throw std::runtime_error("Embedded snapshot too large");
    const size_t chunks = (value.size()+ChunkBytes-1)/ChunkBytes;
    auto result = list_(static_cast<int>(chunks+1));
    if (!result) throw std::runtime_error("Cannot allocate NPC snapshot chunks");
    try {
        // PyList_SetItem steals the item reference, including on failure.
        if (set_(result,0,String(marker_)) != 0) throw std::runtime_error("Cannot set snapshot marker");
        constexpr char digits[] = "0123456789abcdef";
        for (size_t index=0; index<chunks; ++index) {
            const auto count = std::min(ChunkBytes,value.size()-index*ChunkBytes);
            std::string text(count*2,'0');
            for (size_t i=0; i<count; ++i) {
                const auto byte = static_cast<unsigned char>(value[index*ChunkBytes+i]);
                text[i*2]=digits[byte>>4]; text[i*2+1]=digits[byte&15];
            }
            if (set_(result,static_cast<int>(index+1),String(text)) != 0) throw std::runtime_error("Cannot set snapshot chunk");
        }
        return result;
    } catch (...) { Release(result); throw; }
}
std::string SnapshotCodec::Decode(Object* value) const {
    if (!value) throw std::runtime_error("Missing NPC snapshot");
    if (value->type == stringType_) return Bytes(value,MaxSnapshot); // 0.2.0/legacy saves.
    if (value->type != listType_) throw std::runtime_error("Invalid embedded NPC state type");
    const int count = size_(value);
    if (count < 1 || static_cast<size_t>(count) > (MaxSnapshot+ChunkBytes-1)/ChunkBytes+1 || Bytes(get_(value,0),64) != marker_)
        throw std::runtime_error("Unsupported NPC snapshot chunk format");
    std::string result;
    for (int i=1; i<count; ++i) {
        const auto text = Bytes(get_(value,i),ChunkBytes*2);
        if (text.empty() || text.size()%2 || (i < count-1 && text.size() != ChunkBytes*2) || result.size()+text.size()/2 > MaxSnapshot)
            throw std::runtime_error("Invalid NPC snapshot chunk length");
        for (size_t at=0; at<text.size(); at+=2) result.push_back(static_cast<char>((Nibble(text[at])<<4)|Nibble(text[at+1])));
    }
    return result;
}
}
