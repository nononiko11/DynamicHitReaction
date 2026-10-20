// Minimal stand-in for <windows.h>, ONLY used to syntax-check / self-test the logic on a non-Windows box.
#pragma once
#include <cstdint>
#include <cstddef>
typedef void* HMODULE; typedef void* HANDLE; typedef void* LPVOID; typedef const void* LPCVOID;
typedef unsigned long DWORD; typedef int BOOL; typedef unsigned int UINT; typedef const char* LPCSTR; typedef char* LPSTR;
typedef uint16_t WORD; typedef uint8_t BYTE; typedef size_t SIZE_T; typedef uint16_t WCHAR;
#define WINAPI
#define APIENTRY
#define TRUE 1
#define FALSE 0
#define MEM_COMMIT 0x1000
#define PAGE_NOACCESS 0x01
#define PAGE_GUARD 0x100
#define PAGE_EXECUTE_READWRITE 0x40
#define DLL_PROCESS_ATTACH 1
#define EXCEPTION_EXECUTE_HANDLER 1
#define IMAGE_SCN_MEM_EXECUTE 0x20000000
struct MEMORY_BASIC_INFORMATION { void* BaseAddress; void* AllocationBase; DWORD AllocationProtect; size_t RegionSize; DWORD State; DWORD Protect; DWORD Type; };
struct IMAGE_DOS_HEADER { uint16_t e_magic; uint8_t pad[58]; int32_t e_lfanew; };
struct IMAGE_FILE_HEADER { uint16_t Machine; uint16_t NumberOfSections; uint32_t a, b, c; uint16_t SizeOfOptionalHeader; uint16_t Characteristics; };
struct IMAGE_NT_HEADERS { uint32_t Signature; IMAGE_FILE_HEADER FileHeader; uint8_t OptionalHeader[224]; };
struct IMAGE_SECTION_HEADER { uint8_t Name[8]; union { uint32_t PhysicalAddress; uint32_t VirtualSize; } Misc; uint32_t VirtualAddress; uint32_t SizeOfRawData; uint32_t d[3]; uint16_t e[2]; uint32_t Characteristics; };
#define IMAGE_FIRST_SECTION(h) ((IMAGE_SECTION_HEADER*)((const uint8_t*)(h) + offsetof(IMAGE_NT_HEADERS, OptionalHeader) + (h)->FileHeader.SizeOfOptionalHeader))
inline HMODULE GetModuleHandleA(LPCSTR) { return nullptr; }
inline DWORD GetModuleFileNameA(HMODULE, LPSTR, DWORD) { return 0; }
inline BOOL VirtualProtect(LPVOID, size_t, DWORD, DWORD*) { return 1; }
inline size_t VirtualQuery(LPCVOID, MEMORY_BASIC_INFORMATION*, size_t) { return 0; }
inline BOOL FlushInstructionCache(HANDLE, LPCVOID, size_t) { return 1; }
inline HANDLE GetCurrentProcess() { return nullptr; }
inline BOOL DisableThreadLibraryCalls(HMODULE) { return 1; }
inline DWORD GetPrivateProfileStringA(LPCSTR, LPCSTR, LPCSTR def, LPSTR out, DWORD n, LPCSTR) { size_t i = 0; while (def[i] && i + 1 < n) { out[i] = def[i]; ++i; } out[i] = 0; return (DWORD)i; }
inline UINT GetPrivateProfileIntA(LPCSTR, LPCSTR, int def, LPCSTR) { return (UINT)def; }
#define MAX_PATH 260
