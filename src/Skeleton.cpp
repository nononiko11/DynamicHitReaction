#include "Skeleton.h"
#include "GameSA.h"
#include "Log.h"
#include <algorithm>

namespace dhf {

// GTA SA ped skeleton: node ids and parents.
const BoneDef kBones[B_COUNT] = {
    {0,  -1,        "Root"},
    {1,  B_ROOT,    "Pelvis"},
    {2,  B_PELVIS,  "Spine"},
    {3,  B_SPINE,   "Spine1"},
    {4,  B_SPINE1,  "Neck"},
    {5,  B_NECK,    "Head"},
    {6,  B_HEAD,    "LBrow"},
    {7,  B_HEAD,    "RBrow"},
    {8,  B_HEAD,    "Jaw"},
    {21, B_SPINE1,  "RClavicle"},
    {22, B_RCLAV,   "RUpperArm"},
    {23, B_RUPARM,  "RForearm"},
    {24, B_RFORE,   "RHand"},
    {25, B_RHAND,   "RFingers"},
    {26, B_RFINGERS,"RFinger01"},
    {31, B_SPINE1,  "LClavicle"},
    {32, B_LCLAV,   "LUpperArm"},
    {33, B_LUPARM,  "LForearm"},
    {34, B_LFORE,   "LHand"},
    {35, B_LHAND,   "LFingers"},
    {36, B_LFINGERS,"LFinger01"},
    {41, B_PELVIS,  "LThigh"},
    {42, B_LTHIGH,  "LCalf"},
    {43, B_LCALF,   "LFoot"},
    {44, B_LFOOT,   "LToe"},
    {51, B_PELVIS,  "RThigh"},
    {52, B_RTHIGH,  "RCalf"},
    {53, B_RCALF,   "RFoot"},
    {54, B_RFOOT,   "RToe"},
};

namespace {

struct Layout { bool known = false; int hdr = 0, stride = 0, qOff = 0; } g_layout;
Calibration g_calib;
const char* g_skelError = "";
bool g_dumped = false;

void HexDump(const char* label, const uint8_t* p, size_t n) {
    char buf[640]; size_t o = 0;
    for (size_t i = 0; i < n && o + 6 < sizeof buf; ++i) o += static_cast<size_t>(std::snprintf(buf + o, sizeof buf - o, "%02X%s", p[i], (i % 4 == 3) ? " " : ""));
    log::Write("    %s: %s", label, buf);
}
void DumpOnce(const uint8_t* hier, const uint8_t* info, const uint8_t* anim) {
    if (g_dumped) return;
    g_dumped = true;
    log::Write("  diagnostic dump (please include this in a bug report):");
    HexDump("hierarchy[0x00..0x28]", hier, 0x28);
    if (info) HexDump("nodeInfo[0..3]", info, 0x30);
    if (anim) { HexDump("interpolator[0x00..0x40]", anim, 0x40); HexDump("interpolator[0x40..0x80]", anim + 0x40, 0x40); HexDump("interpolator[0x80..0xC0]", anim + 0x80, 0x40); }
}

const Bone kRequired[] = {B_PELVIS, B_SPINE, B_SPINE1, B_NECK, B_HEAD,
                          B_RUPARM, B_RFORE, B_RHAND, B_LUPARM, B_LFORE, B_LHAND,
                          B_LTHIGH, B_LCALF, B_LFOOT, B_RTHIGH, B_RCALF, B_RFOOT};

bool FramesLookValid(const uint8_t* anim, int numNodes, int hdr, int stride, int qOff) {
    for (int i = 0; i < numNodes; ++i) {
        const uint8_t* f = anim + hdr + static_cast<size_t>(i) * stride + qOff;
        float q[4], t[3];
        std::memcpy(q, f, 16);
        std::memcpy(t, f + 16, 12);
        for (int k = 0; k < 4; ++k) if (!std::isfinite(q[k])) return false;
        float l2 = q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3];
        if (l2 < 0.97f || l2 > 1.03f) return false;
        for (int k = 0; k < 3; ++k) if (!std::isfinite(t[k]) || std::fabs(t[k]) > 3.0f) return false;
    }
    return true;
}

// Probing may read past the end of an allocation when a candidate layout is wrong; that must read as "rejected",
// not as a fault of the plugin.
bool SafeFramesLookValid(const uint8_t* anim, int numNodes, int hdr, int stride, int qOff) {
#ifdef _MSC_VER
    __try { return FramesLookValid(anim, numNodes, hdr, stride, qOff); }
    __except (1 /* EXCEPTION_EXECUTE_HANDLER */) { return false; }
#else
    return FramesLookValid(anim, numNodes, hdr, stride, qOff);
#endif
}

bool EnsureLayout(const uint8_t* anim, int numNodes) {
    if (g_layout.known && SafeFramesLookValid(anim, numNodes, g_layout.hdr, g_layout.stride, g_layout.qOff)) return true;
    static const int kHdr[] = {0x44, 0x48, 0x40, 0x4C, 0x50, 0x54, 0x58, 0x3C, 0x38, 0x34, 0x30, 0x5C, 0x60};
    static const int kQ[] = {8, 4, 12, 0, 16};
    int strides[8]; int ns = 0;
    int s0 = Rd<int32_t>(anim, 0x24);   // RtAnimInterpolator::currentInterpKeyFrameSize
    if (s0 >= 0x18 && s0 <= 0x48 && (s0 % 4) == 0) strides[ns++] = s0;
    static const int kS[] = {0x24, 0x20, 0x28, 0x2C, 0x30};
    for (int s : kS) if (s != s0) strides[ns++] = s;
    for (int h : kHdr)
        for (int si = 0; si < ns; ++si)
            for (int q : kQ)
                if (SafeFramesLookValid(anim, numNodes, h, strides[si], q)) {
                    if (!g_layout.known || g_layout.hdr != h || g_layout.stride != strides[si] || g_layout.qOff != q)
                        log::Write("skeleton: interpolated frame layout hdr=0x%X stride=0x%X quat@+%d", h, strides[si], q);
                    g_layout.known = true; g_layout.hdr = h; g_layout.stride = strides[si]; g_layout.qOff = q;
                    return true;
                }
    return false;
}

inline const uint8_t* FramePtr(const Skel& s, Bone b) {
    return s.interp + g_layout.hdr + static_cast<size_t>(s.node[b]) * g_layout.stride + g_layout.qOff;
}
inline uint8_t* FramePtrW(const Skel& s, Bone b) {
    return s.interp + g_layout.hdr + static_cast<size_t>(s.node[b]) * g_layout.stride + g_layout.qOff;
}

void Decide() {
    Calibration& c = g_calib;
    if (c.nd < 24 || c.frames < 8) {
        if (c.stale >= 60 && c.frames < 8) {
            c.failed = true;
            log::Write("calibration FAILED: bone matrices never matched the animation key frames (%d inconsistent samples, %d good). "
                       "Reactions, trip and get-up blending are disabled.", c.stale, c.frames);
        } else if (c.frames > 800) {
            c.failed = true;
            log::Write("calibration FAILED: not enough informative samples (frames=%d nd=%d)", c.frames, c.nd);
        }
        return;
    }
    const bool tr = c.d1 < c.d0;
    const double best = (tr ? c.d1 : c.d0) / c.nd;
    const double other = (tr ? c.d0 : c.d1) / c.nd;
    if (best > 0.08 || other < 0.25) {
        c.failed = true;
        log::Write("calibration FAILED: bone matrices do not match the animation quaternions (best=%.4f other=%.4f). "
                   "Reactions, trip and get-up blending are disabled.", best, other);
        return;
    }
    c.transposed = tr;
    int ok = 0;
    for (int b = 1; b < B_COUNT; ++b) {
        if (c.n[b] >= 4) {
            double avg = (tr ? c.e1[b] : c.e0[b]) / c.n[b];
            c.boneOk[b] = avg < 0.06;
            if (c.boneOk[b]) ++ok;
        }
    }
    if (c.rootN >= 4) c.rootOk = ((tr ? c.rootE1 : c.rootE0) / c.rootN) < 0.06;
    for (int i = 0; i < 2; ++i) c.transOk[i] = c.tN[i] >= 4 && (c.tErr[i] / c.tN[i]) < 0.03;
    c.done = true;
    log::Write("calibration OK: quaternion convention=%s, bones usable=%d/%d, root=%s, rootT=%s, pelvisT=%s (err %.4f/%.4f)",
               tr ? "transposed" : "standard", ok, B_COUNT - 1, c.rootOk ? "yes" : "no",
               c.transOk[0] ? "yes" : "no", c.transOk[1] ? "yes" : "no", best, other);
}

} // namespace

Calibration::Calibration() {
    for (int i = 0; i < B_COUNT; ++i) { boneOk[i] = false; e0[i] = e1[i] = 0; n[i] = 0; }
    transOk[0] = transOk[1] = false;
    tErr[0] = tErr[1] = 0; tN[0] = tN[1] = 0;
}
const Calibration& Calib() { return g_calib; }
void CalibReset() { g_calib = Calibration(); g_layout = Layout(); }

M3 LocalFromQuat(const Q& q) {
    M3 r = QuatToM3(q);
    return g_calib.transposed ? Transposed(r) : r;
}
Q QuatFromLocal(const M3& m) {
    return M3ToQuat(g_calib.transposed ? Transposed(m) : m);
}

bool ReadPedMatrix(uintptr_t ped, M3& rot, V3& pos) {
    const void* m = RdPtr(reinterpret_cast<const void*>(ped), game::off::kMatrix);
    if (!m) return false;
    RwMat r = Rd<RwMat>(m, 0);
    V3 p = {r.px, r.py, r.pz};
    if (!Finite(p)) return false;
    rot = Orthonormal(FromColumns({r.rx, r.ry, r.rz}, {r.ux, r.uy, r.uz}, {r.ax, r.ay, r.az}));
    pos = p;
    return true;
}

bool BuildSkel(void* clump, uintptr_t ped, Skel& out) {
    out = Skel();
    g_skelError = "";
    if (!clump || !game::g_api.getAnimHierarchy) { g_skelError = "no clump / hierarchy getter"; return false; }
    uint8_t* hier = static_cast<uint8_t*>(game::g_api.getAnimHierarchy(clump));
    if (!hier) { g_skelError = "clump has no skinned hierarchy"; return false; }
    int numNodes = Rd<int32_t>(hier, 4);
    if (numNodes < 12 || numNodes > 96) { g_skelError = "hierarchy node count out of range"; DumpOnce(hier, nullptr, nullptr); return false; }
    uint8_t* mats = static_cast<uint8_t*>(RdPtr(hier, 0x08));
    uint8_t* info = static_cast<uint8_t*>(RdPtr(hier, 0x10));
    uint8_t* anim = static_cast<uint8_t*>(RdPtr(hier, 0x20));
    if (!mats || !info || !anim || (reinterpret_cast<uintptr_t>(mats) & 3u)) { g_skelError = "hierarchy pointers (matrices / node info / interpolator) invalid"; DumpOnce(hier, info, anim); return false; }
    for (int i = 0; i < numNodes; ++i) {
        int id = Rd<int32_t>(info, static_cast<size_t>(i) * 16);   // RpHAnimNodeInfo::nodeID
        for (int b = 0; b < B_COUNT; ++b) if (kBones[b].id == id) out.node[b] = i;
    }
    for (Bone b : kRequired) if (out.node[b] < 0) { out = Skel(); g_skelError = "model is missing standard ped bones"; return false; }
    if (!EnsureLayout(anim, numNodes)) { out = Skel(); g_skelError = "interpolated key frame layout not recognised (frames not initialised yet, or layout differs)"; DumpOnce(hier, info, anim); return false; }
    out.clump = clump; out.hier = hier; out.mats = mats; out.interp = anim; out.numNodes = numNodes; out.valid = true;
    M3 pr; V3 pp;
    if (ReadPedMatrix(ped, pr, pp)) {
        V3 d = BonePos(out, B_PELVIS) - pp;
        if (!(Length(d) < 3.0f)) { out = Skel(); g_skelError = "pelvis matrix is far from the ped (matrices not in world space, or not updated yet)"; return false; }
    }
    return true;
}

const char* SkelLastError() { return g_skelError; }

bool SkelStillValid(const Skel& s) {
    if (!s.valid || !game::g_api.getAnimHierarchy) return false;
    uint8_t* hier = static_cast<uint8_t*>(game::g_api.getAnimHierarchy(s.clump));
    if (hier != s.hier) return false;
    return Rd<int32_t>(hier, 4) == s.numNodes && RdPtr(hier, 0x08) == s.mats && RdPtr(hier, 0x20) == s.interp;
}

RwMat ReadMat(const Skel& s, Bone b) { return Rd<RwMat>(s.mats, static_cast<size_t>(s.node[b]) * sizeof(RwMat)); }
V3 BonePos(const Skel& s, Bone b) { RwMat m = ReadMat(s, b); return {m.px, m.py, m.pz}; }
M3 BoneRot(const Skel& s, Bone b) {
    RwMat m = ReadMat(s, b);
    return Orthonormal(FromColumns({m.rx, m.ry, m.rz}, {m.ux, m.uy, m.uz}, {m.ax, m.ay, m.az}));
}
Q ReadFrameQ(const Skel& s, Bone b) { return Rd<Q>(FramePtr(s, b), 0); }
void WriteFrameQ(const Skel& s, Bone b, const Q& q) { Wr<Q>(FramePtrW(s, b), 0, q); }
V3 ReadFrameT(const Skel& s, Bone b) { return Rd<V3>(FramePtr(s, b), 16); }
void WriteFrameT(const Skel& s, Bone b, const V3& t) { Wr<V3>(FramePtrW(s, b), 16, t); }

void CalibAccumulate(const Skel& sk, const M3& pedRot, const V3& pedPos) {
    Calibration& c = g_calib;
    if (c.done || c.failed || !sk.valid) return;
    M3 rot[B_COUNT];
    bool have[B_COUNT];
    for (int b = 0; b < B_COUNT; ++b) { have[b] = Has(sk, static_cast<Bone>(b)); if (have[b]) rot[b] = BoneRot(sk, static_cast<Bone>(b)); }

    // Gather this frame's errors first; a frame whose matrices do not match its key frames (stale update,
    // another mod overriding the pose, ...) is discarded as a whole instead of poisoning the statistics.
    double fe0[B_COUNT], fe1[B_COUNT], fd0 = 0, fd1 = 0, sumMin = 0, ferrT[2] = {0, 0};
    bool fcnt[B_COUNT], ftN[2] = {false, false};
    int fnd = 0, counted = 0;
    for (int b = 0; b < B_COUNT; ++b) { fe0[b] = fe1[b] = 0; fcnt[b] = false; }
    for (int b = 0; b < B_COUNT; ++b) {
        if (!have[b]) continue;
        const int p = kBones[b].parent;
        if (p >= 0 && !have[p]) continue;
        Q q = ReadFrameQ(sk, static_cast<Bone>(b));
        if (!Finite(q)) continue;
        float l2 = Dot(q, q);
        if (l2 < 0.97f || l2 > 1.03f) continue;
        q = Normalized(q);
        M3 Rq = QuatToM3(q), RqT = Transposed(Rq);
        M3 Pw = (p >= 0) ? rot[p] : pedRot;
        M3 expected = Mul(Transposed(Pw), rot[b]);
        const double e0 = FroDiff(expected, Rq), e1 = FroDiff(expected, RqT);
        fe0[b] = e0; fe1[b] = e1; fcnt[b] = true;
        sumMin += (e0 < e1 ? e0 : e1);
        ++counted;
        if (p >= 0 && FroDiff(Rq, RqT) > 0.5f) { fd0 += e0; fd1 += e1; ++fnd; }
        if (b == B_ROOT || b == B_PELVIS) {
            const int idx = (b == B_ROOT) ? 0 : 1;
            V3 posP = (p >= 0) ? BonePos(sk, static_cast<Bone>(p)) : pedPos;
            V3 texp = Mul(Transposed(Pw), BonePos(sk, static_cast<Bone>(b)) - posP);
            V3 tact = ReadFrameT(sk, static_cast<Bone>(b));
            if (Finite(tact)) { ferrT[idx] = Length(texp - tact); ftN[idx] = true; }
        }
    }
    if (counted < 8 || sumMin / counted > 0.12) { ++c.stale; Decide(); return; }

    for (int b = 0; b < B_COUNT; ++b) {
        if (!fcnt[b]) continue;
        if (kBones[b].parent >= 0) { c.e0[b] += fe0[b]; c.e1[b] += fe1[b]; c.n[b]++; }
        else { c.rootE0 += fe0[b]; c.rootE1 += fe1[b]; c.rootN++; }
    }
    c.d0 += fd0; c.d1 += fd1; c.nd += fnd;
    for (int i = 0; i < 2; ++i) if (ftN[i]) { c.tErr[i] += ferrT[i]; c.tN[i]++; }
    c.frames++;
    Decide();
}

} // namespace dhf
