#pragma once
#include "Platform.h"
#include "Config.h"

namespace dhf {

// Shared with the optional CLEO bridge script (DHF_TripBridge). The script reads `pedPtr`; when it is
// non-zero it runs the game's own TASK_FALL_AND_GET_UP (opcode 05BB) on that ped, clears `pedPtr`
// and writes `seq` into `ack`.
struct Mailbox {
    volatile uint32_t pedPtr;
    volatile uint32_t fallDown;
    volatile uint32_t timeMs;
    volatile uint32_t seq;
    volatile uint32_t ack;
    volatile uint32_t reserved[3];
};

namespace react {

struct Stats {
    uint32_t bulletsSeen = 0, hitsQueued = 0, hitsApplied = 0, trips = 0;
    uint32_t bridgePosted = 0, bridgeAcked = 0, bridgeTimeouts = 0;
    uint32_t smoothEngaged = 0, animsSuppressed = 0, writes = 0, preCalls = 0;
};

void Init(const Config& cfg);
void Shutdown();
// Called right before the game converts animation key frames into bone matrices.
// Returns true when PostUpdate() should be called afterwards.
bool PreUpdate(uintptr_t ped);
void PostUpdate(uintptr_t ped);
// Called for every bullet that reaches DoBulletImpact.
void OnBullet(const void* weapon, const void* victim, const float* start, const float* end, const float* hit);
const Stats& GetStats();
size_t TrackedPeds();

} // namespace react
} // namespace dhf

DHF_EXPORT_C dhf::Mailbox DHF_Mailbox;
