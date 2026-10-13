// Compensated summation, double-precision cross product and Matrix4d identity.

#include <ludus/foundation/math/matrix.hpp>
#include <ludus/foundation/math/summation.hpp>

#include <catch2/catch_test_macros.hpp>

using namespace ludus::foundation::math;

TEST_CASE("CompensatedSum recovers terms lost by naive addition", "[math][summation]")
{
    CompensatedSum sum;
    sum.Add(1e16);
    sum.Add(1.0);
    sum.Add(-1e16);
    REQUIRE(sum.GetResult() == 1.0);
    REQUIRE(CompensatedSum{}.GetResult() == 0.0);
}

TEST_CASE("CompensatedDot matches Python sum of products", "[math][summation]")
{
    // sum([0.1 * 0.1, 0.2 * 0.2, 0.3 * 0.3]) in CPython 3.12.
    REQUIRE(CompensatedDot(Vector3d{0.1, 0.2, 0.3}, Vector3d{0.1, 0.2, 0.3}) == 0.14);
}

TEST_CASE("Vector3d cross is right-handed and Matrix4d identity compares equal", "[math][summation]")
{
    REQUIRE((Cross(Vector3d{1.0, 0.0, 0.0}, Vector3d{0.0, 1.0, 0.0}) == Vector3d{0.0, 0.0, 1.0}));
    Matrix4d matrix = Matrix4d::Identity();
    REQUIRE(matrix == Matrix4d::Identity());
    matrix.Elements[12] = 2.0;
    REQUIRE_FALSE(matrix == Matrix4d::Identity());
    REQUIRE(Matrix4d{}.Elements[0] == 0.0);
}
