#include "Reactions.h"
#include "GameSA.h"
#include "Log.h"
#include "Skeleton.h"
#include <algorithm>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

// Exported so the CLEO bridge can find it with GetProcAddress("DHF_Mailbox").
#ifdef _MSC_VER
DHF_EXPORT_C dhf::Mailbox DHF_Mailbox = {};
#else
extern "C" { dhf::Mailbox DHF_Mailbox = {}; }
#endif

namespace dhf { namespace react {

namespace {

// ------------------------------------------------------------------------------------------------
// tuning tables
// ------------------------------------------------------------------------------------------------
struct BoneDyn { float freq; float zeta; float maxDeg; };   // spring frequency (Hz), damping ratio, angle cap
const BoneDyn kDyn[B_COUNT] = {
    /* ROOT      */ {0.f, 0.f, 0.f},
    /* PELVIS    */ {2.6f, 0.60f, 14.f},
    /* SPINE     */ {2.8f, 0.55f, 22.f},
    /* SPINE1    */ {3.2f, 0.50f, 28.f},
    /* NECK      */ {4.2f, 0.50f, 34.f},
    /* HEAD      */ {4.8f, 0.42f, 40.f},
    /* LBROW     */ {0.f, 0.f, 0.f}, /* RBROW */ {0.f, 0.f, 0.f}, /* JAW */ {0.f, 0.f, 0.f},
    /* RCLAV     */ {4.0f, 0.55f, 18.f},
    /* RUPARM    */ {4.4f, 0.45f, 65.f},
    /* RFORE     */ {5.2f, 0.40f, 75.f},
    /* RHAND     */ {6.5f, 0.35f, 70.f},
    /* RFINGERS  */ {0.f, 0.f, 0.f}, /* RFINGER1 */ {0.f, 0.f, 0.f},
    /* LCLAV     */ {4.0f, 0.55f, 18.f},
    /* LUPARM    */ {4.4f, 0.45f, 65.f},
    /* LFORE     */ {5.2f, 0.40f, 75.f},
    /* LHAND     */ {6.5f, 0.35f, 70.f},
    /* LFINGERS  */ {0.f, 0.f, 0.f}, /* LFINGER1 */ {0.f, 0.f, 0.f},
    /* LTHIGH    */ {2.8f, 0.65f, 28.f},
    /* LCALF     */ {3.6f, 0.55f, 42.f},
    /* LFOOT     */ {4.8f, 0.45f, 42.f},
    /* LTOE      */ {0.f, 0.f, 0.f},
    /* RTHIGH    */ {2.8f, 0.65f, 28.f},
    /* RCALF     */ {3.6f, 0.55f, 42.f},
    /* RFOOT     */ {4.8f, 0.45f, 42.f},
    /* RTOE      */ {0.f, 0.f, 0.f},
};

const Bone kArmChain[2][4] = {{B_RHAND, B_RFORE, B_RUPARM, B_RCLAV}, {B_LHAND, B_LFORE, B_LUPARM, B_LCLAV}};
const float kArmAmp[4] = {40.f, 30.f, 22.f, 13.f};
const Bone kLegChain[2][3] = {{B_RFOOT, B_RCALF, B_RTHIGH}, {B_LFOOT, B_LCALF, B_LTHIGH}};
const float kLegAmp[3] = {36.f, 30.f, 20.f};

const Bone kKeyBones[] = {B_PELVIS, B_SPINE, B_SPINE1, B_HEAD, B_RUPARM, B_LUPARM, B_RFORE, B_LFORE,
                          B_RTHIGH, B_LTHIGH, B_RCALF, B_LCALF};

struct Seg { Bone a; Bone b; float r; };    // hit-test capsules: segment a->b with radius r (b == B_COUNT: extend)
const Seg kSegs[] = {
    {B_PELVIS, B_SPINE, 0.18f}, {B_SPINE, B_SPINE1, 0.17f}, {B_SPINE1, B_NECK, 0.18f},
    {B_NECK, B_HEAD, 0.075f}, {B_HEAD, B_COUNT, 0.12f},
    {B_RCLAV, B_RUPARM, 0.08f}, {B_RUPARM, B_RFORE, 0.065f}, {B_RFORE, B_RHAND, 0.055f}, {B_RHAND, B_RFINGERS, 0.055f},
    {B_LCLAV, B_LUPARM, 0.08f}, {B_LUPARM, B_LFORE, 0.065f}, {B_LFORE, B_LHAND, 0.055f}, {B_LHAND, B_LFINGERS, 0.055f},
    {B_LTHIGH, B_LCALF, 0.10f}, {B_LCALF, B_LFOOT, 0.07f}, {B_LFOOT, B_LTOE, 0.065f},
    {B_RTHIGH, B_RCALF, 0.10f}, {B_RCALF, B_RFOOT, 0.07f}, {B_RFOOT, B_RTOE, 0.065f},
};

constexpr int kMaxPending = 8;

// ------------------------------------------------------------------------------------------------
// state
// ------------------------------------------------------------------------------------------------
struct Spring { V3 a; V3 v; bool active = false; };     // ped-local axes (right, forward, up)
struct PendingHit { uint32_t atMs = 0; int weapon = 0; V3 pos; V3 dir; };

struct PedState {
    uintptr_t ped = 0;
    void* clump = nullptr;
    Skel skel;
    uint32_t nextSkelTry = 0;
    uint32_t lastSeenMs = 0, lastGameMs = 0;
    bool firstCall = true;
    bool calibUsable = false;

    Spring spr[B_COUNT];
    PendingHit pend[kMaxPending];
    int npend = 0;
    uint32_t suppressUntil = 0;
    uint32_t lastHitMs = 0;

    Q base[B_COUNT], written[B_COUNT];
    bool wrote[B_COUNT];
    V3 baseT[2], writtenT[2];
    bool wroteT[2];

    uint32_t tripCooldownUntil = 0;
    uint32_t watchUntil = 0, nextWatchPoll = 0;
    bool smoothing = false;
    int calmFrames = 0;

    bool bridgeWaiting = false;
    uint32_t bridgePostedAt = 0, bridgeSeq = 0;

    PedState() {
        for (int i = 0; i < B_COUNT; ++i) wrote[i] = false;
        wroteT[0] = wroteT[1] = false;
    }
};

Config g_cfg;
bool g_inited = false;
Stats g_stats;
std::unordered_map<uintptr_t, std::unique_ptr<PedState>> g_states;
uint32_t g_nextSweep = 0;
uint32_t g_rng = 0x9E3779B9u;
uint32_t g_seq = 0;
int g_bridgeFailures = 0;
bool g_bridgeAlive = false, g_bridgeDead = false;
bool g_assocLayoutBad = false;
int g_healthSane = 0, g_healthInsane = 0;
bool g_healthWarned = false;
bool g_hookWarned = false;

float Rnd01() { g_rng ^= g_rng << 13; g_rng ^= g_rng >> 17; g_rng ^= g_rng << 5; return static_cast<float>(g_rng & 0xFFFFFFu) / 16777216.0f; }
float RndSym() { return Rnd01() * 2.f - 1.f; }
const void* P(uintptr_t a) { return reinterpret_cast<const void*>(a); }
void* PW(uintptr_t a) { return reinterpret_cast<void*>(a); }

enum WClass { W_LIGHT, W_MEDIUM, W_HEAVY };
WClass ClassOf(float intensity) { return intensity < 0.7f ? W_LIGHT : (intensity < 1.15f ? W_MEDIUM : W_HEAVY); }

PedState* GetState(uintptr_t ped, bool create) {
    auto it = g_states.find(ped);
    if (it != g_states.end()) return it->second.get();
    if (!create) return nullptr;
    auto st = std::make_unique<PedState>();
    st->ped = ped;
    PedState* raw = st.get();
    g_states.emplace(ped, std::move(st));
    return raw;
}

void ClearReactions(PedState& st) {
    for (int b = 0; b < B_COUNT; ++b) { st.spr[b] = Spring(); st.wrote[b] = false; }
    st.wroteT[0] = st.wroteT[1] = false;
    st.npend = 0;
    st.smoothing = false;
    st.calmFrames = 0;
}

// ------------------------------------------------------------------------------------------------
// spring dynamics
// ------------------------------------------------------------------------------------------------
void StepAxis(float& x, float& v, float w, float z, float dt) {
    if (dt <= 0.f) return;
    if (z > 0.999f) z = 0.999f;
    const float wd = w * std::sqrt(1.f - z * z);
    const float e = std::exp(-z * w * dt);
    const float c = std::cos(wd * dt), s = std::sin(wd * dt);
    const float A = x, B = (v + z * w * x) / wd;
    x = e * (A * c + B * s);
    v = e * ((-z * w * A + wd * B) * c + (-z * w * B - wd * A) * s);
}

void StepSprings(PedState& st, float dt) {
    for (int b = 0; b < B_COUNT; ++b) {
        Spring& s = st.spr[b];
        if (!s.active) continue;
        const BoneDyn& dy = kDyn[b];
        const float w = 2.f * kPi * dy.freq;
        StepAxis(s.a.x, s.v.x, w, dy.zeta, dt);
        StepAxis(s.a.y, s.v.y, w, dy.zeta, dt);
        StepAxis(s.a.z, s.v.z, w, dy.zeta, dt);
        const float maxRad = Deg2Rad(dy.maxDeg * g_cfg.maxAngleScale);
        const float al = Length(s.a);
        if (al > maxRad) s.a = s.a * (maxRad / al);
        if (Length(s.a) < 0.0015f && Length(s.v) < 0.03f) s = Spring();
    }
}

// Adds an angular kick (peak swing ~ampDeg) about a world-space axis. Negative amplitude = opposite direction.
void AddImpulse(PedState& st, Bone b, V3 axisWorld, float ampDeg, const M3& pedRot) {
    const BoneDyn& dy = kDyn[b];
    if (dy.freq <= 0.f || !Has(st.skel, b) || !Finite(axisWorld)) return;
    const float w = 2.f * kPi * dy.freq;
    V3 vWorld = axisWorld * (Deg2Rad(ampDeg) * w * 1.35f);
    Spring& s = st.spr[b];
    s.v += Mul(Transposed(pedRot), vWorld);
    const float vmax = Deg2Rad(dy.maxDeg * g_cfg.maxAngleScale) * w * 1.8f;
    const float vl = Length(s.v);
    if (vl > vmax) s.v = s.v * (vmax / vl);
    s.active = true;
}

// Swing `bone` about its own joint, away from the incoming bullet (torque = r x F).
void Push(PedState& st, Bone bone, V3 hitPos, V3 dir, float ampDeg, const M3& pedRot) {
    if (!Has(st.skel, bone)) return;
    V3 r = Normalized(hitPos - BonePos(st.skel, bone), V3{0.f, 0.f, 1.f});
    V3 c = Cross(r, dir);
    const float len = Length(c);
    V3 axis = len > 1e-4f ? c * (1.f / len) : Normalized(Cross(dir, V3{0.f, 0.f, 1.f}), V3{1.f, 0.f, 0.f});
    AddImpulse(st, bone, axis, ampDeg * std::max(len, 0.35f), pedRot);
}

V3 RandomAxis() { return Normalized(V3{RndSym(), RndSym(), RndSym()}, V3{1.f, 0.f, 0.f}); }

// ------------------------------------------------------------------------------------------------
// hit analysis
// ------------------------------------------------------------------------------------------------
float DistPointSegment(V3 p, V3 a, V3 b) {
    V3 ab = b - a;
    const float l2 = Dot(ab, ab);
    const float t = l2 > 1e-8f ? Clampf(Dot(p - a, ab) / l2, 0.f, 1.f) : 0.f;
    return Length(p - (a + ab * t));
}

Bone FindRegion(const Skel& sk, V3 p) {
    float best = 1e9f;
    Bone bestBone = B_SPINE1;
    for (const Seg& s : kSegs) {
        if (!Has(sk, s.a)) continue;
        const V3 A = BonePos(sk, s.a);
        V3 B;
        if (s.b != B_COUNT && Has(sk, s.b)) {
            B = BonePos(sk, s.b);
        } else {
            V3 dir = {0.f, 0.f, 1.f};
            const int par = kBones[s.a].parent;
            if (par >= 0 && Has(sk, static_cast<Bone>(par))) dir = Normalized(A - BonePos(sk, static_cast<Bone>(par)), dir);
            B = A + dir * 0.18f;
        }
        const float d = DistPointSegment(p, A, B) / s.r;
        if (d < best) { best = d; bestBone = s.a; }
    }
    return bestBone;
}

Bone Canonical(Bone b) {   // fingers -> hand, toes -> foot, brows/jaw -> head
    switch (b) {
        case B_RFINGERS: case B_RFINGER1: return B_RHAND;
        case B_LFINGERS: case B_LFINGER1: return B_LHAND;
        case B_RTOE: return B_RFOOT;
        case B_LTOE: return B_LFOOT;
        case B_LBROW: case B_RBROW: case B_JAW: return B_HEAD;
        default: return b;
    }
}

bool InChain(Bone hb, bool arm, int& side, int& idx) {
    const int n = arm ? 4 : 3;
    for (int s = 0; s < 2; ++s)
        for (int i = 0; i < n; ++i)
            if ((arm ? kArmChain[s][i] : kLegChain[s][i]) == hb) { side = s; idx = i; return true; }
    return false;
}

const char* BoneName(Bone b) { return (b >= 0 && b < B_COUNT) ? kBones[b].name : "?"; }

// ------------------------------------------------------------------------------------------------
// reaction recipes
// ------------------------------------------------------------------------------------------------
void ArmHit(PedState& st, int side, int k, V3 h, V3 d, float I, const M3& pedRot) {
    const float mul = I * g_cfg.armFlinch;
    for (int j = k; j < 4; ++j) {
        const float fall = (j == k) ? 1.f : std::pow(0.62f, static_cast<float>(j - k));
        Push(st, kArmChain[side][j], h, d, kArmAmp[j] * fall * mul, pedRot);
    }
    if (k > 0) Push(st, kArmChain[side][k - 1], h, d, -0.5f * kArmAmp[k] * mul, pedRot);   // distal part lags behind
    // reflex: pull the arm in towards the body
    if (Has(st.skel, B_SPINE1) && Has(st.skel, kArmChain[side][0]) && Has(st.skel, kArmChain[side][2])) {
        V3 hand = BonePos(st.skel, kArmChain[side][0]), shoulder = BonePos(st.skel, kArmChain[side][2]);
        V3 armDir = Normalized(hand - shoulder, V3{0.f, 0.f, -1.f});
        V3 toBody = Normalized(BonePos(st.skel, B_SPINE1) - hand, V3{0.f, 1.f, 0.f});
        V3 axis = Normalized(Cross(armDir, toBody), V3{1.f, 0.f, 0.f});
        AddImpulse(st, kArmChain[side][2], axis, 20.f * mul * 0.7f, pedRot);
        AddImpulse(st, kArmChain[side][1], axis, 26.f * mul * 0.7f, pedRot);
    }
    Push(st, B_SPINE1, h, d, 5.f * I * g_cfg.torsoBend, pedRot);
    Push(st, B_HEAD, h, d, -3.f * I * g_cfg.headSnap, pedRot);
    AddImpulse(st, kArmChain[1 - side][2], RandomAxis(), 7.f * I * g_cfg.armFlinch, pedRot);   // other arm startles
}

void LegHit(PedState& st, int side, int k, V3 h, V3 d, float I, const M3& pedRot) {
    const float mul = I * g_cfg.legFlinch;
    for (int j = k; j < 3; ++j) {
        const float fall = (j == k) ? 1.f : std::pow(0.62f, static_cast<float>(j - k));
        Push(st, kLegChain[side][j], h, d, kLegAmp[j] * fall * mul, pedRot);
    }
    if (k > 0) Push(st, kLegChain[side][k - 1], h, d, -0.5f * kLegAmp[k] * mul, pedRot);
    const V3 fwd = Column(pedRot, 1);
    V3 legDir = {0.f, 0.f, -1.f};
    if (Has(st.skel, kLegChain[side][0]) && Has(st.skel, kLegChain[side][2]))
        legDir = Normalized(BonePos(st.skel, kLegChain[side][0]) - BonePos(st.skel, kLegChain[side][2]), legDir);
    V3 lift = Normalized(Cross(legDir, fwd), Column(pedRot, 0));   // swings the leg forward
    AddImpulse(st, kLegChain[side][2], lift, 16.f * mul, pedRot);
    AddImpulse(st, kLegChain[side][1], lift, -24.f * mul, pedRot);   // knee folds
    Push(st, B_PELVIS, h, d, 5.f * I, pedRot);
    Push(st, B_SPINE, h, d, 4.f * I * g_cfg.torsoBend, pedRot);
    AddImpulse(st, kArmChain[0][2], RandomAxis(), 8.f * I * g_cfg.armFlinch, pedRot);
    AddImpulse(st, kArmChain[1][2], RandomAxis(), 8.f * I * g_cfg.armFlinch, pedRot);
    Push(st, B_HEAD, h, d, 3.f * I * g_cfg.headSnap, pedRot);
}

void TorsoHit(PedState& st, V3 h, V3 d, float I, const M3& pedRot) {
    const V3 up = {0.f, 0.f, 1.f};
    V3 bend = Cross(up, d);
    const float bl = Length(bend);
    const V3 bendAxis = bl > 0.15f ? bend * (1.f / bl) : Normalized(Cross(Column(pedRot, 1), up), V3{1.f, 0.f, 0.f});
    const float bm = std::max(bl, 0.5f);
    const float tb = I * g_cfg.torsoBend;
    AddImpulse(st, B_SPINE, bendAxis, 14.f * tb * bm, pedRot);
    AddImpulse(st, B_SPINE1, bendAxis, 20.f * tb * bm, pedRot);
    AddImpulse(st, B_PELVIS, bendAxis, 6.f * tb * bm, pedRot);
    // twist from off-centre impacts
    if (Has(st.skel, B_SPINE1)) {
        V3 c = BonePos(st.skel, B_SPINE1);
        const float tw = (h.x - c.x) * d.y - (h.y - c.y) * d.x;
        const float twn = Clampf(tw * 3.0f, -1.f, 1.f);
        AddImpulse(st, B_SPINE1, up, 12.f * tb * twn, pedRot);
        AddImpulse(st, B_SPINE, up, 7.f * tb * twn, pedRot);
    }
    // the head lags behind the torso, then whips
    AddImpulse(st, B_NECK, bendAxis, -9.f * I * g_cfg.headSnap * bm, pedRot);
    AddImpulse(st, B_HEAD, bendAxis, -16.f * I * g_cfg.headSnap * bm, pedRot);
    for (int s = 0; s < 2; ++s) {
        Push(st, kArmChain[s][2], h, d, 10.f * I * g_cfg.armFlinch * 0.6f, pedRot);
        Push(st, kArmChain[s][1], h, d, 8.f * I * g_cfg.armFlinch * 0.6f, pedRot);
    }
}

void HeadHit(PedState& st, V3 h, V3 d, float I, const M3& pedRot) {
    const float hs = I * g_cfg.headSnap;
    Push(st, B_HEAD, h, d, 30.f * hs, pedRot);
    Push(st, B_NECK, h, d, 18.f * hs, pedRot);
    Push(st, B_SPINE1, h, d, 10.f * I * g_cfg.torsoBend, pedRot);
    Push(st, B_SPINE, h, d, 6.f * I * g_cfg.torsoBend, pedRot);
    for (int s = 0; s < 2; ++s) Push(st, kArmChain[s][2], h, d, 6.f * I * g_cfg.armFlinch, pedRot);
}

// Forward lurch with flailing arms - used when a runner is shot in the leg.
void Stumble(PedState& st, int legSide, float sev, const M3& pedRot) {
    sev *= g_cfg.stumbleScale;
    const V3 up = {0.f, 0.f, 1.f}, down = {0.f, 0.f, -1.f};
    const V3 fwd = Column(pedRot, 1), right = Column(pedRot, 0);
    const V3 lean = Normalized(Cross(up, fwd), right);
    AddImpulse(st, B_SPINE, lean, 18.f * sev, pedRot);
    AddImpulse(st, B_SPINE1, lean, 24.f * sev, pedRot);
    AddImpulse(st, B_PELVIS, lean, 9.f * sev, pedRot);
    AddImpulse(st, B_NECK, lean, 12.f * sev, pedRot);
    AddImpulse(st, B_HEAD, lean, 16.f * sev, pedRot);
    for (int side = 0; side < 2; ++side) {   // throw each arm up and out, on whichever side of the body it really is
        const Bone up = kArmChain[side][2], fore = kArmChain[side][1];
        if (!Has(st.skel, up) || !Has(st.skel, B_SPINE1)) continue;
        const float lateral = Dot(BonePos(st.skel, up) - BonePos(st.skel, B_SPINE1), right) >= 0.f ? 1.f : -1.f;
        const V3 out = Normalized(right * (0.7f * lateral) + fwd * 0.7f, fwd);
        const V3 axis = Normalized(Cross(down, out), right);
        AddImpulse(st, up, axis, 48.f * sev, pedRot);
        AddImpulse(st, fore, axis, 20.f * sev, pedRot);
    }
    const V3 back = Normalized(Cross(down, fwd * -1.f), right);
    const V3 fwdKick = Normalized(Cross(down, fwd), right);
    AddImpulse(st, kLegChain[legSide][2], fwdKick, 22.f * sev, pedRot);
    AddImpulse(st, kLegChain[legSide][1], back, 30.f * sev, pedRot);
}

// ------------------------------------------------------------------------------------------------
// trip handling
// ------------------------------------------------------------------------------------------------
bool PostToBridge(PedState& st, uintptr_t ped, uint32_t now) {
    if (!g_cfg.tripUseBridge || g_bridgeDead) return false;
    if (DHF_Mailbox.pedPtr != 0) return false;
    DHF_Mailbox.fallDown = static_cast<uint32_t>(g_cfg.tripFallDownParam);
    DHF_Mailbox.timeMs = static_cast<uint32_t>(g_cfg.tripGroundTimeMs);
    ++g_seq;
    DHF_Mailbox.seq = g_seq;
    DHF_Mailbox.pedPtr = static_cast<uint32_t>(ped);   // written last: this is what the script polls
    st.bridgeWaiting = true;
    st.bridgePostedAt = now;
    st.bridgeSeq = g_seq;
    ++g_stats.bridgePosted;
    return true;
}

void CheckBridge(PedState& st, uint32_t now) {
    if (!st.bridgeWaiting) return;
    if (DHF_Mailbox.ack == st.bridgeSeq) {
        st.bridgeWaiting = false;
        g_bridgeAlive = true;
        g_bridgeFailures = 0;
        ++g_stats.bridgeAcked;
        st.watchUntil = now + static_cast<uint32_t>(g_cfg.smoothWatchMs);
        log::WriteLimited(1, 60, "bridge: script acknowledged trip request #%u", st.bridgeSeq);
        return;
    }
    if (now - st.bridgePostedAt > static_cast<uint32_t>(g_cfg.tripBridgeTimeoutMs)) {
        st.bridgeWaiting = false;
        if (DHF_Mailbox.pedPtr == static_cast<uint32_t>(st.ped)) DHF_Mailbox.pedPtr = 0;   // withdraw
        ++g_stats.bridgeTimeouts;
        if (!g_bridgeAlive && ++g_bridgeFailures >= 3) {
            g_bridgeDead = true;
            log::Write("bridge: script never answered 3 trip requests - falling back to stumble only "
                       "(is DHF_TripBridge.cs installed in the CLEO folder?)");
        }
    }
}

void MaybeTrip(PedState& st, uintptr_t ped, Bone hitBone, WClass wc, uint32_t now, const M3& pedRot) {
    if (!g_cfg.tripEnabled) return;
    const bool foot = hitBone == B_LFOOT || hitBone == B_RFOOT || hitBone == B_LCALF || hitBone == B_RCALF ||
                      hitBone == B_LTOE || hitBone == B_RTOE;
    const bool thigh = hitBone == B_LTHIGH || hitBone == B_RTHIGH;
    if (!foot && !thigh) return;
    if (now < st.tripCooldownUntil) return;
    if (g_cfg.smoothEnabled && now < st.watchUntil) return;   // already down / getting up
    const int ms = Rd<int32_t>(P(ped), game::off::kMoveState);
    const float vx = Rd<float>(P(ped), game::off::kMoveSpeed), vy = Rd<float>(P(ped), game::off::kMoveSpeed + 4);
    const float sp = std::sqrt(vx * vx + vy * vy);
    const bool msValid = ms >= 0 && ms <= 7;
    const bool running = (msValid && ms >= g_cfg.tripMinMoveState) || (std::isfinite(sp) && sp > g_cfg.tripMinSpeed);
    if (!running) return;
    float chance = wc == W_LIGHT ? g_cfg.tripChanceLight : (wc == W_MEDIUM ? g_cfg.tripChanceMedium : g_cfg.tripChanceHeavy);
    if (thigh) chance *= g_cfg.tripThighScale;
    if (Rnd01() > chance) return;

    st.tripCooldownUntil = now + static_cast<uint32_t>(g_cfg.tripCooldownMs);
    ++g_stats.trips;
    const int legSide = (hitBone == B_LFOOT || hitBone == B_LCALF || hitBone == B_LTHIGH || hitBone == B_LTOE) ? 1 : 0;
    const bool bridged = PostToBridge(st, ped, now);
    Stumble(st, legSide, bridged ? 1.0f : 1.35f, pedRot);
    st.watchUntil = now + static_cast<uint32_t>(g_cfg.smoothWatchMs);
    log::WriteLimited(2, 80, "TRIP ped=%p hit=%s speed=%.3f moveState=%d bridge=%s", P(ped), BoneName(hitBone),
                      static_cast<double>(sp), ms, bridged ? "posted" : (g_bridgeDead ? "dead" : "off"));
}

// ------------------------------------------------------------------------------------------------
// applying a queued bullet hit
// ------------------------------------------------------------------------------------------------
void ApplyHit(PedState& st, uintptr_t ped, const PendingHit& hit, uint32_t now, const M3& pedRot) {
    const float base = (hit.weapon >= 0 && hit.weapon < 64) ? g_cfg.weaponIntensity[hit.weapon] : 0.f;
    if (!(base > 0.f)) return;
    const uint32_t since = now - st.lastHitMs;
    const float burst = since < 45u ? 0.45f : (since < 120u ? 0.75f : 1.0f);   // pellets / full-auto do not stack linearly
    st.lastHitMs = now;
    // Intensity grows faster than linearly so a pistol reads as a tap and a shotgun / sniper as a real blow.
    const float I = std::pow(base, 1.35f) * g_cfg.globalIntensity * burst * (1.f + RndSym() * g_cfg.randomness);
    const WClass wc = ClassOf(base);

    const Bone region = Canonical(FindRegion(st.skel, hit.pos));
    int side = 0, idx = 0;
    const char* kind = "torso";
    if (region == B_HEAD || region == B_NECK) { HeadHit(st, hit.pos, hit.dir, I, pedRot); kind = "head"; }
    else if (InChain(region, true, side, idx)) { ArmHit(st, side, idx, hit.pos, hit.dir, I, pedRot); kind = "arm"; }
    else if (InChain(region, false, side, idx)) { LegHit(st, side, idx, hit.pos, hit.dir, I, pedRot); kind = "leg"; }
    else TorsoHit(st, hit.pos, hit.dir, I, pedRot);

    if (g_cfg.knockback > 0.f && I > 0.6f) {
        const float kb = 0.02f * g_cfg.knockback * I;
        void* ms = PW(ped + game::off::kMoveSpeed);
        Wr<float>(ms, 0, Rd<float>(ms, 0) + hit.dir.x * kb);
        Wr<float>(ms, 4, Rd<float>(ms, 4) + hit.dir.y * kb);
    }
    MaybeTrip(st, ped, region, wc, now, pedRot);
    ++g_stats.hitsApplied;
    log::WriteLimited(0, 150, "hit ped=%p weapon=%d I=%.2f region=%s(%s)", P(ped), hit.weapon, static_cast<double>(I), BoneName(region), kind);
}

// ------------------------------------------------------------------------------------------------
// vanilla hit animation suppression / watch list
// ------------------------------------------------------------------------------------------------
void SuppressVanilla(PedState& st) {
    if (g_assocLayoutBad || !game::g_api.getAssociation) return;
    for (const std::string& n : g_cfg.suppressNames) {
        void* a = game::g_api.getAssociation(st.clump, n.c_str());
        if (!a) continue;
        const float amt = Rd<float>(a, 0x14), del = Rd<float>(a, 0x18);   // CAnimBlendAssociation blend amount / delta
        if (!(amt >= -0.01f && amt <= 1.5f) || !(std::fabs(del) < 1000.f)) {
            g_assocLayoutBad = true;
            log::Write("anim suppression disabled: association layout does not look right (amt=%f delta=%f)", static_cast<double>(amt), static_cast<double>(del));
            return;
        }
        if (amt == 0.f && del < 0.f) continue;
        Wr<float>(a, 0x14, 0.f);
        Wr<float>(a, 0x18, -10.f);
        ++g_stats.animsSuppressed;
        log::WriteLimited(3, 60, "suppressed vanilla anim %s on ped %p", n.c_str(), P(st.ped));
    }
}

bool PollWatchAnims(PedState& st) {
    if (!game::g_api.getAssociation) return false;
    for (const std::string& n : g_cfg.watchNames)
        if (game::g_api.getAssociation(st.clump, n.c_str())) return true;
    return false;
}

// ------------------------------------------------------------------------------------------------
// pose output: springs + get-up blend follower, written into the interpolated key frames
// ------------------------------------------------------------------------------------------------
void ApplyPose(PedState& st, uintptr_t ped, float dt, uint32_t now, const M3& pedRot, const V3& pedPos) {
    const Skel& sk = st.skel;
    const Calibration& cal = Calib();

    bool anySpring = false;
    for (int b = 0; b < B_COUNT; ++b) if (st.spr[b].active) { anySpring = true; break; }
    const bool watch = g_cfg.smoothEnabled && (g_cfg.smoothAlways || now < st.watchUntil || st.smoothing);
    if (!anySpring && !watch) {
        for (int b = 0; b < B_COUNT; ++b) st.wrote[b] = false;
        st.wroteT[0] = st.wroteT[1] = false;
        return;
    }
    if (!SkelStillValid(sk)) {   // the ped's model was rebuilt under us: never write through cached pointers
        st.skel = Skel();
        ClearReactions(st);
        return;
    }

    M3 rot[B_COUNT];
    for (int b = 0; b < B_COUNT; ++b) if (Has(sk, static_cast<Bone>(b))) rot[b] = BoneRot(sk, static_cast<Bone>(b));

    Q base[B_COUNT], qS[B_COUNT];
    bool ok[B_COUNT];
    for (int b = 0; b < B_COUNT; ++b) {
        ok[b] = false;
        const Bone bb = static_cast<Bone>(b);
        if (!Has(sk, bb)) continue;
        const int p = kBones[b].parent;
        if (p >= 0 && !Has(sk, static_cast<Bone>(p))) continue;
        if (b == B_ROOT ? !cal.rootOk : !cal.boneOk[b]) continue;
        Q cur = ReadFrameQ(sk, bb);
        if (!Finite(cur)) continue;
        // If the animation system did not rewrite this key frame since we last touched it, restart from the saved original.
        const Q b0 = (st.wrote[b] && NearlyEqual(cur, st.written[b])) ? st.base[b] : cur;
        st.base[b] = b0;
        base[b] = b0;
        ok[b] = true;
    }

    // Source pose = what was displayed last frame (this is the ragdoll pose right after the ragdoll mod lets go).
    float D = 0.f;
    if (watch) {
        for (int b = 0; b < B_COUNT; ++b) {
            if (!ok[b]) continue;
            const int p = kBones[b].parent;
            const M3 Pw = (p >= 0) ? rot[p] : pedRot;
            qS[b] = QuatFromLocal(Mul(Transposed(Pw), rot[b]));
        }
        for (Bone kb : kKeyBones) if (ok[kb]) D = std::max(D, AngleBetween(qS[kb], base[kb]));
        const float trig = Deg2Rad(g_cfg.smoothTriggerDeg), endA = Deg2Rad(g_cfg.smoothEndDeg);
        if (!st.smoothing) {
            if (D > trig) {
                st.smoothing = true; st.calmFrames = 0; ++g_stats.smoothEngaged;
                log::WriteLimited(4, 80, "get-up blend engaged ped=%p (pose gap %.0f deg)", P(ped), static_cast<double>(Rad2Deg(D)));
            }
        } else if (D < endA) {
            if (++st.calmFrames >= 4) {
                st.smoothing = false;
                log::WriteLimited(4, 80, "get-up blend finished ped=%p", P(ped));
            }
        } else {
            st.calmFrames = 0;
        }
    }
    float alpha = 1.f;   // fraction of the remaining gap closed this frame
    if (st.smoothing) {
        const float tau = std::max(0.02f, g_cfg.smoothTime / 3.f);
        alpha = dt > 0.f ? 1.f - std::exp(-dt / tau) : 0.f;
    }

    bool wroteAny = false;
    for (int b = 0; b < B_COUNT; ++b) {
        if (!ok[b]) { st.wrote[b] = false; continue; }
        const Bone bb = static_cast<Bone>(b);
        Q out = base[b];
        if (st.smoothing) out = Slerp(qS[b], base[b], alpha);
        if (b != B_ROOT && st.spr[b].active) {
            const int p = kBones[b].parent;
            const M3 Pw = (p >= 0) ? rot[p] : pedRot;
            const V3 thl = Mul(Transposed(Pw), Mul(pedRot, st.spr[b].a));    // ped-local angles -> parent-local rotation vector
            out = QuatFromLocal(Mul(RotationVector(thl), LocalFromQuat(out)));
        }
        if (Dot(out, base[b]) < 0.f) out = {-out.x, -out.y, -out.z, -out.w};
        if (!NearlyEqual(out, base[b])) {
            WriteFrameQ(sk, bb, out);
            st.written[b] = out;
            st.wrote[b] = true;
            wroteAny = true;
        } else {
            st.wrote[b] = false;
        }
    }
    // root / pelvis translation (only used by the get-up blend)
    for (int idx = 0; idx < 2; ++idx) {
        const Bone bb = idx == 0 ? B_ROOT : B_PELVIS;
        if (!Has(sk, bb) || !cal.transOk[idx]) { st.wroteT[idx] = false; continue; }
        const V3 cur = ReadFrameT(sk, bb);
        if (!Finite(cur)) continue;
        const V3 t0 = (st.wroteT[idx] && Length(cur - st.writtenT[idx]) < 1e-6f) ? st.baseT[idx] : cur;
        st.baseT[idx] = t0;
        V3 out = t0;
        if (st.smoothing) {
            const int p = kBones[bb].parent;
            const M3 Pw = (p >= 0) ? rot[p] : pedRot;
            const V3 posP = (p >= 0) ? BonePos(sk, static_cast<Bone>(p)) : pedPos;
            const V3 tS = Mul(Transposed(Pw), BonePos(sk, bb) - posP);
            out = tS + (t0 - tS) * alpha;
        }
        if (Length(out - t0) > 1e-6f && Finite(out)) {
            WriteFrameT(sk, bb, out);
            st.writtenT[idx] = out; st.wroteT[idx] = true; wroteAny = true;
        } else {
            st.wroteT[idx] = false;
        }
    }
    if (wroteAny) {
        ++g_stats.writes;
        // Force the game to rebuild the bone matrices from the key frames we just edited.
        void* pp = PW(ped);
        Wr<uint32_t>(pp, game::off::kFlags, Rd<uint32_t>(pp, game::off::kFlags) & ~game::off::kFlagDontUpdateHierarchy);
    }
}

void Sweep(uint32_t now) {
    if (now - g_nextSweep < 2000u && g_nextSweep != 0) return;
    g_nextSweep = now;
    for (auto it = g_states.begin(); it != g_states.end();) {
        if (now - it->second->lastSeenMs > 5000u) it = g_states.erase(it); else ++it;
    }
}

} // namespace

// ------------------------------------------------------------------------------------------------
// public API
// ------------------------------------------------------------------------------------------------
void Init(const Config& cfg) {
    g_cfg = cfg;
    g_inited = true;
    g_stats = Stats();
    g_states.clear();
    std::memset(&DHF_Mailbox, 0, sizeof(Mailbox));
    g_rng ^= static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&g_stats));
    if (!g_rng) g_rng = 0x9E3779B9u;
}

void Shutdown() {
    log::Write("shutdown: bullets=%u queued=%u applied=%u trips=%u bridge posted/acked/timeouts=%u/%u/%u "
               "blends=%u suppressed=%u pose writes=%u pre-update calls=%u",
               g_stats.bulletsSeen, g_stats.hitsQueued, g_stats.hitsApplied, g_stats.trips,
               g_stats.bridgePosted, g_stats.bridgeAcked, g_stats.bridgeTimeouts,
               g_stats.smoothEngaged, g_stats.animsSuppressed, g_stats.writes, g_stats.preCalls);
    g_inited = false;
}

const Stats& GetStats() { return g_stats; }
size_t TrackedPeds() { return g_states.size(); }

void OnBullet(const void* weapon, const void* victim, const float* start, const float* end, const float* hit) {
    if (!g_inited || !g_cfg.enabled || !weapon || !victim || !start || !end) return;
    ++g_stats.bulletsSeen;
    if (!g_hookWarned && g_stats.bulletsSeen >= 8 && g_stats.preCalls == 0) {
        g_hookWarned = true;
        log::Write("WARNING: bullets are being detected but the UpdateRpHAnim hook has never run. Another plugin probably took over "
                   "those call sites - try renaming this .asi so it loads before/after the other mods (ASIs load alphabetically).");
    }
    if (!game::IsPedEntity(victim)) return;
    const int wt = Rd<int32_t>(weapon, 0);
    if (wt < 0 || wt >= 64 || !(g_cfg.weaponIntensity[wt] > 0.f)) return;
    const uintptr_t ped = reinterpret_cast<uintptr_t>(victim);
    if (ped == game::PlayerPed() && !g_cfg.affectPlayer) return;
    const V3 s = {start[0], start[1], start[2]}, e = {end[0], end[1], end[2]};
    V3 h = hit ? V3{hit[0], hit[1], hit[2]} : e;
    if (!Finite(s) || !Finite(e) || !Finite(h)) return;
    if (Length(h - e) > 3.0f) h = e;   // colPoint did not look like a point near the impact
    const V3 d = Normalized(e - s, V3{0.f, 1.f, 0.f});
    PedState* st = GetState(ped, true);
    const uint32_t now = game::TimeMs();
    st->suppressUntil = now + static_cast<uint32_t>(std::max(0, g_cfg.suppressWindowMs));
    if (st->npend < kMaxPending) {
        PendingHit ph;
        ph.atMs = now; ph.weapon = wt; ph.pos = h; ph.dir = d;
        st->pend[st->npend++] = ph;
        ++g_stats.hitsQueued;
    }
}

bool PreUpdate(uintptr_t ped) {
    if (!g_inited || !g_cfg.enabled) return false;
    ++g_stats.preCalls;
    const uint32_t now = game::TimeMs();
    const uintptr_t player = game::PlayerPed();
    if (ped == player && !g_cfg.affectPlayer) return false;
    void* clump = RdPtr(P(ped), game::off::kRwObject);
    if (!clump) return false;

    M3 pedRot; V3 pedPos;
    if (!ReadPedMatrix(ped, pedRot, pedPos)) {
        log::WriteLimited(6, 5, "ped %p has no usable matrix (CPlaceable::m_matrix) - skipped", P(ped));
        return false;
    }
    if (player && g_cfg.maxDistance > 0.f) {
        M3 pr; V3 pp;
        if (ReadPedMatrix(player, pr, pp)) {
            const V3 dd = pedPos - pp;
            if (Dot(dd, dd) > g_cfg.maxDistance * g_cfg.maxDistance) return false;
        }
    }

    Sweep(now);
    PedState& st = *GetState(ped, true);
    if (st.clump != clump) { st = PedState(); st.ped = ped; st.clump = clump; }
    st.lastSeenMs = now;
    float dt = 0.f;
    if (!st.firstCall) dt = Clampf(static_cast<float>(now - st.lastGameMs) * 0.001f, 0.f, 0.1f);
    st.firstCall = false;
    st.lastGameMs = now;
    st.calibUsable = false;

    const float hp = Rd<float>(P(ped), game::off::kHealth);
    if (std::isfinite(hp) && hp > -1000.f && hp < 10000.f) ++g_healthSane; else ++g_healthInsane;
    if (!g_healthWarned && g_healthInsane > 40 && g_healthInsane > g_healthSane) {
        g_healthWarned = true;
        log::Write("WARNING: ped health values look wrong - CPed layout differs from what this build expects");
    }
    if (!(hp > 0.f)) { ClearReactions(st); return false; }   // dead: the ragdoll mod owns this ped

    CheckBridge(st, now);
    if (g_cfg.suppressVanillaHitAnims && now < st.suppressUntil) SuppressVanilla(st);

    const Calibration& cal = Calib();
    if (cal.failed) { st.npend = 0; return false; }

    if (!st.skel.valid) {
        if (now < st.nextSkelTry) return false;
        if (!BuildSkel(clump, ped, st.skel)) {
            st.nextSkelTry = now + 250;
            log::WriteLimited(6, 12, "skeleton of ped %p not usable yet: %s", P(ped), SkelLastError());
            return false;
        }
        log::WriteLimited(6, 12, "skeleton of ped %p ready (%d nodes)", P(ped), st.skel.numNodes);
    }
    if (!cal.done) {
        st.calibUsable = !(Rd<uint32_t>(P(ped), game::off::kFlags) & game::off::kFlagDontUpdateHierarchy);
        return st.calibUsable;   // PostUpdate() collects calibration samples
    }

    if (g_cfg.smoothEnabled && now >= st.nextWatchPoll) {
        st.nextWatchPoll = now + 100;
        if (PollWatchAnims(st)) st.watchUntil = now + static_cast<uint32_t>(g_cfg.smoothWatchMs);
    }

    for (int i = 0; i < st.npend;) {
        const uint32_t age = now - st.pend[i].atMs;
        if (age > 400u) {                      // queued while the skeleton / calibration was not ready: too old to look right
            st.pend[i] = st.pend[--st.npend];
        } else if (age >= static_cast<uint32_t>(g_cfg.hitDelayMs)) {
            const PendingHit h = st.pend[i];
            st.pend[i] = st.pend[--st.npend];
            ApplyHit(st, ped, h, now, pedRot);
        } else {
            ++i;
        }
    }
    StepSprings(st, dt);
    ApplyPose(st, ped, dt, now, pedRot, pedPos);
    return false;
}

void PostUpdate(uintptr_t ped) {
    PedState* st = GetState(ped, false);
    if (!st || !st->skel.valid || !st->calibUsable) return;
    const Calibration& cal = Calib();
    if (cal.done || cal.failed) return;
    M3 pr; V3 pp;
    if (!ReadPedMatrix(ped, pr, pp)) return;
    CalibAccumulate(st->skel, pr, pp);
}

}} // namespace dhf::react
