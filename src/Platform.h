// Platform.h - compiler/ABI helpers and raw memory access helpers.
// The real target is a 32-bit MSVC build (GTA:SA is a 32-bit process).
// Other compilers are only used for static syntax checks / logic self-tests (see dev/).
#pragma once
#include <cstdint>
#include <cstddef>
#include <cstring>
#include <cmath>
#include <cstdio>

#if defined(_MSC_VER)
  #define DHF_THISCALL __thiscall
  #define DHF_FASTCALL __fastcall
  #define DHF_CDECL    __cdecl
  #define DHF_EXPORT_C extern "C" __declspec(dllexport)
#else
  #define DHF_THISCALL
  #define DHF_FASTCALL
  #define DHF_CDECL
  #define DHF_EXPORT_C extern "C"
#endif

namespace dhf {

// Raw reads/writes. memcpy keeps this alignment-safe and strict-aliasing-safe.
template <class T> inline T Rd(const void* base, size_t off) {
    T v;
    std::memcpy(&v, static_cast<const uint8_t*>(base) + off, sizeof(T));
    return v;
}
template <class T> inline void Wr(void* base, size_t off, const T& v) {
    std::memcpy(static_cast<uint8_t*>(base) + off, &v, sizeof(T));
}
// Game pointers are 32-bit. Reading them as uint32 keeps the offsets valid in every build.
inline void* RdPtr(const void* base, size_t off) {
    return reinterpret_cast<void*>(static_cast<uintptr_t>(Rd<uint32_t>(base, off)));
}

} // namespace dhf
