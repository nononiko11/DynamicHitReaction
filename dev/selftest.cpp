// Logic self-test: runs the REAL Skeleton/Reactions code against a synthetic ped living in fake game memory.
// It verifies maths and control flow (calibration, hit direction, idempotency, trip handshake, get-up blending,
// fail-safes). It cannot verify anything about the real game executable - that is what the in-game log is for.
#include <sys/mman.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <windows.h>
#include "../src/Config.h"
#include "../src/GameSA.h"
#include "../src/Log.h"
#include "../src/Math3.h"
#include "../src/Reactions.h"
#include "../src/Skeleton.h"

using namespace dhf;

static int g_fail = 0, g_pass = 0;
#define CHECK(cond, ...) do { if (cond) { ++g_pass; } else { ++g_fail; std::printf("  FAIL line %d: ", __LINE__); std::printf(__VA_ARGS__); std::printf("\n"); } } while (0)

// ---------------------------------------------------------------- fake game memory (32-bit addressable)
static uint8_t* g_arena = nullptr; static size_t g_used = 0;
static uint8_t* Alloc(size_t n) {
    if (!g_arena) {
        g_arena = static_cast<uint8_t*>(mmap(nullptr, 1 << 20, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS | MAP_32BIT, -1, 0));
        if (g_arena == MAP_FAILED) { std::printf("mmap failed\n"); std::exit(2); }
    }
    g_used = (g_used + 63) & ~size_t(63);
    uint8_t* p = g_arena + g_used; g_used += n; std::memset(p, 0, n); return p;
}
static uint32_t P32(const void* p) { uintptr_t v = reinterpret_cast<uintptr_t>(p); if (v >> 32) { std::printf("pointer above 4GB\n"); std::exit(2); } return static_cast<uint32_t>(v); }

struct Assoc { const char* name; bool present; uint8_t* mem; };
struct World {
    uint8_t *ped, *pedMat, *clump, *hier, *mats, *nodeInfo, *anim;
    uint8_t *player, *playerMat, *playersArr; uint8_t focus = 0;
    volatile uint32_t timeMs = 100000;
    bool conv = false;            // fake game's quaternion convention: false = R(q), true = R(q)^T
    int frame = 0;
    bool ragOverride = false;
    Q ragQ[B_COUNT];
    std::vector<Assoc> assocs;
} W;

struct Pose { Q q[B_COUNT]; V3 t[B_COUNT]; };

static V3 kRest[B_COUNT];
static void InitRest() {
    auto s = [](Bone b, float x, float y, float z) { kRest[b] = {x, y, z}; };
    s(B_ROOT, 0, 0, 0); s(B_PELVIS, 0, 0, 0.95f); s(B_SPINE, 0, 0, 0.10f); s(B_SPINE1, 0, 0, 0.15f);
    s(B_NECK, 0, 0, 0.25f); s(B_HEAD, 0, 0, 0.08f); s(B_LBROW, -0.04f, 0.09f, 0.06f); s(B_RBROW, 0.04f, 0.09f, 0.06f); s(B_JAW, 0, 0.05f, -0.05f);
    s(B_RCLAV, 0.05f, 0, 0.20f); s(B_RUPARM, 0.14f, 0, 0); s(B_RFORE, 0, 0, -0.28f); s(B_RHAND, 0, 0, -0.25f); s(B_RFINGERS, 0, 0, -0.07f); s(B_RFINGER1, 0, 0, -0.04f);
    s(B_LCLAV, -0.05f, 0, 0.20f); s(B_LUPARM, -0.14f, 0, 0); s(B_LFORE, 0, 0, -0.28f); s(B_LHAND, 0, 0, -0.25f); s(B_LFINGERS, 0, 0, -0.07f); s(B_LFINGER1, 0, 0, -0.04f);
    s(B_LTHIGH, -0.09f, 0, -0.05f); s(B_LCALF, 0, 0, -0.42f); s(B_LFOOT, 0, 0, -0.42f); s(B_LTOE, 0, 0.12f, -0.05f);
    s(B_RTHIGH, 0.09f, 0, -0.05f); s(B_RCALF, 0, 0, -0.42f); s(B_RFOOT, 0, 0, -0.42f); s(B_RTOE, 0, 0.12f, -0.05f);
}
static Q QAA(V3 axis, float deg) { return M3ToQuat(RotationVector(Normalized(axis, {1, 0, 0}) * Deg2Rad(deg))); }
static Q QMul(const Q& a, const Q& b) { return M3ToQuat(Mul(QuatToM3(a), QuatToM3(b))); }

static Pose AnimPose(int f) {
    Pose p;
    for (int b = 0; b < B_COUNT; ++b) { p.q[b] = Q(); p.t[b] = kRest[b]; }
    p.q[B_SPINE] = QAA({1, 0, 0}, 14);  p.q[B_SPINE1] = QAA({1, 0, 0}, -14);
    p.q[B_NECK] = QAA({1, 0, 0}, 12);   p.q[B_HEAD] = QAA({1, 0, 0}, -12);
    p.q[B_RUPARM] = QAA({0, 1, 0}, 18); p.q[B_RFORE] = QAA({0, 1, 0}, -18);
    p.q[B_LUPARM] = QAA({0, 1, 0}, -18); p.q[B_LFORE] = QAA({0, 1, 0}, 18);
    p.q[B_RTHIGH] = QAA({1, 0, 0}, 16); p.q[B_RCALF] = QAA({1, 0, 0}, -16);
    p.q[B_LTHIGH] = QAA({1, 0, 0}, -16); p.q[B_LCALF] = QAA({1, 0, 0}, 16);
    const float w = f * 0.07f;   // gentle wobble so the animation "plays" every frame like the real thing
    p.q[B_SPINE1] = QMul(p.q[B_SPINE1], QAA({0, 0, 1}, 4 * std::sin(w)));
    p.q[B_RUPARM] = QMul(p.q[B_RUPARM], QAA({1, 0, 0}, 5 * std::sin(w + 1)));
    p.q[B_LUPARM] = QMul(p.q[B_LUPARM], QAA({1, 0, 0}, 5 * std::sin(w + 2)));
    return p;
}

static M3 PedRot() { return RotationVector({0, 0, Deg2Rad(35.f)}); }
static V3 PedPos() { return {100.f, 200.f, 10.f}; }

static void FK(const Pose& p, bool conv, V3* outPos, M3* outRot) {
    M3 Rw[B_COUNT];
    for (int b = 0; b < B_COUNT; ++b) {
        M3 Rl = QuatToM3(p.q[b]); if (conv) Rl = Transposed(Rl);
        const int par = kBones[b].parent;
        M3 Rp = par >= 0 ? Rw[par] : PedRot();
        V3 pp = par >= 0 ? outPos[par] : PedPos();
        Rw[b] = Mul(Rp, Rl);
        outPos[b] = pp + Mul(Rp, p.t[b]);
        if (outRot) outRot[b] = Rw[b];
    }
}

static void WriteMat(uint8_t* base, int node, const M3& R, V3 pos) {
    RwMat m = {};
    m.rx = R.m[0][0]; m.ry = R.m[1][0]; m.rz = R.m[2][0];
    m.ux = R.m[0][1]; m.uy = R.m[1][1]; m.uz = R.m[2][1];
    m.ax = R.m[0][2]; m.ay = R.m[1][2]; m.az = R.m[2][2];
    m.px = pos.x; m.py = pos.y; m.pz = pos.z;
    Wr<RwMat>(base, node * sizeof(RwMat), m);
}
static uint8_t* FramePtr(int node) { return W.anim + 0x44 + node * 0x24 + 8; }
static void WriteFramesRaw(const Pose& p) { for (int b = 0; b < B_COUNT; ++b) { Wr<Q>(FramePtr(b), 0, p.q[b]); Wr<V3>(FramePtr(b), 16, p.t[b]); } }
static Pose ReadFrames() { Pose p; for (int b = 0; b < B_COUNT; ++b) { p.q[b] = Rd<Q>(FramePtr(b), 0); p.t[b] = Rd<V3>(FramePtr(b), 16); } return p; }
static void SetFlagDontUpdate(bool v) { uint32_t f = Rd<uint32_t>(W.ped, 0x1C); f = v ? (f | game::off::kFlagDontUpdateHierarchy) : (f & ~game::off::kFlagDontUpdateHierarchy); Wr<uint32_t>(W.ped, 0x1C, f); }
static bool FlagDontUpdate() { return (Rd<uint32_t>(W.ped, 0x1C) & game::off::kFlagDontUpdateHierarchy) != 0; }

static void GameUpdateMatrices() {   // CEntity::UpdateRpHAnim equivalent
    if (FlagDontUpdate()) return;
    Pose p = ReadFrames();
    V3 pos[B_COUNT]; M3 rot[B_COUNT];
    FK(p, W.conv, pos, rot);
    for (int b = 0; b < B_COUNT; ++b) WriteMat(W.mats, b, rot[b], pos[b]);
    SetFlagDontUpdate(true);
}
static void RagdollOverride() {
    Pose p; for (int b = 0; b < B_COUNT; ++b) { p.q[b] = W.ragQ[b]; p.t[b] = kRest[b]; }
    V3 pos[B_COUNT]; M3 rot[B_COUNT];
    FK(p, W.conv, pos, rot);
    for (int b = 0; b < B_COUNT; ++b) WriteMat(W.mats, b, rot[b], pos[b]);
}
static void SetAssoc(const char* name, bool present, float amount = 1.f) {
    for (auto& a : W.assocs) if (!std::strcmp(a.name, name)) { a.present = present; Wr<float>(a.mem, 0x14, amount); Wr<float>(a.mem, 0x18, 0.f); return; }
}
static void* FakeGetHier(void* clump) { return clump == W.clump ? W.hier : nullptr; }
static void* FakeGetAssoc(void* clump, const char* name) {
    if (clump != W.clump) return nullptr;
    for (auto& a : W.assocs) if (a.present && !std::strcmp(a.name, name)) return a.mem;
    return nullptr;
}

static void BuildWorld(bool conv) {
    g_used = 0; W.assocs.clear(); W.conv = conv; W.frame = 0; W.timeMs = 100000; W.ragOverride = false;
    W.ped = Alloc(0x7A4); W.pedMat = Alloc(0x50); W.clump = Alloc(0x40); W.hier = Alloc(0x40);
    W.mats = Alloc(B_COUNT * 64); W.nodeInfo = Alloc(B_COUNT * 16); W.anim = Alloc(0x44 + B_COUNT * 0x24 + 16);
    W.player = Alloc(0x7A4); W.playerMat = Alloc(0x50); W.playersArr = Alloc(0x190 * 2);
    M3 R = PedRot(); V3 pp = PedPos();
    WriteMat(W.pedMat, 0, R, pp);
    Wr<uint32_t>(W.ped, 0x14, P32(W.pedMat)); Wr<uint32_t>(W.ped, 0x18, P32(W.clump));
    Wr<uint8_t>(W.ped, 0x36, 3); Wr<float>(W.ped, 0x540, 100.f); Wr<int32_t>(W.ped, 0x534, 1);
    WriteMat(W.playerMat, 0, M3Identity(), {105.f, 205.f, 10.f});
    Wr<uint32_t>(W.player, 0x14, P32(W.playerMat)); Wr<uint8_t>(W.player, 0x36, 3); Wr<float>(W.player, 0x540, 100.f);
    Wr<uint32_t>(W.playersArr, 0, P32(W.player));
    Wr<int32_t>(W.hier, 4, B_COUNT); Wr<uint32_t>(W.hier, 8, P32(W.mats)); Wr<uint32_t>(W.hier, 0x10, P32(W.nodeInfo)); Wr<uint32_t>(W.hier, 0x20, P32(W.anim));
    Wr<int32_t>(W.anim, 0x24, 0x24);
    for (int b = 0; b < B_COUNT; ++b) Wr<int32_t>(W.nodeInfo, b * 16, kBones[b].id);
    static const char* names[] = {"HIT_FRONT", "DAM_ARML_FRM", "SHOT_PARTIAL", "GETUP", "GETUP_FRONT", "FALL_FRONT"};
    for (const char* n : names) W.assocs.push_back({n, false, Alloc(0x40)});
    game::g_api.getAnimHierarchy = &FakeGetHier;
    game::g_api.getAssociation = &FakeGetAssoc;
    game::g_api.timeMs = &W.timeMs;
    game::g_api.players = W.playersArr;
    game::g_api.playerFocus = &W.focus;
    InitRest();
}

static uint32_t PedPtr() { return P32(W.ped); }

// One game frame: animation update -> mod hook (PreUpdate) -> engine matrix update -> optional ragdoll override -> PostUpdate.
static void Frame(int ms = 16, bool callTwice = false) {
    W.timeMs += ms; ++W.frame;
    WriteFramesRaw(AnimPose(W.frame));
    SetFlagDontUpdate(false);
    bool post = react::PreUpdate(reinterpret_cast<uintptr_t>(W.ped));
    if (callTwice) react::PreUpdate(reinterpret_cast<uintptr_t>(W.ped));
    GameUpdateMatrices();
    if (W.ragOverride) RagdollOverride();
    if (post) react::PostUpdate(reinterpret_cast<uintptr_t>(W.ped));
}

static Config TestCfg() {
    Config c;
    c.randomness = 0.f; c.logging = false; c.knockback = 0.f; c.hitDelayMs = 34;
    c.tripChanceLight = c.tripChanceMedium = c.tripChanceHeavy = 1.f;
    c.suppressWindowMs = 900;
    return c;
}
static void Reset(bool conv, const Config& c) {
    BuildWorld(conv); CalibReset(); react::Init(c);
}
static bool Calibrate() { for (int i = 0; i < 40 && !Calib().done && !Calib().failed; ++i) Frame(); return Calib().done; }

static V3 RefPos(Bone b, int frame) { V3 pos[B_COUNT]; FK(AnimPose(frame), W.conv, pos, nullptr); return pos[b]; }
static V3 WorldPos(Bone b) { RwMat m = Rd<RwMat>(W.mats, b * 64); return {m.px, m.py, m.pz}; }

struct Shot { float w[8]; };
static void Fire(int weapon, V3 hitPos, V3 dir) {
    uint8_t* weap = Alloc(0x1C); Wr<int32_t>(weap, 0, weapon);
    float start[3] = {hitPos.x - dir.x * 10, hitPos.y - dir.y * 10, hitPos.z - dir.z * 10};
    float end[3] = {hitPos.x, hitPos.y, hitPos.z};
    float hit[3] = {hitPos.x, hitPos.y, hitPos.z};
    react::OnBullet(weap, W.ped, start, end, hit);
}

// Fires at `target` (world point on the reference pose) and returns the max displacement of `watch` along dir over 0.6 s.
static float PeakAlong(Bone watch, V3 dir, V3 target, int weapon, float* worstAway = nullptr) {
    float peak = 0.f, away = 0.f;
    Fire(weapon, target, dir);
    for (int i = 0; i < 36; ++i) {
        Frame();
        V3 disp = WorldPos(watch) - RefPos(watch, W.frame);
        float a = Dot(disp, dir);
        peak = std::max(peak, a); away = std::min(away, a);
    }
    if (worstAway) *worstAway = away;
    return peak;
}
static V3 Mid(Bone a, Bone b, int frame) { return (RefPos(a, frame) + RefPos(b, frame)) * 0.5f; }

// ---------------------------------------------------------------------------------------------------- tests
static void TestMath() {
    std::printf("[math]\n");
    for (int i = 0; i < 200; ++i) {
        V3 ax = Normalized({std::sin(i * 1.7f), std::cos(i * 0.9f), std::sin(i * 0.3f) + 0.2f}, {1, 0, 0});
        Q q = QAA(ax, 3.f + i * 0.85f);
        M3 m = QuatToM3(q);
        Q q2 = M3ToQuat(m);
        CHECK(std::fabs(std::fabs(Dot(q, q2)) - 1.f) < 1e-5f, "quat roundtrip %d", i);
    }
    Q a = QAA({0, 0, 1}, 10), b = QAA({0, 0, 1}, 50);
    CHECK(std::fabs(AngleBetween(Slerp(a, b, 0.5f), a) - Deg2Rad(20)) < 1e-4f, "slerp midpoint");
    V3 v = Mul(RotationVector(V3{0, 0, Deg2Rad(90)}), V3{1, 0, 0});
    CHECK(std::fabs(v.x) < 1e-5f && std::fabs(v.y - 1.f) < 1e-5f, "rotation vector sign (x -> y about +z)");
}

static void TestCalibration() {
    std::printf("[calibration]\n");
    for (int conv = 0; conv < 2; ++conv) {
        Reset(conv != 0, TestCfg());
        bool ok = Calibrate();
        CHECK(ok, "calibration finished (conv=%d)", conv);
        CHECK(Calib().transposed == (conv != 0), "convention detected (conv=%d got %d)", conv, Calib().transposed);
        int usable = 0; for (int b = 1; b < B_COUNT; ++b) if (Calib().boneOk[b]) ++usable;
        CHECK(usable >= 26, "bones usable %d", usable);
        CHECK(Calib().rootOk && Calib().transOk[0] && Calib().transOk[1], "root + translations validated");
    }
    // Corrupted skeleton: matrices do not follow the key frames -> must fail safe.
    Reset(false, TestCfg());
    for (int i = 0; i < 120 && !Calib().failed; ++i) {
        W.timeMs += 16; ++W.frame;
        WriteFramesRaw(AnimPose(W.frame)); SetFlagDontUpdate(false);
        bool post = react::PreUpdate(reinterpret_cast<uintptr_t>(W.ped));
        for (int b = 0; b < B_COUNT; ++b) { M3 r = RotationVector({0.3f * b, 0.5f, 0.2f * i}); WriteMat(W.mats, b, r, PedPos() + V3{0, 0, 0.5f}); }
        SetFlagDontUpdate(true);
        if (post) react::PostUpdate(reinterpret_cast<uintptr_t>(W.ped));
    }
    CHECK(Calib().failed && !Calib().done, "garbage skeleton => calibration fails safe");
    Pose before = ReadFrames();
    react::Init(TestCfg());
    Fire(30, RefPos(B_RHAND, W.frame), {0, 1, 0});
    for (int i = 0; i < 20; ++i) Frame();
    Pose after = ReadFrames(); Pose expect = AnimPose(W.frame);
    bool same = true; for (int b = 0; b < B_COUNT; ++b) if (!NearlyEqual(after.q[b], expect.q[b], 1e-5f)) same = false;
    CHECK(same, "no pose writes while calibration failed");
    (void)before;
}

static void TestHitDirections() {
    std::printf("[hit reactions]\n");
    for (int conv = 0; conv < 2; ++conv) {
        Reset(conv != 0, TestCfg());
        Calibrate();
        for (int i = 0; i < 6; ++i) Frame();
        struct Case { const char* name; Bone a, b, watch; V3 dir; int weapon; float minPeak; };
        const V3 dirs[] = {{-1, 0, 0}, {1, 0, 0}, {0, 1, 0}, {0, -1, 0}};
        const Case cases[] = {
            {"right forearm from +x", B_RFORE, B_RHAND, B_RHAND, {-1, 0, 0}, 22, 0.03f},
            {"left forearm from -x",  B_LFORE, B_LHAND, B_LHAND, {1, 0, 0}, 30, 0.03f},
            {"right hand from front", B_RHAND, B_RFINGERS, B_RFINGERS, {0, -1, 0}, 24, 0.03f},
            {"torso from the front",  B_SPINE, B_SPINE1, B_HEAD, {0, -1, 0}, 31, 0.03f},
            {"torso from the right",  B_SPINE, B_SPINE1, B_HEAD, {-1, 0, 0}, 25, 0.04f},
            {"head from the left",    B_NECK, B_HEAD, B_HEAD, {1, 0, 0}, 34, 0.02f},
            {"right calf from front", B_RCALF, B_RFOOT, B_RFOOT, {0, -1, 0}, 30, 0.03f},
            {"left calf from the left", B_LCALF, B_LFOOT, B_LFOOT, {1, 0, 0}, 25, 0.03f},
        };
        (void)dirs;
        for (const Case& c : cases) {
            Reset(conv != 0, TestCfg()); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
            // directions are given in ped-local terms; rotate into the world by the ped's heading
            V3 d = Normalized(Mul(PedRot(), c.dir), {0, 1, 0});
            V3 target = Mid(c.a, c.b, W.frame + 3);
            float away = 0.f;
            float peak = PeakAlong(c.watch, d, target, c.weapon, &away);
            CHECK(peak > c.minPeak, "conv=%d %-24s peak=%.3f m (need > %.3f)", conv, c.name, static_cast<double>(peak), static_cast<double>(c.minPeak));
        }
    }
    // Contextual: a hand hit must move the shot arm much more than the far leg.
    Reset(false, TestCfg()); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
    V3 d = Normalized(Mul(PedRot(), V3{-1, 0, 0}), {0, 1, 0});
    V3 target = Mid(B_RFORE, B_RHAND, W.frame + 3);
    Fire(22, target, d);
    float handMove = 0.f, legMove = 0.f;
    for (int i = 0; i < 36; ++i) {
        Frame();
        handMove = std::max(handMove, Length(WorldPos(B_RHAND) - RefPos(B_RHAND, W.frame)));
        legMove = std::max(legMove, Length(WorldPos(B_LFOOT) - RefPos(B_LFOOT, W.frame)));
    }
    CHECK(handMove > 0.08f && handMove > 2.5f * legMove, "hand hit: hand moved %.3f m vs far foot %.3f m", static_cast<double>(handMove), static_cast<double>(legMove));
}

static void TestWeaponScaling() {
    std::printf("[weapon scaling]\n");
    float peaks[3] = {};
    const int weapons[3] = {22, 30, 25};   // light pistol, assault rifle, shotgun
    for (int i = 0; i < 3; ++i) {
        Reset(false, TestCfg()); Calibrate(); for (int k = 0; k < 6; ++k) Frame();
        V3 d = Normalized(Mul(PedRot(), V3{-1, 0, 0}), {0, 1, 0});
        peaks[i] = PeakAlong(B_RHAND, d, Mid(B_RFORE, B_RHAND, W.frame + 3), weapons[i]);
    }
    CHECK(peaks[0] < peaks[1] && peaks[1] < peaks[2], "light<medium<heavy: %.3f %.3f %.3f", static_cast<double>(peaks[0]), static_cast<double>(peaks[1]), static_cast<double>(peaks[2]));
    CHECK(peaks[0] > 0.02f, "light weapon still visible: %.3f", static_cast<double>(peaks[0]));
}

static void TestIdempotentAndSettling() {
    std::printf("[idempotency / settling / safety]\n");
    Reset(false, TestCfg()); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
    Fire(25, Mid(B_SPINE, B_SPINE1, W.frame + 2), Normalized(Mul(PedRot(), V3{0, -1, 0}), {0, 1, 0}));
    for (int i = 0; i < 5; ++i) Frame();
    // two PreUpdate calls in the same game tick must give the same pose as one
    W.timeMs += 16; ++W.frame; WriteFramesRaw(AnimPose(W.frame)); SetFlagDontUpdate(false);
    react::PreUpdate(reinterpret_cast<uintptr_t>(W.ped));
    Pose once = ReadFrames();
    react::PreUpdate(reinterpret_cast<uintptr_t>(W.ped));
    Pose twice = ReadFrames();
    bool same = true; for (int b = 0; b < B_COUNT; ++b) if (!NearlyEqual(once.q[b], twice.q[b], 1e-5f)) same = false;
    CHECK(same, "second PreUpdate in the same tick does not double-apply");
    // everything must settle back to the pure animation
    for (int i = 0; i < 400; ++i) Frame();
    Pose fin = ReadFrames(), anim = AnimPose(W.frame);
    float worst = 0.f; for (int b = 0; b < B_COUNT; ++b) worst = std::max(worst, AngleBetween(fin.q[b], anim.q[b]));
    CHECK(worst < Deg2Rad(0.5f), "reactions decay to nothing (residual %.3f deg)", static_cast<double>(Rad2Deg(worst)));
    // dead peds are left alone
    Wr<float>(W.ped, 0x540, 0.f);
    Fire(25, Mid(B_SPINE, B_SPINE1, W.frame + 2), {0, 1, 0});
    for (int i = 0; i < 10; ++i) Frame();
    fin = ReadFrames(); anim = AnimPose(W.frame);
    worst = 0.f; for (int b = 0; b < B_COUNT; ++b) worst = std::max(worst, AngleBetween(fin.q[b], anim.q[b]));
    CHECK(worst < Deg2Rad(0.01f), "dead ped untouched");
    Wr<float>(W.ped, 0x540, 100.f);
    // peds far from the player are skipped
    uint8_t* far = W.playerMat; WriteMat(far, 0, M3Identity(), {5000.f, 5000.f, 10.f});
    Fire(25, Mid(B_SPINE, B_SPINE1, W.frame + 2), {0, 1, 0});
    for (int i = 0; i < 10; ++i) Frame();
    fin = ReadFrames(); anim = AnimPose(W.frame);
    worst = 0.f; for (int b = 0; b < B_COUNT; ++b) worst = std::max(worst, AngleBetween(fin.q[b], anim.q[b]));
    CHECK(worst < Deg2Rad(0.01f), "distance gate");
    // the player is excluded unless enabled
    WriteMat(far, 0, M3Identity(), {105.f, 205.f, 10.f});
    uint8_t* weap = Alloc(0x1C); Wr<int32_t>(weap, 0, 30);
    float s[3] = {0, 0, 0}, e[3] = {0, 1, 0};
    uint32_t q0 = react::GetStats().hitsQueued;
    react::OnBullet(weap, W.player, s, e, e);
    CHECK(react::GetStats().hitsQueued == q0, "player excluded by default");
    // unknown weapons (fists, knives) do nothing
    Wr<int32_t>(weap, 0, 4);
    react::OnBullet(weap, W.ped, s, e, e);
    CHECK(react::GetStats().hitsQueued == q0, "melee weapon ignored");
}

static void TestStaleAndPellets() {
    std::printf("[stale updates / shotgun pellets]\n");
    for (int conv = 0; conv < 2; ++conv) {
        Reset(conv != 0, TestCfg());
        for (int i = 0; i < 120 && !Calib().done && !Calib().failed; ++i) {
            // every third frame the engine "forgets" to rebuild the matrices
            W.timeMs += 16; ++W.frame; WriteFramesRaw(AnimPose(W.frame)); SetFlagDontUpdate(false);
            bool post = react::PreUpdate(reinterpret_cast<uintptr_t>(W.ped));
            if (i % 3 != 0) GameUpdateMatrices(); else SetFlagDontUpdate(false);
            if (post) react::PostUpdate(reinterpret_cast<uintptr_t>(W.ped));
        }
        CHECK(Calib().done && Calib().transposed == (conv != 0), "calibration survives skipped updates (conv=%d done=%d failed=%d)", conv, Calib().done, Calib().failed);
    }
    Reset(false, TestCfg()); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
    V3 d = Normalized(Mul(PedRot(), V3{0, -1, 0}), {0, 1, 0});
    V3 target = Mid(B_SPINE, B_SPINE1, W.frame + 3);
    for (int k = 0; k < 9; ++k) Fire(25, target, d);   // 9 pellets in one tick
    float maxAngle = 0.f;
    for (int i = 0; i < 40; ++i) {
        Frame();
        Pose cur = ReadFrames(), a = AnimPose(W.frame);
        for (int b = 0; b < B_COUNT; ++b) maxAngle = std::max(maxAngle, AngleBetween(cur.q[b], a.q[b]));
    }
    CHECK(maxAngle < Deg2Rad(75.f), "pellet blast stays within the caps (max %.0f deg)", static_cast<double>(Rad2Deg(maxAngle)));
    CHECK(maxAngle > Deg2Rad(8.f), "pellet blast is clearly visible (max %.0f deg)", static_cast<double>(Rad2Deg(maxAngle)));
}

static void TestRebuiltModel() {
    std::printf("[model rebuilt under an active reaction]\n");
    Reset(false, TestCfg()); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
    Fire(25, Mid(B_SPINE, B_SPINE1, W.frame + 3), Normalized(Mul(PedRot(), V3{0, -1, 0}), {0, 1, 0}));
    for (int i = 0; i < 6; ++i) Frame();      // reaction is running and writing into W.anim
    // the engine throws the old hierarchy away and builds a new one for the same ped
    uint8_t* oldAnim = W.anim;
    std::vector<uint8_t> snapshot(oldAnim, oldAnim + 0x44 + B_COUNT * 0x24);
    uint8_t* newHier = Alloc(0x40); uint8_t* newAnim = Alloc(0x44 + B_COUNT * 0x24 + 16);
    std::memcpy(newHier, W.hier, 0x40); std::memcpy(newAnim, oldAnim, 0x44 + B_COUNT * 0x24);
    Wr<uint32_t>(newHier, 0x20, P32(newAnim));
    W.hier = newHier; W.anim = newAnim;
    for (int i = 0; i < 20; ++i) Frame();
    bool untouched = std::memcmp(snapshot.data(), oldAnim, snapshot.size()) == 0;
    CHECK(untouched, "stale hierarchy is never written to after the model changes");
    // and the mod recovers on the new hierarchy
    Fire(25, Mid(B_SPINE, B_SPINE1, W.frame + 3), Normalized(Mul(PedRot(), V3{0, -1, 0}), {0, 1, 0}));
    float peak = 0.f;
    for (int i = 0; i < 36; ++i) { Frame(); peak = std::max(peak, Length(WorldPos(B_HEAD) - RefPos(B_HEAD, W.frame))); }
    CHECK(peak > 0.05f, "reactions work again on the rebuilt model (peak %.3f m)", static_cast<double>(peak));
}

static void TestStaleHits() {
    std::printf("[hits queued before the mod is ready]\n");
    Reset(false, TestCfg());
    Frame(); Frame();   // skeleton not built / not calibrated yet
    Fire(25, Mid(B_SPINE, B_SPINE1, W.frame + 3), Normalized(Mul(PedRot(), V3{0, -1, 0}), {0, 1, 0}));
    W.timeMs += 1000;   // startup takes a while: the queued hit is now far too old to replay
    Calibrate();
    for (int i = 0; i < 40; ++i) Frame();
    CHECK(react::GetStats().hitsApplied == 0, "a hit older than 400 ms is discarded instead of replayed late (applied=%u)", react::GetStats().hitsApplied);
}

static void TestSuppression() {
    std::printf("[vanilla hit animation suppression]\n");
    Reset(false, TestCfg()); Calibrate(); for (int i = 0; i < 4; ++i) Frame();
    SetAssoc("HIT_FRONT", true, 1.0f); SetAssoc("DAM_ARML_FRM", true, 0.6f);
    Fire(22, Mid(B_SPINE, B_SPINE1, W.frame + 2), {0, 1, 0});
    Frame();
    CHECK(Rd<float>(W.assocs[0].mem, 0x14) == 0.f && Rd<float>(W.assocs[0].mem, 0x18) < 0.f, "HIT_FRONT faded out");
    CHECK(Rd<float>(W.assocs[1].mem, 0x14) == 0.f, "DAM_ARML_FRM faded out");
    // outside the window (e.g. melee) nothing is touched
    for (int i = 0; i < 80; ++i) Frame();
    SetAssoc("HIT_FRONT", true, 1.0f);
    Frame();
    CHECK(Rd<float>(W.assocs[0].mem, 0x14) == 1.0f, "no suppression outside the bullet window");
}

static void TestTrip() {
    std::printf("[trip + bridge handshake]\n");
    Config c = TestCfg();
    Reset(false, c); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
    Wr<int32_t>(W.ped, 0x534, 6);   // running
    CHECK(DHF_Mailbox.pedPtr == 0, "mailbox empty at start");
    Fire(25, Mid(B_RCALF, B_RFOOT, W.frame + 3), Normalized(Mul(PedRot(), V3{0, -1, 0}), {0, 1, 0}));
    for (int i = 0; i < 6; ++i) Frame();
    CHECK(react::GetStats().trips == 1, "running ped shot in the calf trips (trips=%u)", react::GetStats().trips);
    CHECK(DHF_Mailbox.pedPtr == PedPtr(), "trip request posted to the CLEO bridge");
    CHECK(DHF_Mailbox.timeMs == static_cast<uint32_t>(c.tripGroundTimeMs), "ground time forwarded");
    // emulate the script: consume + ack
    uint32_t seq = DHF_Mailbox.seq; DHF_Mailbox.pedPtr = 0; DHF_Mailbox.ack = seq;
    Frame();
    CHECK(react::GetStats().bridgeAcked == 1, "ack recognised");
    // cooldown: an immediate second hit does not retrigger
    Fire(25, Mid(B_RCALF, B_RFOOT, W.frame + 3), {0, 1, 0});
    for (int i = 0; i < 6; ++i) Frame();
    CHECK(react::GetStats().trips == 1, "cooldown respected");

    // walking / standing peds do not trip
    Reset(false, c); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
    Wr<int32_t>(W.ped, 0x534, 4);
    Fire(25, Mid(B_RCALF, B_RFOOT, W.frame + 3), {0, 1, 0});
    for (int i = 0; i < 6; ++i) Frame();
    CHECK(react::GetStats().trips == 0, "walking ped does not trip");
    // running but hit in the arm: no trip
    Wr<int32_t>(W.ped, 0x534, 7);
    Fire(25, Mid(B_RFORE, B_RHAND, W.frame + 3), {0, 1, 0});
    for (int i = 0; i < 6; ++i) Frame();
    CHECK(react::GetStats().trips == 0, "arm hit does not trip");

    // no script answering: request is withdrawn after the timeout, then the bridge is declared dead after 3 failures
    Reset(false, c); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
    Wr<int32_t>(W.ped, 0x534, 6);
    for (int k = 0; k < 4; ++k) {
        Fire(25, Mid(B_RCALF, B_RFOOT, W.frame + 3), {0, 1, 0});
        for (int i = 0; i < 70; ++i) Frame();     // > cooldown (4 s) not reached, so shorten cooldown below
        W.timeMs += 5000;                        // jump past the cooldown
        Frame();
    }
    CHECK(react::GetStats().bridgeTimeouts >= 3, "unanswered requests time out (%u)", react::GetStats().bridgeTimeouts);
    CHECK(DHF_Mailbox.pedPtr == 0, "unanswered request withdrawn");
}

static void TestGetUpBlend() {
    std::printf("[ragdoll -> get-up blend]\n");
    for (int conv = 0; conv < 2; ++conv) {
        Config c = TestCfg();
        Reset(conv != 0, c); Calibrate(); for (int i = 0; i < 6; ++i) Frame();
        // a violent, arbitrary ragdoll pose
        for (int b = 0; b < B_COUNT; ++b) W.ragQ[b] = QAA({std::sin(b * 2.1f) + 0.3f, std::cos(b * 1.3f), std::sin(b * 0.7f)}, 55.f + 20.f * std::sin(b * 1.9f));
        SetAssoc("FALL_FRONT", true);       // fall anim -> watch window opens
        for (int i = 0; i < 6; ++i) Frame();
        W.ragOverride = true;
        SetAssoc("FALL_FRONT", false); SetAssoc("GETUP", true);
        for (int i = 0; i < 20; ++i) Frame();      // ragdoll mod is in control, displayed pose = ragdoll
        W.ragOverride = false;                     // ... and lets go
        Pose anim = AnimPose(W.frame + 1);
        float gap0 = 0.f;
        W.timeMs += 16; ++W.frame; WriteFramesRaw(AnimPose(W.frame)); SetFlagDontUpdate(false);
        react::PreUpdate(reinterpret_cast<uintptr_t>(W.ped));
        Pose first = ReadFrames(); anim = AnimPose(W.frame);
        GameUpdateMatrices();
        float toRag = 0.f, toAnim = 0.f;
        for (Bone b : {B_SPINE1, B_HEAD, B_RUPARM, B_LTHIGH}) {
            toRag = std::max(toRag, AngleBetween(first.q[b], W.ragQ[b]));
            toAnim = std::max(toAnim, AngleBetween(first.q[b], anim.q[b]));
        }
        gap0 = toAnim;
        CHECK(toRag < toAnim, "conv=%d first released frame is still near the ragdoll pose (%.0f deg from ragdoll, %.0f from anim)", conv, static_cast<double>(Rad2Deg(toRag)), static_cast<double>(Rad2Deg(toAnim)));
        CHECK(react::GetStats().smoothEngaged >= 1, "blend engaged");
        // converge over time, monotonically
        float prev = gap0; bool mono = true; float gap = 0.f;
        for (int i = 0; i < 90; ++i) {
            Frame();
            Pose cur = ReadFrames(); Pose a = AnimPose(W.frame);
            gap = 0.f; for (Bone b : {B_SPINE1, B_HEAD, B_RUPARM, B_LTHIGH}) gap = std::max(gap, AngleBetween(cur.q[b], a.q[b]));
            if (gap > prev + Deg2Rad(2.f)) mono = false;
            prev = gap;
            if (i == 29) CHECK(gap < gap0 * 0.35f, "conv=%d after 0.5 s the gap shrank to %.0f deg (from %.0f)", conv, static_cast<double>(Rad2Deg(gap)), static_cast<double>(Rad2Deg(gap0)));
        }
        CHECK(mono, "blend converges monotonically");
        CHECK(gap < Deg2Rad(3.f), "fully back on the animation after 1.5 s (gap %.2f deg)", static_cast<double>(Rad2Deg(gap)));
    }
    // A normal animation must never be touched by the blend.
    Reset(false, TestCfg()); Calibrate();
    for (int i = 0; i < 120; ++i) Frame();
    Pose fin = ReadFrames(), a = AnimPose(W.frame);
    float worst = 0.f; for (int b = 0; b < B_COUNT; ++b) worst = std::max(worst, AngleBetween(fin.q[b], a.q[b]));
    CHECK(worst < Deg2Rad(0.01f), "ordinary animation untouched");
}

static void Report() {
    std::printf("peak displacement (cm) of the watched point along the bullet direction, and peak swing of hit bone (deg)\n");
    struct Row { const char* name; Bone a, b, watch; V3 dir; };
    const Row rows[] = {
        {"hand      (from side)", B_RHAND, B_RFINGERS, B_RFINGERS, {-1, 0, 0}},
        {"forearm   (from side)", B_RFORE, B_RHAND, B_RHAND, {-1, 0, 0}},
        {"upper arm (from side)", B_RUPARM, B_RFORE, B_RHAND, {-1, 0, 0}},
        {"chest     (front)",     B_SPINE, B_SPINE1, B_HEAD, {0, -1, 0}},
        {"head      (front)",     B_NECK, B_HEAD, B_HEAD, {0, -1, 0}},
        {"calf      (side)",      B_RCALF, B_RFOOT, B_RFOOT, {-1, 0, 0}},
        {"foot      (front)",     B_RCALF, B_RFOOT, B_RFOOT, {0, -1, 0}},
    };
    const int weapons[] = {22, 28, 24, 30, 25, 34};
    const char* wn[] = {"Colt45", "MicroSMG", "Deagle", "AK47", "Shotgun", "Sniper"};
    std::printf("%-24s", "");
    for (const char* n : wn) std::printf("%9s", n);
    std::printf("\n");
    for (const Row& r : rows) {
        std::printf("%-24s", r.name);
        for (int w : weapons) {
            Reset(false, TestCfg()); Calibrate(); for (int k = 0; k < 6; ++k) Frame();
            V3 d = Normalized(Mul(PedRot(), r.dir), {0, 1, 0});
            float peak = PeakAlong(r.watch, d, Mid(r.a, r.b, W.frame + 3), w);
            std::printf("%9.1f", static_cast<double>(peak * 100.f));
        }
        std::printf("\n");
    }
}

int main(int argc, char** argv) {
    InitRest();
    if (argc > 1 && !std::strcmp(argv[1], "--report")) { Report(); return 0; }
    TestMath();
    TestCalibration();
    TestHitDirections();
    TestWeaponScaling();
    TestIdempotentAndSettling();
    TestStaleAndPellets();
    TestRebuiltModel();
    TestStaleHits();
    TestSuppression();
    TestTrip();
    TestGetUpBlend();
    std::printf("\n%d checks passed, %d failed\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
