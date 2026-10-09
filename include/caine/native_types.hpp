#pragma once
#include <windows.h>
#include <cstdint>
#include <cstddef>
namespace caine::native {
// Confirmed CPython 2.1 x86 header and Bloodlines-owned file wrapper.
// These are in-memory views. Their padding/pointers are never serialized.
struct PythonObject { int32_t refs;void* type; };
struct PythonSaveFile { PythonObject header;void* reader;int32_t end; };
static_assert(sizeof(void*)==4 && sizeof(PythonObject)==8);
static_assert(sizeof(PythonSaveFile)==16 && offsetof(PythonSaveFile,reader)==8 && offsetof(PythonSaveFile,end)==12);
struct EntityHandle {
    uint32_t value{UINT32_MAX};
    uint32_t Index() const { return value&0x1fffu; }
    uint32_t Serial() const { return value>>13; }
    bool Valid() const { return value!=UINT32_MAX; }
};
static_assert(sizeof(EntityHandle)==4);
// Partial views of verified fields only. No guessed inheritance or full class size.
struct DialogueFields {
    static constexpr size_t Npc=0,Player=4,Data=8,Line=0x2830,Count=0x2834;
    static constexpr size_t PacketOpening=0,PacketResponses=0x804,TextBytes=2048;
};
struct HudDialogueFields {
    static constexpr size_t Active=0x1b4,Waiting=0x1b3,Opening=0xa28,Count=0x1228,Responses=0x122c;
};
inline void Release(PythonObject* object) {
    if (object && --object->refs==0) {
        const auto destroy=*reinterpret_cast<void(__cdecl**)(PythonObject*)>(static_cast<uint8_t*>(object->type)+0x18);
        destroy(object);
    }
}
}
