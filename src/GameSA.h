// GameSA.h - everything that touches the game executable (GTA:SA 1.0 US).
// All game access goes through `Api` so the logic can be exercised in a synthetic self-test.
#pragma once
#include "Platform.h"
#include <vector>

namespace dhf { namespace game {

// Addresses for GTA SA 1.0 US (verified against MTA:SA / plugin-sdk / disassembly references).
constexpr uintptr_t kAddrUpdateRpHAnim   = 0x532B20; // CEntity::UpdateRpHAnim            thiscall, no args
constexpr uintptr_t kAddrDoBulletImpact  = 0x73B550; // CWeapon::DoBulletImpact           thiscall, 6 stack args
constexpr uintptr_t kAddrGetAnimHier     = 0x734A40; // GetAnimHierarchyFromSkinClump     cdecl(RpClump*)
constexpr uintptr_t kAddrGetAssociation  = 0x4D6870; // RpAnimBlendClumpGetAssociation    cdecl(RpClump*, const char*)
constexpr uintptr_t kAddrTimeMs          = 0xB7CB84; // CTimer::m_snTimeInMilliseconds
constexpr uintptr_t kAddrPlayers         = 0xB7CD98; // CWorld::Players[] (CPlayerInfo, 0x190 bytes each, m_pPed first)
constexpr uintptr_t kAddrPlayerInFocus   = 0xB7CD74; // CWorld::PlayerInFocus

// Entity / ped field offsets (CEntity / CPhysical / CPed layout of GTA SA 1.0 US).
namespace off {
constexpr size_t kMatrix      = 0x14; // CPlaceable::m_matrix  (CMatrix*)
constexpr size_t kRwObject    = 0x18; // CEntity::m_pRwObject  (RpClump* for peds)
constexpr size_t kFlags       = 0x1C; // CEntity::m_nFlags
constexpr size_t kType        = 0x36; // CEntity::m_nType (low 3 bits, 3 = ped)
constexpr size_t kMoveSpeed   = 0x44; // CPhysical::m_vecMoveSpeed
constexpr size_t kMoveState   = 0x534; // CPed::m_nMoveState
constexpr size_t kHealth      = 0x540; // CPed::m_fHealth
constexpr uint32_t kFlagDontUpdateHierarchy = 0x00800000u; // CEntity flags bit 23
}

struct Api {
    void* (DHF_CDECL* getAnimHierarchy)(void* clump) = nullptr;
    void* (DHF_CDECL* getAssociation)(void* clump, const char* name) = nullptr;
    const volatile uint32_t* timeMs = nullptr;
    const uint8_t* players = nullptr;
    const uint8_t* playerFocus = nullptr;
};
extern Api g_api;

void InitRealApi();
bool IsReadable(const void* p, size_t n);
bool IsSupportedExe();

inline uint32_t TimeMs() { return g_api.timeMs ? *g_api.timeMs : 0u; }
inline uintptr_t PlayerPed() {
    if (!g_api.players || !g_api.playerFocus) return 0;
    unsigned f = *g_api.playerFocus;
    if (f > 1u) f = 0u;
    return static_cast<uintptr_t>(Rd<uint32_t>(g_api.players + 0x190u * f, 0));
}
inline bool IsPedEntity(const void* entity) { return (Rd<uint8_t>(entity, off::kType) & 7u) == 3u; }

// Code patching: finds every `call rel32` to `target` in the executable's code sections and
// retargets it. Works regardless of which caller invokes the function.
size_t FindCallSites(uintptr_t target, std::vector<uintptr_t>& out);
bool PatchCallSite(uintptr_t site, uintptr_t newTarget);

}} // namespace dhf::game
