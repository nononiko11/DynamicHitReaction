// Math3.h - tiny vector / matrix / quaternion toolkit.
// Matrices are 3x3, column-vector convention: v' = M * v, columns = (right, up, at) as in RwMatrix.
#pragma once
#include "Platform.h"

namespace dhf {

constexpr float kPi = 3.14159265358979f;
inline float Deg2Rad(float d) { return d * (kPi / 180.0f); }
inline float Rad2Deg(float r) { return r * (180.0f / kPi); }
inline float Clampf(float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); }

struct V3 { float x = 0.f, y = 0.f, z = 0.f; };
inline V3 operator+(V3 a, V3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
inline V3 operator-(V3 a, V3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
inline V3 operator*(V3 a, float s) { return {a.x * s, a.y * s, a.z * s}; }
inline V3 operator-(V3 a) { return {-a.x, -a.y, -a.z}; }
inline V3& operator+=(V3& a, V3 b) { a.x += b.x; a.y += b.y; a.z += b.z; return a; }
inline float Dot(V3 a, V3 b) { return a.x * b.x + a.y * b.y + a.z * b.z; }
inline V3 Cross(V3 a, V3 b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
inline float Length(V3 a) { return std::sqrt(Dot(a, a)); }
inline bool Finite(V3 a) { return std::isfinite(a.x) && std::isfinite(a.y) && std::isfinite(a.z); }
inline V3 Normalized(V3 a, V3 fallback) {
    float l = Length(a);
    return (l > 1e-6f && std::isfinite(l)) ? a * (1.0f / l) : fallback;
}

struct M3 { float m[3][3]; };   // m[row][col]
inline M3 M3Identity() {
    M3 r;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = (i == j) ? 1.f : 0.f;
    return r;
}
inline V3 Mul(const M3& a, V3 v) {
    return {a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z,
            a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
            a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z};
}
inline M3 Mul(const M3& a, const M3& b) {
    M3 r;
    for (int i = 0; i < 3; ++i)
        for (int j = 0; j < 3; ++j)
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
    return r;
}
inline M3 Transposed(const M3& a) {
    M3 r;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) r.m[i][j] = a.m[j][i];
    return r;
}
inline M3 FromColumns(V3 c0, V3 c1, V3 c2) {
    M3 r;
    r.m[0][0] = c0.x; r.m[1][0] = c0.y; r.m[2][0] = c0.z;
    r.m[0][1] = c1.x; r.m[1][1] = c1.y; r.m[2][1] = c1.z;
    r.m[0][2] = c2.x; r.m[1][2] = c2.y; r.m[2][2] = c2.z;
    return r;
}
inline V3 Column(const M3& a, int c) { return {a.m[0][c], a.m[1][c], a.m[2][c]}; }
inline float FroDiff(const M3& a, const M3& b) {
    float s = 0.f;
    for (int i = 0; i < 3; ++i) for (int j = 0; j < 3; ++j) { float d = a.m[i][j] - b.m[i][j]; s += d * d; }
    return std::sqrt(s);
}
// Gram-Schmidt: removes scale/shear so rotation maths stays well behaved.
inline M3 Orthonormal(const M3& a) {
    V3 c0 = Normalized(Column(a, 0), {1.f, 0.f, 0.f});
    V3 c1 = Column(a, 1);
    c1 = c1 - c0 * Dot(c0, c1);
    c1 = Normalized(c1, {0.f, 1.f, 0.f});
    V3 c2 = Cross(c0, c1);
    return FromColumns(c0, c1, c2);
}
// Rotation vector (axis * angle, radians) -> matrix (Rodrigues).
inline M3 RotationVector(V3 rv) {
    float a = Length(rv);
    if (!(a > 1e-6f)) return M3Identity();
    V3 k = rv * (1.0f / a);
    float c = std::cos(a), s = std::sin(a), t = 1.f - c;
    M3 r;
    r.m[0][0] = t * k.x * k.x + c;       r.m[0][1] = t * k.x * k.y - s * k.z; r.m[0][2] = t * k.x * k.z + s * k.y;
    r.m[1][0] = t * k.x * k.y + s * k.z; r.m[1][1] = t * k.y * k.y + c;       r.m[1][2] = t * k.y * k.z - s * k.x;
    r.m[2][0] = t * k.x * k.z - s * k.y; r.m[2][1] = t * k.y * k.z + s * k.x; r.m[2][2] = t * k.z * k.z + c;
    return r;
}

struct Q { float x = 0.f, y = 0.f, z = 0.f, w = 1.f; };   // same member order as RtQuat (imag.xyz, real)
inline float Dot(const Q& a, const Q& b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }
inline Q Normalized(Q q) {
    float l = std::sqrt(Dot(q, q));
    if (!(l > 1e-8f) || !std::isfinite(l)) return Q();
    float i = 1.0f / l;
    return {q.x * i, q.y * i, q.z * i, q.w * i};
}
inline bool Finite(const Q& q) { return std::isfinite(q.x) && std::isfinite(q.y) && std::isfinite(q.z) && std::isfinite(q.w); }
inline bool NearlyEqual(const Q& a, const Q& b, float eps = 1e-6f) {
    return std::fabs(a.x - b.x) < eps && std::fabs(a.y - b.y) < eps && std::fabs(a.z - b.z) < eps && std::fabs(a.w - b.w) < eps;
}
// Standard (active) rotation matrix of a unit quaternion.
inline M3 QuatToM3(const Q& q) {
    float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
    float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
    float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
    M3 r;
    r.m[0][0] = 1.f - 2.f * (yy + zz); r.m[0][1] = 2.f * (xy - wz);       r.m[0][2] = 2.f * (xz + wy);
    r.m[1][0] = 2.f * (xy + wz);       r.m[1][1] = 1.f - 2.f * (xx + zz); r.m[1][2] = 2.f * (yz - wx);
    r.m[2][0] = 2.f * (xz - wy);       r.m[2][1] = 2.f * (yz + wx);       r.m[2][2] = 1.f - 2.f * (xx + yy);
    return r;
}
inline Q M3ToQuat(const M3& m) {
    Q q;
    float tr = m.m[0][0] + m.m[1][1] + m.m[2][2];
    if (tr > 0.f) {
        float s = std::sqrt(tr + 1.f) * 2.f;
        q.w = 0.25f * s;
        q.x = (m.m[2][1] - m.m[1][2]) / s;
        q.y = (m.m[0][2] - m.m[2][0]) / s;
        q.z = (m.m[1][0] - m.m[0][1]) / s;
    } else if (m.m[0][0] > m.m[1][1] && m.m[0][0] > m.m[2][2]) {
        float s = std::sqrt(1.f + m.m[0][0] - m.m[1][1] - m.m[2][2]) * 2.f;
        q.w = (m.m[2][1] - m.m[1][2]) / s;
        q.x = 0.25f * s;
        q.y = (m.m[0][1] + m.m[1][0]) / s;
        q.z = (m.m[0][2] + m.m[2][0]) / s;
    } else if (m.m[1][1] > m.m[2][2]) {
        float s = std::sqrt(1.f + m.m[1][1] - m.m[0][0] - m.m[2][2]) * 2.f;
        q.w = (m.m[0][2] - m.m[2][0]) / s;
        q.x = (m.m[0][1] + m.m[1][0]) / s;
        q.y = 0.25f * s;
        q.z = (m.m[1][2] + m.m[2][1]) / s;
    } else {
        float s = std::sqrt(1.f + m.m[2][2] - m.m[0][0] - m.m[1][1]) * 2.f;
        q.w = (m.m[1][0] - m.m[0][1]) / s;
        q.x = (m.m[0][2] + m.m[2][0]) / s;
        q.y = (m.m[1][2] + m.m[2][1]) / s;
        q.z = 0.25f * s;
    }
    return Normalized(q);
}
inline float AngleBetween(const Q& a, const Q& b) {
    float d = std::fabs(Dot(a, b));
    if (d > 1.f) d = 1.f;
    return 2.f * std::acos(d);
}
inline Q Slerp(const Q& a, Q b, float t) {
    float d = Dot(a, b);
    if (d < 0.f) { b = {-b.x, -b.y, -b.z, -b.w}; d = -d; }
    if (d > 0.9995f) {   // nearly parallel: lerp
        Q r = {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t, a.w + (b.w - a.w) * t};
        return Normalized(r);
    }
    float th = std::acos(d);
    float s = std::sin(th);
    float wa = std::sin((1.f - t) * th) / s, wb = std::sin(t * th) / s;
    return Normalized({a.x * wa + b.x * wb, a.y * wa + b.y * wb, a.z * wa + b.z * wb, a.w * wa + b.w * wb});
}

} // namespace dhf
