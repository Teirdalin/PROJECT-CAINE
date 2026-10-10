// Copyright (c) 2026 Teirdalin.
// SPDX-License-Identifier: LicenseRef-JDL-1
// Additional plugin-development permission: PLUGIN_API_PERMISSION.md.
#pragma once
#include <windows.h>
#include <stdint.h>
#define CAINE_MOD_ABI_V1 1u
#define CAINE_FRAMEWORK_VERSION 0x000315u
#ifdef __cplusplus
extern "C" {
#endif
/* Windows x86 C ABI. No STL objects, allocator ownership, or exceptions cross it.
   Callbacks run serially on CAINE's control worker, NOT the game thread.
   Paths and host table live until process exit. Mods are never hot-unloaded. */
typedef struct CaineModuleV1 {
    uint32_t size;
    HMODULE handle;
    uint8_t* base;
    uint32_t imageSize;
    char sha256[65];
} CaineModuleV1;
typedef struct CaineHookV1 {
    uint32_t size;
    const char* id;
    const char* moduleSha256;
    uint32_t rva;
    const uint8_t* expected;
    uint32_t expectedSize;
    void* detour;
    void** original;
} CaineHookV1;
enum { CAINE_MENU_OPEN = 1, CAINE_MENU_BUILD = 2, CAINE_MENU_ACTION = 3, CAINE_MENU_CHAR = 4, CAINE_MENU_WANTS_TEXT = 5, CAINE_MENU_VALUE = 6 };
/* Optional 0.3.8 gameUI callback: POLL must return 1 only while a mod owns a
   current game UI. BUILD/VALUE/CLOSE run on the presentation thread. All are
   nonblocking; native game operations belong in the mod's verified callbacks. */
enum { CAINE_GAMEUI_POLL=0x100u, CAINE_GAMEUI_CLOSE=0x101u };
enum { CAINE_CONTROL_TEXT, CAINE_CONTROL_HEADING, CAINE_CONTROL_BUTTON, CAINE_CONTROL_TOGGLE,
       CAINE_CONTROL_SLIDER, CAINE_CONTROL_INPUT, CAINE_CONTROL_CHOICE, CAINE_CONTROL_TAB };
enum { CAINE_CONTROL_SELECTED=1u, CAINE_CONTROL_DISABLED=2u, CAINE_CONTROL_SECRET=4u,
       CAINE_CONTROL_LIVE=8u, CAINE_CONTROL_INTEGER=16u, CAINE_CONTROL_SUBMIT=32u };
typedef struct CaineControlV1 {
    uint32_t size, kind, id, flags;
    const char* label;
    const char* text;
    const char* hint;
    double number, minimum, maximum;
    uint32_t maxBytes; /* UTF-8 field capacity, excluding NUL; at most 65536. */
} CaineControlV1;
typedef struct CaineValueV1 {
    uint32_t size;
    const char* text;
    double number;
} CaineValueV1;
typedef struct CaineMenuV1 {
    uint32_t size;
    void* context;
    void (__cdecl* addRow)(void* context, const char* label, uint32_t action);
    /* Optional 0.3.3 extension. Check size before accessing. Strings are copied
       immediately by the host. VALUE's text is valid only during that callback. */
    void (__cdecl* addControl)(void* context, const CaineControlV1* control);
    const CaineValueV1* value;
} CaineMenuV1;
/* Copied live context, never a native object pointer or save representation.
   CP1252 game text is converted to UTF-8. The token expires on every packet,
   closure or relinquish. A native choice is its current visible index (0..3).
   Since 0.3.11, responseCount=0 and line=-1 describe accepted player use of
   an NPC without an interactive native packet. Opening may be empty; source
   still identifies the NPC. Only close (-2) is valid in that context.
   Since 0.3.17 a dialogueless living pedestrian hit by the native use trace
   can have source entity://npc_vpedestrian/<hex UTF-8 targetname>. This is
   identity, not a file or action. Resolve a unique installed-map entity/spawner
   before persisting a person; never persist npcHandle or playerHandle. */
typedef struct CaineDialogueV1 {
    uint32_t size, responseCount;
    uint64_t token;
    int32_t line;
    uint32_t npcHandle, playerHandle;
    char source[1040];
    char opening[8192];
    char responses[4][8192];
} CaineDialogueV1;
enum { CAINE_SCALAR_NULL,CAINE_SCALAR_INT32,CAINE_SCALAR_FLOAT64,CAINE_SCALAR_UTF8 };
enum { CAINE_SCRIPT_GLOBAL,CAINE_SCRIPT_QUEST,CAINE_SCRIPT_HAS_ITEM,CAINE_SCRIPT_PLAYER_PRESENT,
       CAINE_SCRIPT_PLAYER_PROPERTY,CAINE_SCRIPT_PLAYER_INFO };
typedef struct CaineScalarV1 {
    uint32_t size,kind;
    int32_t integer;
    double number;
    uint32_t textBytes;
    char text[16384];
} CaineScalarV1;
enum { CAINE_TYPE_SERIALIZABLE=1u,CAINE_TYPE_NATIVE_VIEW=2u,CAINE_TYPE_LIVE_ONLY=4u,CAINE_TYPE_PARTIAL=8u };
typedef struct CaineTypeV1 { uint32_t size,version,flags;char name[64]; } CaineTypeV1;
typedef struct CaineHostV1 {
    uint32_t size;
    uint32_t abiVersion;
    uint32_t frameworkVersion;
    void* context;
    const wchar_t* gameDirectory;
    const wchar_t* modDirectory;
    const wchar_t* configFile;
    void (__cdecl* log)(void* context, const char* message);
    int (__cdecl* inspectModule)(void* context, HMODULE module, CaineModuleV1* output);
    /* Control worker only. Validates the batch before enabling it.
       Returns 1 on success, 0 with a bounded NUL-terminated diagnostic otherwise. */
    int (__cdecl* installHooks)(void* context, HMODULE module, const CaineHookV1* hooks,
                                uint32_t count, char* error, uint32_t errorSize);
    /* 0.3.8: game/presentation thread only. No network or script execution.
       readDialogue returns only a current visible, handle-validated packet.
       claimDialogue(0,0) relinquishes this mod's ownership. queueDialoguePick
       revalidates the token, native handles, line and response data at execution.
       -2 is the game's native end-conversation route. No generated scripts. */
    int (__cdecl* readDialogue)(void* context,CaineDialogueV1* output);
    int (__cdecl* claimDialogue)(void* context,uint64_t token,int enabled);
    int (__cdecl* queueDialoguePick)(void* context,uint64_t token,int index);
    /* Read-only Python 2.1 adapter on the game window's thread. 1=success,
       0=missing/not ready/wrong thread, -1=oversized, -2=unsupported type.
       Existing Python exceptions and reference ownership are preserved. */
    int (__cdecl* readScriptScalar)(void* context,uint32_t kind,const char* name,CaineScalarV1* output);
    /* Validate/canonicalize a CAINE-owned versioned JSON envelope. Returns
       required UTF-8 bytes INCLUDING NUL; zero rejects an unknown type/version.
       No partial output. Native memory images and pointers are not save data. */
    uint32_t (__cdecl* serializeOwned)(void* context,const char* input,uint32_t bytes,char* output,uint32_t capacity);
    /* Enumerate supported types starting at index zero, until return zero. */
    int (__cdecl* typeInfo)(void* context,uint32_t index,CaineTypeV1* output);
} CaineHostV1;
typedef struct CaineModV1 {
    uint32_t size;
    uint32_t abiVersion;
    uint32_t minimumFrameworkVersion;
    const char* id; /* matches XML id; [a-z0-9-], at most 64 characters */
    const char* name;
    const char* version;
    int (__cdecl* start)(const CaineHostV1* host);
    void (__cdecl* tick)(void); /* optional; bounded, nonblocking control work */
    /* These optional callbacks run on the MAIN MENU thread, not the control worker.
       configure returns 0 to close. Legacy BUILD adds at most 8 rows; modern BUILD
       may add up to 4096 typed controls. UINT32_MAX is a legacy label.
       No blocking I/O or network work in these callbacks. CAINE defers ACTION until paint. */
    int (__cdecl* configure)(const CaineMenuV1* menu, uint32_t event, uint32_t value);
    int (__cdecl* canEnterGame)(void);
    void (__cdecl* menuFrame)(void);
    int (__cdecl* gameUI)(const CaineMenuV1* menu, uint32_t event, uint32_t value);
} CaineModV1;
/* Export undecorated "CaineMod_Query". Return NULL for unsupported ABI.
   DllMain must not install hooks, wait for threads, or call game APIs. */
typedef const CaineModV1* (__cdecl* CaineModQuery)(uint32_t requestedAbi);
#ifdef __cplusplus
}
#endif
