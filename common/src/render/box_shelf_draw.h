#pragma once

// What the 3D shelf's own code (box_shelf.cpp) and each program's part of it
// (render/shelf_consoles.cpp in sega/ and nintendo/) both draw with: the shelf's measures,
// column-major 4x4 matrices as OpenGL expects them, turns as quaternions, and how a cover
// is split into its front, spine and back. Only for those files.

#include <algorithm>
#include <array>
#include <cmath>

#include "render/box_shelf.h"

namespace plum {
namespace shelf {

constexpr float kPi = 3.14159265f;
constexpr float kRad = kPi / 180.0f;

// The shelf's measures, made for a Mega Drive case: 13.5 x 19 x 2.5 cm, with 13.5 cm as 1.
// The boxes of each system have their own size (BoxShelf::boxSize).
constexpr float kBoxW = 1.0f, kBoxH = 1.4f, kBoxD = 0.2f, kWall = 0.02f;
constexpr float kFloorY = -kBoxH / 2;   // the boxes stand on the floor
constexpr float kConsoleZ = 1.6f;       // the console sits on the floor in front of them
constexpr float kFov = 34.0f;           // degrees, top to bottom
// The shelf behind: the boxes' middle, their spacing, and its boards.
constexpr float kShelfZ = -1.6f, kSpacing = kBoxD + 0.012f;
constexpr float kBoard = 0.045f, kPlinth = 0.09f, kSide = 0.05f, kRowH = kBoxH + 0.1f + kBoard;
const float kBackground[3] = {0.035f, 0.037f, 0.045f};

// Column-major 4x4 matrices, as OpenGL expects them.
using Mat4 = std::array<float, 16>;

inline Mat4 identity() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

inline Mat4 operator*(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; ++c)
        for (int row = 0; row < 4; ++row)
            for (int k = 0; k < 4; ++k) r[c * 4 + row] += a[k * 4 + row] * b[c * 4 + k];
    return r;
}

inline Mat4 translate(float x, float y, float z) { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, x, y, z, 1}; }
inline Mat4 scale(float x, float y, float z) { return {x, 0, 0, 0, 0, y, 0, 0, 0, 0, z, 0, 0, 0, 0, 1}; }

// Turns +z towards +x for positive degrees.
inline Mat4 rotateY(float degrees) {
    const float c = std::cos(degrees * kRad), s = std::sin(degrees * kRad);
    return {c, 0, -s, 0, 0, 1, 0, 0, s, 0, c, 0, 0, 0, 0, 1};
}

// Turns +z towards -y for positive degrees (so -90 lays a face looking at the viewer on its back).
inline Mat4 rotateX(float degrees) {
    const float c = std::cos(degrees * kRad), s = std::sin(degrees * kRad);
    return {1, 0, 0, 0, 0, c, s, 0, 0, -s, c, 0, 0, 0, 0, 1};
}

// Turns +x towards +y for positive degrees.
inline Mat4 rotateZ(float degrees) {
    const float c = std::cos(degrees * kRad), s = std::sin(degrees * kRad);
    return {c, s, 0, 0, -s, c, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1};
}

// Turns as unit quaternions (w, x, y, z), the same ways as rotateX and rotateY.
using Quat = std::array<float, 4>;
constexpr Quat kNoTurn{1, 0, 0, 0};

inline Quat operator*(const Quat& a, const Quat& b) {
    return {a[0] * b[0] - a[1] * b[1] - a[2] * b[2] - a[3] * b[3], a[0] * b[1] + a[1] * b[0] + a[2] * b[3] - a[3] * b[2],
            a[0] * b[2] - a[1] * b[3] + a[2] * b[0] + a[3] * b[1], a[0] * b[3] + a[1] * b[2] - a[2] * b[1] + a[3] * b[0]};
}

inline Quat normalized(const Quat& q) {
    const float n = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    return n > 0 ? Quat{q[0] / n, q[1] / n, q[2] / n, q[3] / n} : kNoTurn;
}

inline Quat quatX(float degrees) { return {std::cos(degrees * kRad / 2), std::sin(degrees * kRad / 2), 0, 0}; }
inline Quat quatY(float degrees) { return {std::cos(degrees * kRad / 2), 0, std::sin(degrees * kRad / 2), 0}; }
inline Quat quatZ(float degrees) { return {std::cos(degrees * kRad / 2), 0, 0, std::sin(degrees * kRad / 2)}; }

// From `a` (t = 0) to `b` (t = 1) the short way round.
inline Quat towards(const Quat& a, Quat b, float t) {
    if (a[0] * b[0] + a[1] * b[1] + a[2] * b[2] + a[3] * b[3] < 0) b = {-b[0], -b[1], -b[2], -b[3]};
    return normalized({a[0] + (b[0] - a[0]) * t, a[1] + (b[1] - a[1]) * t, a[2] + (b[2] - a[2]) * t, a[3] + (b[3] - a[3]) * t});
}

inline Mat4 rotation(const Quat& q) {
    const float w = q[0], x = q[1], y = q[2], z = q[3];
    return {1 - 2 * (y * y + z * z), 2 * (x * y + w * z),     2 * (x * z - w * y),     0,
            2 * (x * y - w * z),     1 - 2 * (x * x + z * z), 2 * (y * z + w * x),     0,
            2 * (x * z + w * y),     2 * (y * z - w * x),     1 - 2 * (x * x + y * y), 0,
            0,                       0,                       0,                       1};
}

inline Mat4 perspective(float fovY, float aspect, float zNear, float zFar) {
    const float f = 1.0f / std::tan(fovY / 2);
    Mat4 m{};
    m[0] = f / aspect;
    m[5] = f;
    m[10] = (zFar + zNear) / (zNear - zFar);
    m[11] = -1;
    m[14] = 2 * zFar * zNear / (zNear - zFar);
    return m;
}

// The inverse of a matrix that only turns, scales and moves.
inline Mat4 affineInverse(const Mat4& m) {
    const float a = m[0], b = m[4], c = m[8], d = m[1], e = m[5], f = m[9], g = m[2], h = m[6], i = m[10];
    const float A = e * i - f * h, B = -(d * i - f * g), C = d * h - e * g;
    const float det = a * A + b * B + c * C;
    if (std::fabs(det) < 1e-12f) return identity();
    const float k = 1.0f / det;
    // Rows of the inverse of the 3x3 part.
    const float r00 = A * k, r01 = -(b * i - c * h) * k, r02 = (b * f - c * e) * k;
    const float r10 = B * k, r11 = (a * i - c * g) * k, r12 = -(a * f - c * d) * k;
    const float r20 = C * k, r21 = -(a * h - b * g) * k, r22 = (a * e - b * d) * k;
    const float tx = m[12], ty = m[13], tz = m[14];
    return {r00, r10, r20, 0, r01, r11, r21, 0, r02, r12, r22, 0,
            -(r00 * tx + r01 * ty + r02 * tz), -(r10 * tx + r11 * ty + r12 * tz), -(r20 * tx + r21 * ty + r22 * tz), 1};
}

inline float smooth(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    return t * t * (3 - 2 * t);
}

inline float lerp(float a, float b, float t) { return a + (b - a) * t; }

// The front, spine and back of a cover as texture rectangles; the back and spine are
// empty (u1 <= u0) on a cover that is only the front.
struct Sleeve {
    std::array<float, 4> front{0, 0, 1, 1}, spine{0, 0, 0, 1}, back{0, 0, 0, 1};
};

inline Sleeve sleeve(int width, int height, ConsoleSystem system) {
    Sleeve s;
    const float f = BoxShelf::coverFront(width, height, system);
    if (f <= 0) return s;
    const float side = 1 - f;  // the front's width, and the back's
    s.front = {f, 0, 1, 1};
    s.back = {0, 0, side, 1};
    s.spine = {side, 0, f, 1};
    return s;
}

}  // namespace shelf
}  // namespace plum
