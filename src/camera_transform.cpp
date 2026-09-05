// SPDX-License-Identifier: MIT
// Copyright (c) 2026 itsloopyo

#include "camera_transform.h"

#include <cstring>

#include "cameraunlock/math/quat4.h"
#include "cameraunlock/math/vec3.h"

namespace wf2_ht {

// The engine's layout, and the one thing every function here depends on: a
// row-major 4x4 whose rows 0..2 are the camera's right, up and forward axes and
// whose row 3 is its world position, with (0,0,0,1) down the last column. The
// engine composes it as S*Rx*Ry*Rz*T and stores the translation in elements
// 12..14, which is what fixes the row-vector (v' = v * M) convention below.
constexpr int kDimension = 4;
constexpr int kRightRow = 0;
constexpr int kUpRow = 1;
constexpr int kForwardRow = 2;
constexpr int kTranslationRow = 3;
constexpr int kBasisRows = 3;
// The x/y/z part of a row, i.e. everything before the homogeneous column.
constexpr int kSpatialColumns = 3;

constexpr int Element(int row, int column) { return row * kDimension + column; }

// Left-multiplies the head rotation onto `base`, which rotates the camera about
// its own origin. `out` must not alias `base`.
//
// Row i of the head rotation's basis block is q.Rotate(e_i): for a row vector,
// v*H == sum_i v_i * q.Rotate(e_i) == q.Rotate(v), so the block is exactly the
// core's quaternion with no hand-rolled Euler matrix to drift out of sync with
// the pose the position processor was fed.
//
// The rest of that 4x4 is (0,0,0,1) down its last column and across its last
// row, so the product needs neither: each output basis row is a weighted sum of
// `base`'s three basis rows, and the translation row is `base`'s unchanged -
// which is what turns the view without the camera orbiting the car.
static void RotateBasis(const cameraunlock::math::Quat4& q,
                        const float base[kCameraTransformFloats],
                        float out[kCameraTransformFloats]) {
    using cameraunlock::math::Vec3;
    const Vec3 basis[kBasisRows] = {
        Vec3(1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f) };
    for (int r = 0; r < kBasisRows; ++r) {
        const Vec3 w = q.Rotate(basis[r]);
        for (int c = 0; c < kDimension; ++c) {
            out[Element(r, c)] = w.x * base[Element(kRightRow, c)]
                               + w.y * base[Element(kUpRow, c)]
                               + w.z * base[Element(kForwardRow, c)];
        }
    }
    for (int c = 0; c < kDimension; ++c) {
        out[Element(kTranslationRow, c)] = base[Element(kTranslationRow, c)];
    }
}

// Lean, carried through the CLEAN camera basis rather than the head-rotated
// one, so the offset follows the car's orientation and not where the head is
// pointing.
static void AddBodyFrameLean(const float clean[kCameraTransformFloats],
                             float dx, float dy, float dz,
                             float out[kCameraTransformFloats]) {
    for (int c = 0; c < kSpatialColumns; ++c) {
        out[Element(kTranslationRow, c)] += dx * clean[Element(kRightRow, c)]
                                          + dy * clean[Element(kUpRow, c)]
                                          + dz * clean[Element(kForwardRow, c)];
    }
}

void ApplyHeadPose(float transform[kCameraTransformFloats], const HeadPose& pose) {
    // The camera basis is left-handed X-right / Y-up / Z-forward (confirmed
    // from a live matrix: right x up == forward exactly), and three of the
    // pose's axes run opposite to it: the engine's pitch and its lateral axis
    // against OpenTrack's, and its forward axis against the position
    // processor's, whose convention is that NEGATIVE z is the forward lean.
    // Yaw matches and is passed through. All four directions were checked
    // against a running game on 2026-09-01, which is the only way a tracker
    // convention can be settled - the protocol never states one.
    //
    // Negating here, at the engine boundary, is the whole of the conversion:
    // the mod exposes no axis inversion of its own, because the tracker owns
    // pose shaping and one profile has to behave the same way in every game.
    // It has to happen after the processor's clamp, not before: the z limits
    // are deliberately asymmetric (0.40m of forward lean, 0.10m back), so
    // negating any earlier would hand the generous range to leaning backwards.
    const float engineYaw = pose.yaw;
    const float enginePitch = -pose.pitch;
    const float engineLeanX = -pose.lean_x;
    const float engineLeanZ = -pose.lean_z;

    // A copy, because RotateBasis writes its result over the same storage it
    // would otherwise be reading its input from, and because the lean needs the
    // basis as the engine computed it.
    float clean[kCameraTransformFloats];
    std::memcpy(clean, transform, sizeof(clean));

    // All three axes through the one quaternion, in the YXZ order the core
    // composes, left-multiplied onto the camera basis - which is what makes
    // every one of them camera-local.
    const cameraunlock::math::Quat4 q =
        cameraunlock::math::Quat4::FromYawPitchRoll(engineYaw, enginePitch, pose.roll);

    RotateBasis(q, clean, transform);
    AddBodyFrameLean(clean, engineLeanX, pose.lean_y, engineLeanZ, transform);
}

}  // namespace wf2_ht
