#include "GameSA.h"
#include <windows.h>

namespace dhf { namespace game {

Api g_api;

void InitRealApi() {
    g_api.getAnimHierarchy = reinterpret_cast<void* (DHF_CDECL*)(void*)>(kAddrGetAnimHier);
    g_api.getAssociation   = reinterpret_cast<void* (DHF_CDECL*)(void*, const char*)>(kAddrGetAssociation);
    g_api.timeMs           = reinterpret_cast<const volatile uint32_t*>(kAddrTimeMs);
    g_api.players          = reinterpret_cast<const uint8_t*>(kAddrPlayers);
    g_api.playerFocus      = reinterpret_cast<const uint8_t*>(kAddrPlayerInFocus);
}

bool IsReadable(const void* p, size_t n) {
    if (!p) return false;
    const uint8_t* cur = static_cast<const uint8_t*>(p);
    const uint8_t* end = cur + n;
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi;
        if (VirtualQuery(cur, &mbi, sizeof(mbi)) == 0) return false;
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        cur = static_cast<const uint8_t*>(mbi.BaseAddress) + mbi.RegionSize;
    }
    return true;
}

bool IsSupportedExe() {
    // Same signature check used by plugin-sdk: 1.0 US (HOODLUM / compact layouts).
    const void* a = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x82457C));
    const void* b = reinterpret_cast<const void*>(static_cast<uintptr_t>(0x8245BC));
    if (IsReadable(a, 4) && Rd<uint32_t>(a, 0) == 0x94BFu) return true;
    if (IsReadable(b, 4) && Rd<uint32_t>(b, 0) == 0x94BFu) return true;
    return false;
}

size_t FindCallSites(uintptr_t target, std::vector<uintptr_t>& out) {
    const uint8_t* base = reinterpret_cast<const uint8_t*>(GetModuleHandleA(nullptr));
    if (!base) return 0;
    const IMAGE_DOS_HEADER* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const IMAGE_NT_HEADERS* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    const IMAGE_SECTION_HEADER* sec = IMAGE_FIRST_SECTION(nt);
    const size_t before = out.size();
    for (unsigned i = 0; i < nt->FileHeader.NumberOfSections; ++i) {
        if (!(sec[i].Characteristics & IMAGE_SCN_MEM_EXECUTE)) continue;
        const uint8_t* b = base + sec[i].VirtualAddress;
        size_t size = sec[i].Misc.VirtualSize > sec[i].SizeOfRawData ? sec[i].Misc.VirtualSize : sec[i].SizeOfRawData;
        if (size < 5 || !IsReadable(b, size)) continue;
        for (size_t k = 0; k + 5 <= size; ++k) {
            if (b[k] != 0xE8) continue;
            int32_t rel;
            std::memcpy(&rel, b + k + 1, 4);
            uint32_t dest = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(b + k) + 5u + static_cast<uint32_t>(rel));
            if (dest == static_cast<uint32_t>(target)) out.push_back(reinterpret_cast<uintptr_t>(b + k));
        }
    }
    return out.size() - before;
}

bool PatchCallSite(uintptr_t site, uintptr_t newTarget) {
    uint8_t* p = reinterpret_cast<uint8_t*>(site);
    if (p[0] != 0xE8) return false;
    DWORD old = 0;
    if (!VirtualProtect(p, 5, PAGE_EXECUTE_READWRITE, &old)) return false;
    int32_t rel = static_cast<int32_t>(static_cast<uint32_t>(newTarget) - (static_cast<uint32_t>(site) + 5u));
    std::memcpy(p + 1, &rel, 4);
    DWORD tmp = 0;
    VirtualProtect(p, 5, old, &tmp);
    FlushInstructionCache(GetCurrentProcess(), p, 5);
    return true;
}

}} // namespace dhf::game
