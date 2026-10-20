#pragma once
#include <string>
#include <vector>

namespace dhf {

struct Config {
    // [General]
    bool  enabled = true;
    bool  affectPlayer = false;       // CJ is excluded by default (he is not handled by the ragdoll mod either)
    bool  logging = true;
    float maxDistance = 80.0f;        // peds further than this from the player are ignored
    bool  suppressVanillaHitAnims = true;
    int   suppressWindowMs = 900;

    // [Reaction]
    float globalIntensity = 1.0f;
    float randomness = 0.20f;
    float armFlinch = 1.0f;
    float legFlinch = 1.0f;
    float torsoBend = 1.0f;
    float headSnap = 1.0f;
    float maxAngleScale = 1.0f;
    float knockback = 0.5f;           // tiny shove along the bullet direction (0 = off)
    int   hitDelayMs = 34;            // reaction starts after this delay so lethal hits can be skipped

    // [Weapons] intensity per weapon id (0 = no reaction)
    float weaponIntensity[64];

    // [Trip]
    bool  tripEnabled = true;
    int   tripMinMoveState = 6;      // 4 walk, 5 jog, 6 run, 7 sprint
    float tripMinSpeed = 0.05f;      // game units per tick, alternative "is running" test
    float tripChanceLight = 0.45f;
    float tripChanceMedium = 0.70f;
    float tripChanceHeavy = 0.95f;
    float tripThighScale = 0.45f;    // chance multiplier when the thigh (not calf/foot) is hit
    int   tripCooldownMs = 4000;
    bool  tripUseBridge = true;      // hand the fall over to the CLEO bridge script (vanilla TASK_FALL_AND_GET_UP)
    int   tripBridgeTimeoutMs = 450;
    int   tripGroundTimeMs = 1800;
    int   tripFallDownParam = 1;
    float stumbleScale = 1.0f;

    // [GetUpBlend]
    bool  smoothEnabled = true;
    float smoothTime = 0.35f;        // seconds to ease from the ragdoll pose into the animation
    float smoothTriggerDeg = 35.0f;
    float smoothEndDeg = 6.0f;
    int   smoothWatchMs = 6000;
    bool  smoothAlways = false;

    std::vector<std::string> watchNames;
    std::vector<std::string> suppressNames;

    Config();
};

void LoadConfig(const char* iniPath, Config& c);

} // namespace dhf
