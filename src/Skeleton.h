// Skeleton.h - access to a ped's skinned bone hierarchy (RpHAnimHierarchy) and calibration of
// rotation conventions. Everything here is validated at runtime; if the data does not look the way
// we expect, the feature switches itself off instead of touching memory blindly.
#pragma once
#include "Platform.h"
#include "Math3.h"

namespace dhf {

enum Bone : int {
    B_ROOT = 0, B_PELVIS, B_SPINE, B_SPINE1, B_NECK, B_HEAD, B_LBROW, B_RBROW, B_JAW,
    B_RCLAV, B_RUPARM, B_RFORE, B_RHAND, B_RFINGERS, B_RFINGER1,
    B_LCLAV, B_LUPARM, B_LFORE, B_LHAND, B_LFINGERS, B_LFINGER1,
    B_LTHIGH, B_LCALF, B_LFOOT, B_LTOE, B_RTHIGH, B_RCALF, B_RFOOT, B_RTOE,
    B_COUNT
};

struct BoneDef { int id; int parent; const char* name; };
extern const BoneDef kBones[B_COUNT];

// 64-byte RenderWare matrix (rows right/up/at/pos).
struct RwMat {
    float rx, ry, rz; uint32_t f0;
    float ux, uy, uz; uint32_t f1;
    float ax, ay, az; uint32_t f2;
    float px, py, pz; uint32_t f3;
};
static_assert(sizeof(RwMat) == 64, "RwMat must be 64 bytes");

struct Skel {
    void* clump = nullptr;
    uint8_t* hier = nullptr;     // RpHAnimHierarchy*
    uint8_t* mats = nullptr;     // world-space bone matrices (RwMat[numNodes])
    uint8_t* interp = nullptr;   // interpolated key frame block (local rotation/translation per node)
    int numNodes = 0;
    int node[B_COUNT];           // node index of each tracked bone, -1 if missing
    bool valid = false;
    Skel() { for (int i = 0; i < B_COUNT; ++i) node[i] = -1; }
};

bool BuildSkel(void* clump, uintptr_t ped, Skel& out);
const char* SkelLastError();
// Re-reads the hierarchy from the clump and checks it is still the one `s` was built from.
bool SkelStillValid(const Skel& s);
inline bool Has(const Skel& s, Bone b) { return s.valid && s.node[b] >= 0; }

RwMat ReadMat(const Skel& s, Bone b);
V3 BonePos(const Skel& s, Bone b);
M3 BoneRot(const Skel& s, Bone b);          // orthonormalised world rotation (columns = right/up/at)
Q  ReadFrameQ(const Skel& s, Bone b);       // local rotation (parent space) that the animation system wrote
void WriteFrameQ(const Skel& s, Bone b, const Q& q);
V3 ReadFrameT(const Skel& s, Bone b);
void WriteFrameT(const Skel& s, Bone b, const V3& t);
bool ReadPedMatrix(uintptr_t ped, M3& rot, V3& pos);

// ---- calibration: figures out how RenderWare's quaternions map onto the bone matrices ----
struct Calibration {
    bool done = false;
    bool failed = false;
    bool transposed = false;     // false: local = R(q), true: local = R(q)^T
    bool rootOk = false;
    bool boneOk[B_COUNT];
    bool transOk[2];             // [0] root, [1] pelvis
    int frames = 0;              // consistent samples accepted
    int stale = 0;               // samples rejected because matrices did not match the key frames
    double e0[B_COUNT], e1[B_COUNT]; int n[B_COUNT];
    double rootE0 = 0, rootE1 = 0; int rootN = 0;
    double d0 = 0, d1 = 0; int nd = 0;
    double tErr[2]; int tN[2];
    Calibration();
};
const Calibration& Calib();
void CalibReset();
void CalibAccumulate(const Skel& sk, const M3& pedRot, const V3& pedPos);
M3 LocalFromQuat(const Q& q);
Q  QuatFromLocal(const M3& m);

} // namespace dhf
