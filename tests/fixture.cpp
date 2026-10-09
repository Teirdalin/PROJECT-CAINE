#include <windows.h>
extern "C" __declspec(noinline) int __cdecl FixtureAdd(int value) {
    volatile int result = value;
    result = result * 3;
    result = result + 7;
    return result;
}
BOOL WINAPI DllMain(HINSTANCE, DWORD, LPVOID) { return TRUE; }
extern "C" __declspec(noinline) int __cdecl FixtureSubtract(int value) {
    volatile int result=value;result=result*2;result=result-4;return result;
}
