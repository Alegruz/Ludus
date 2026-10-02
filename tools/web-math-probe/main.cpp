// Browser/Node numerical corpus for FoundationMath. Native unit tests prove CPU
// correctness; this probe re-runs a representative corpus through the SAME public
// API compiled for the pinned Emscripten target, so the browser's wasm math is
// actually exercised rather than assumed. It prints a single machine-checkable
// summary line the verify script asserts on.
//
// This is NOT a GPU test and NOT a renderer: it validates CPU numerical meaning
// on the web toolchain. GPU transfer convention is verified separately by the
// WebGPU shader fixture (M5).

#include <ludus/foundation/math/batch.hpp>
#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/precision.hpp>
#include <ludus/foundation/math/projection.hpp>
#include <ludus/foundation/math/quaternion.hpp>
#include <ludus/foundation/math/queries.hpp>
#include <ludus/foundation/math/random.hpp>
#include <ludus/foundation/math/transform.hpp>

#include <cmath>
#include <cstdio>

using namespace ludus::foundation::math;

namespace
{
int gFail = 0;

void Check(bool condition, const char* label)
{
    if (!condition)
    {
        std::printf("FAIL: %s\n", label);
        ++gFail;
    }
}

bool CloseF(float a, float b, float t)
{
    return std::fabs(a - b) <= t;
}
} // namespace

int main()
{
    // Cross and rotation.
    const Vector3 z = Cross(Vector3{1, 0, 0}, Vector3{0, 1, 0});
    Check(z.X == 0.0f && z.Y == 0.0f && z.Z == 1.0f, "cross X,Y=Z");

    Quaternion q{};
    Check(TryFromAxisAngle(Vector3{0, 0, 1}, kHalfPiF, q) == MathStatus::Success, "axis-angle");
    const Vector3 r = Rotate(q, Vector3{1, 0, 0});
    Check(CloseF(r.X, 0.0f, 1e-5f) && CloseF(r.Y, 1.0f, 1e-5f), "rotate X->Y");
    const Vector3 rm = ToMatrix3(q) * Vector3{1, 0, 0};
    Check(CloseF(rm.X, r.X, 1e-5f) && CloseF(rm.Y, r.Y, 1e-5f), "rotate==matrix action");

    // Normalisation of extremes.
    Vector3 u{};
    Check(TryNormalize(Vector3{1e30f, 2e30f, 2e30f}, u) == MathStatus::Success, "normalize huge");
    Check(CloseF(Length(u), 1.0f, 2e-6f), "huge unit length");

    // Inverse round-trip.
    Matrix4 m = Matrix4::Identity();
    m.Columns[3] = Vector4{3, 4, 5, 1};
    Matrix4 inv{};
    Check(TryInverse(m, inv) == MathStatus::Success, "inverse");
    const Vector4 back = inv * (m * Vector4{1, 2, 3, 1});
    Check(CloseF(back.X, 1.0f, 1e-4f) && CloseF(back.Z, 3.0f, 1e-4f), "inverse round-trip");

    // Reverse-Z depth endpoints.
    Matrix4 p{};
    Check(TryPerspectiveReverseZ(DegreesToRadians(60.0f), 1.0f, 0.1f, 100.0f, p) == MathStatus::Success, "proj");
    {
        Vector3 ndc{};
        (void)TryPerspectiveDivide(p * Vector4{0, 0, -0.1f, 1}, 0.0f, ndc);
        Check(CloseF(ndc.Z, 1.0f, 1e-4f), "near depth 1");
        (void)TryPerspectiveDivide(p * Vector4{0, 0, -100.0f, 1}, 0.0f, ndc);
        Check(CloseF(ndc.Z, 0.0f, 1e-4f), "far depth 0");
    }

    // Large coordinate precision.
    Vector3 rel{};
    Check(TryMakeRelative(Vector3d{1000000000.5, 0, 0}, Vector3d{1000000000.0, 0, 0}, 10.0, rel) == MathStatus::Success,
          "relative");
    Check(CloseF(rel.X, 0.5f, 1e-4f), "relative preserves 0.5");

    // PCG known answers (bit-exact contract across targets).
    {
        RandomStream rng;
        (void)rng.TryReseed(42, 54);
        const uint32 expected[6] = {0xa15c02b7u, 0x7b47f409u, 0xba1d3330u, 0x83d2f293u, 0xbfa4784bu, 0xcbed606eu};
        bool ok = true;
        for (uint32 e : expected)
        {
            ok = ok && (rng.NextUInt32() == e);
        }
        Check(ok, "PCG known answers");
    }

    std::printf("LUDUS_WEB_MATH_RESULT failures=%d\n", gFail);
    return gFail == 0 ? 0 : 1;
}
