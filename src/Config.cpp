#include "Config.h"
#include <windows.h>
#include <cstdio>
#include <cstdlib>

namespace dhf {

Config::Config() {
    for (int i = 0; i < 64; ++i) weaponIntensity[i] = 0.0f;
    weaponIntensity[22] = 0.55f;  // Colt 45
    weaponIntensity[23] = 0.50f;  // Silenced
    weaponIntensity[24] = 1.00f;  // Desert Eagle
    weaponIntensity[25] = 1.45f;  // Shotgun
    weaponIntensity[26] = 1.50f;  // Sawn-off
    weaponIntensity[27] = 1.40f;  // SPAS-12
    weaponIntensity[28] = 0.50f;  // Micro SMG
    weaponIntensity[29] = 0.55f;  // MP5
    weaponIntensity[30] = 0.95f;  // AK-47
    weaponIntensity[31] = 0.90f;  // M4
    weaponIntensity[32] = 0.50f;  // Tec-9
    weaponIntensity[33] = 1.25f;  // Country rifle
    weaponIntensity[34] = 1.70f;  // Sniper rifle
    weaponIntensity[38] = 0.55f;  // Minigun (per bullet)

    watchNames = {"GETUP", "GETUP_FRONT", "FALL_FRONT", "FALL_BACK", "FALL_COLLAPSE",
                  "KO_SKID_FRONT", "KO_SKID_BACK", "KO_SHOT_FRONT", "KO_SHOT_STOM", "KO_SHOT_FACE",
                  "KO_SPIN_L", "KO_SPIN_R"};
    suppressNames = {"DAM_ARML_FRM", "DAM_ARMR_FRM", "DAM_LEGL_FRM", "DAM_LEGR_FRM", "DAM_STOMACH_FRM",
                     "DAM_ARML_BCK", "DAM_ARMR_BCK", "DAM_LEGL_BCK", "DAM_LEGR_BCK", "DAM_STOMACH_BCK",
                     "DAM_ARML_L", "DAM_ARMR_L", "DAM_ARML_R", "DAM_ARMR_R",
                     "SHOT_PARTIAL", "SHOT_PARTIAL_B", "SHOT_LEFTP", "SHOT_RIGHTP",
                     "HIT_FRONT", "HIT_BACK", "HIT_L", "HIT_R", "HIT_BEHIND", "HIT_WALK"};
}

namespace {

float GetF(const char* sec, const char* key, float def, const char* path) {
    char d[48], b[64];
    std::snprintf(d, sizeof d, "%g", static_cast<double>(def));
    GetPrivateProfileStringA(sec, key, d, b, sizeof b, path);
    return static_cast<float>(std::atof(b));
}
int GetI(const char* sec, const char* key, int def, const char* path) {
    return static_cast<int>(GetPrivateProfileIntA(sec, key, def, path));
}
bool GetB(const char* sec, const char* key, bool def, const char* path) {
    return GetI(sec, key, def ? 1 : 0, path) != 0;
}
std::string GetS(const char* sec, const char* key, const std::string& def, const char* path) {
    char b[2048];
    GetPrivateProfileStringA(sec, key, def.c_str(), b, sizeof b, path);
    return b;
}
std::string Join(const std::vector<std::string>& v) {
    std::string s;
    for (size_t i = 0; i < v.size(); ++i) { if (i) s += ","; s += v[i]; }
    return s;
}
std::vector<std::string> Split(const std::string& s) {
    std::vector<std::string> out;
    std::string cur;
    auto flush = [&]() {
        size_t a = cur.find_first_not_of(" \t\r\n");
        size_t b = cur.find_last_not_of(" \t\r\n");
        if (a != std::string::npos) {
            std::string t = cur.substr(a, b - a + 1);
            for (char& ch : t) if (ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
            out.push_back(t);
        }
        cur.clear();
    };
    for (char ch : s) { if (ch == ',' || ch == ';') flush(); else cur += ch; }
    flush();
    return out;
}

} // namespace

void LoadConfig(const char* p, Config& c) {
    c.enabled                 = GetB("General", "Enabled", c.enabled, p);
    c.affectPlayer            = GetB("General", "AffectPlayer", c.affectPlayer, p);
    c.logging                 = GetB("General", "Log", c.logging, p);
    c.maxDistance             = GetF("General", "MaxDistance", c.maxDistance, p);
    c.suppressVanillaHitAnims = GetB("General", "SuppressVanillaHitAnims", c.suppressVanillaHitAnims, p);
    c.suppressWindowMs        = GetI("General", "SuppressWindowMs", c.suppressWindowMs, p);
    c.suppressNames           = Split(GetS("General", "SuppressAnimNames", Join(c.suppressNames), p));

    c.globalIntensity = GetF("Reaction", "GlobalIntensity", c.globalIntensity, p);
    c.randomness      = GetF("Reaction", "Randomness", c.randomness, p);
    c.armFlinch       = GetF("Reaction", "ArmFlinch", c.armFlinch, p);
    c.legFlinch       = GetF("Reaction", "LegFlinch", c.legFlinch, p);
    c.torsoBend       = GetF("Reaction", "TorsoBend", c.torsoBend, p);
    c.headSnap        = GetF("Reaction", "HeadSnap", c.headSnap, p);
    c.maxAngleScale   = GetF("Reaction", "MaxAngleScale", c.maxAngleScale, p);
    c.knockback       = GetF("Reaction", "Knockback", c.knockback, p);
    c.hitDelayMs      = GetI("Reaction", "HitDelayMs", c.hitDelayMs, p);

    for (int i = 0; i < 64; ++i) {
        char key[16];
        std::snprintf(key, sizeof key, "%d", i);
        c.weaponIntensity[i] = GetF("Weapons", key, c.weaponIntensity[i], p);
    }

    c.tripEnabled         = GetB("Trip", "Enabled", c.tripEnabled, p);
    c.tripMinMoveState    = GetI("Trip", "MinMoveState", c.tripMinMoveState, p);
    c.tripMinSpeed        = GetF("Trip", "MinSpeed", c.tripMinSpeed, p);
    c.tripChanceLight     = GetF("Trip", "ChanceLight", c.tripChanceLight, p);
    c.tripChanceMedium    = GetF("Trip", "ChanceMedium", c.tripChanceMedium, p);
    c.tripChanceHeavy     = GetF("Trip", "ChanceHeavy", c.tripChanceHeavy, p);
    c.tripThighScale      = GetF("Trip", "ThighChanceScale", c.tripThighScale, p);
    c.tripCooldownMs      = GetI("Trip", "CooldownMs", c.tripCooldownMs, p);
    c.tripUseBridge       = GetB("Trip", "UseCleoBridge", c.tripUseBridge, p);
    c.tripBridgeTimeoutMs = GetI("Trip", "BridgeTimeoutMs", c.tripBridgeTimeoutMs, p);
    c.tripGroundTimeMs    = GetI("Trip", "GroundTimeMs", c.tripGroundTimeMs, p);
    c.tripFallDownParam   = GetI("Trip", "FallDownParam", c.tripFallDownParam, p);
    c.stumbleScale        = GetF("Trip", "StumbleScale", c.stumbleScale, p);

    c.smoothEnabled    = GetB("GetUpBlend", "Enabled", c.smoothEnabled, p);
    c.smoothTime       = GetF("GetUpBlend", "BlendTime", c.smoothTime, p);
    c.smoothTriggerDeg = GetF("GetUpBlend", "TriggerDegrees", c.smoothTriggerDeg, p);
    c.smoothEndDeg     = GetF("GetUpBlend", "EndDegrees", c.smoothEndDeg, p);
    c.smoothWatchMs    = GetI("GetUpBlend", "WatchWindowMs", c.smoothWatchMs, p);
    c.smoothAlways     = GetB("GetUpBlend", "WatchAlways", c.smoothAlways, p);
    c.watchNames       = Split(GetS("GetUpBlend", "WatchAnimNames", Join(c.watchNames), p));

    if (c.smoothTime < 0.05f) c.smoothTime = 0.05f;
    if (c.hitDelayMs < 0) c.hitDelayMs = 0;
}

} // namespace dhf
