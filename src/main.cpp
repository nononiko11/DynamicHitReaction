// Dynamic Hit Feedback - GTA San Andreas 1.0 US ASI plugin.
//
// Two engine entry points are hooked by retargeting every `call` to them:
//   CWeapon::DoBulletImpact (0x73B550)  - tells us who got shot, where and with what
//   CEntity::UpdateRpHAnim  (0x532B20)  - runs right before the game turns animation key frames into
//                                         bone matrices, which is the right moment to add procedural motion
// Every piece of code that runs inside those hooks is wrapped in SEH: if anything unexpected happens
// the mod switches itself off instead of crashing the game.
#include <windows.h>
#include <string>
#include <vector>
#include "Config.h"
#include "GameSA.h"
#include "Log.h"
#include "Reactions.h"
#include "Skeleton.h"

namespace {

constexpr const char* kVersion = "1.0.0";

using UpdateFn = void (DHF_THISCALL*)(void*);
using BulletFn = void (DHF_THISCALL*)(void*, void*, void*, void*, void*, void*, int);

UpdateFn g_origUpdate = nullptr;
BulletFn g_origBullet = nullptr;
volatile bool g_active = false;
int g_faults = 0;

[[maybe_unused]] void OnFault(const char* where) {
    ++g_faults;
    dhf::log::WriteLimited(5, 20, "FAULT (access violation) inside %s - count=%d", where, g_faults);
    if (g_faults >= 3) {
        g_active = false;
        dhf::log::Write("too many faults: Dynamic Hit Feedback is disabled for the rest of this session");
    }
}

bool SafePre(void* ped) {
#ifdef _MSC_VER
    __try { return dhf::react::PreUpdate(reinterpret_cast<uintptr_t>(ped)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { OnFault("PreUpdate"); return false; }
#else
    return dhf::react::PreUpdate(reinterpret_cast<uintptr_t>(ped));
#endif
}

void SafePost(void* ped) {
#ifdef _MSC_VER
    __try { dhf::react::PostUpdate(reinterpret_cast<uintptr_t>(ped)); }
    __except (EXCEPTION_EXECUTE_HANDLER) { OnFault("PostUpdate"); }
#else
    dhf::react::PostUpdate(reinterpret_cast<uintptr_t>(ped));
#endif
}

void SafeBullet(void* weapon, void* victim, void* start, void* end, void* colPoint) {
#ifdef _MSC_VER
    __try {
        dhf::react::OnBullet(weapon, victim, static_cast<const float*>(start), static_cast<const float*>(end),
                             static_cast<const float*>(colPoint));
    }
    __except (EXCEPTION_EXECUTE_HANDLER) { OnFault("OnBullet"); }
#else
    dhf::react::OnBullet(weapon, victim, static_cast<const float*>(start), static_cast<const float*>(end),
                         static_cast<const float*>(colPoint));
#endif
}

// CEntity::UpdateRpHAnim(this) - thiscall without stack arguments.
void DHF_FASTCALL Hook_UpdateRpHAnim(void* self, void* /*edx*/) {
    bool post = false;
    if (g_active && dhf::game::IsPedEntity(self)) post = SafePre(self);
    g_origUpdate(self);
    if (post && g_active) SafePost(self);
}

// CWeapon::DoBulletImpact(this, owner, victim, start, end, colPoint, incrementalHit) - thiscall, callee pops 6 args.
void DHF_FASTCALL Hook_DoBulletImpact(void* self, void* /*edx*/, void* owner, void* victim, void* start, void* end,
                                      void* colPoint, int incremental) {
    if (g_active) SafeBullet(self, victim, start, end, colPoint);
    g_origBullet(self, owner, victim, start, end, colPoint, incremental);
}

size_t Redirect(uintptr_t target, uintptr_t hook, const char* name) {
    std::vector<uintptr_t> sites;
    dhf::game::FindCallSites(target, sites);
    size_t patched = 0;
    for (uintptr_t s : sites) if (dhf::game::PatchCallSite(s, hook)) ++patched;
    dhf::log::Write("hook %-22s: %u call site(s) found, %u patched", name, static_cast<unsigned>(sites.size()), static_cast<unsigned>(patched));
    return patched;
}

std::string WithExtension(const char* modulePath, const char* ext) {
    std::string p(modulePath);
    size_t dot = p.find_last_of('.');
    size_t slash = p.find_last_of("\\/");
    if (dot != std::string::npos && (slash == std::string::npos || dot > slash)) p.resize(dot);
    return p + ext;
}

void Init(HMODULE self) {
    char path[MAX_PATH] = {};
    GetModuleFileNameA(self, path, MAX_PATH);
    const std::string ini = WithExtension(path, ".ini");
    const std::string logPath = WithExtension(path, ".log");

    dhf::Config cfg;
    dhf::LoadConfig(ini.c_str(), cfg);
    dhf::log::Init(logPath.c_str(), cfg.logging);
    dhf::log::Write("Dynamic Hit Feedback %s starting (config: %s)", kVersion, ini.c_str());
    if (!cfg.enabled) { dhf::log::Write("disabled in the ini (Enabled=0)"); return; }
    if (!dhf::game::IsSupportedExe()) {
        dhf::log::Write("unsupported executable - this mod needs GTA San Andreas 1.0 US (HOODLUM/compact). Nothing was patched.");
        return;
    }

    dhf::game::InitRealApi();
    dhf::react::Init(cfg);
    g_origUpdate = reinterpret_cast<UpdateFn>(dhf::game::kAddrUpdateRpHAnim);
    g_origBullet = reinterpret_cast<BulletFn>(dhf::game::kAddrDoBulletImpact);

    const size_t pose = Redirect(dhf::game::kAddrUpdateRpHAnim, reinterpret_cast<uintptr_t>(&Hook_UpdateRpHAnim), "UpdateRpHAnim");
    const size_t bullet = Redirect(dhf::game::kAddrDoBulletImpact, reinterpret_cast<uintptr_t>(&Hook_DoBulletImpact), "DoBulletImpact");
    if (!pose) dhf::log::Write("WARNING: no call sites for UpdateRpHAnim found - procedural reactions will not run (another mod may have replaced them)");
    if (!bullet) dhf::log::Write("WARNING: no call sites for DoBulletImpact found - bullet hits cannot be detected");
    g_active = pose != 0 || bullet != 0;
    dhf::log::Write("ready (active=%d). Weapons with reactions: see [Weapons] in the ini.", g_active ? 1 : 0);
}

} // namespace

BOOL APIENTRY DllMain(HMODULE module, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(module);
        Init(module);
    } else if (reason == 0 /* DLL_PROCESS_DETACH */) {
        if (g_active) dhf::react::Shutdown();
    }
    return TRUE;
}
