#include <Ludus/Engine/Core/UnitTest.hpp>
#include <Ludus/Engine/Core/Math/Vector.hpp>
#include <Ludus/Engine/Core/Math/Matrix.hpp>
#include <Ludus/Engine/Core/Math/Quaternion.hpp>
#include <Ludus/Engine/Core/Math/Trigonometry.hpp>
#include <Ludus/Engine/Core/Math/Rect.hpp>
#include <Ludus/Engine/Core/Math/Interpolation.hpp>
#include <Ludus/Engine/Core/Math/Integration.hpp>
#include <Ludus/Engine/Core/Math/Bit.hpp>
#include <Ludus/Engine/Core/CommandLineManager.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>
#include <Ludus/Engine/Core/Logger.h>

using namespace ludus::core;

namespace
{
	struct CapturedLog final
	{
		LogLevel Level = LogLevel::Off;
		String Category;
		String Text;
		uint32_t Count = 0;
	};

	void CaptureLogSink(const LogMessage& message, void* userData) noexcept
	{
		auto* captured = static_cast<CapturedLog*>(userData);
		if (captured == nullptr)
		{
			return;
		}

		captured->Level = message.Level;
		captured->Category = message.Category != nullptr ? String(message.Category) : String("");
		captured->Text = message.Text != nullptr ? String(message.Text) : String("");
		++captured->Count;
	}
} // namespace

// Suppress magic number warnings for test values - tests should have explicit, readable values
// NOLINTBEGIN(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)

// ========================================
// Vector2 Tests
// ========================================

LUDUS_TEST(Vector2_Construction)
{
	Vector2<float> v1;
	LUDUS_TEST_ASSERT_EQ(v1.X, 0.0f);
	LUDUS_TEST_ASSERT_EQ(v1.Y, 0.0f);

	Vector2<float> v2(3.0f, 4.0f);
	LUDUS_TEST_ASSERT_EQ(v2.X, 3.0f);
	LUDUS_TEST_ASSERT_EQ(v2.Y, 4.0f);
}

LUDUS_TEST(Vector2_Addition)
{
	Vector2<float> v1(1.0f, 2.0f);
	Vector2<float> v2(3.0f, 4.0f);
	Vector2<float> result = v1 + v2;

	LUDUS_TEST_ASSERT_EQ(result.X, 4.0f);
	LUDUS_TEST_ASSERT_EQ(result.Y, 6.0f);
}

LUDUS_TEST(Vector2_Subtraction)
{
	Vector2<float> v1(5.0f, 7.0f);
	Vector2<float> v2(2.0f, 3.0f);
	Vector2<float> result = v1 - v2;

	LUDUS_TEST_ASSERT_EQ(result.X, 3.0f);
	LUDUS_TEST_ASSERT_EQ(result.Y, 4.0f);
}

LUDUS_TEST(Vector2_ScalarMultiplication)
{
	Vector2<float> v(2.0f, 3.0f);
	Vector2<float> result = v * 2.0f;

	LUDUS_TEST_ASSERT_EQ(result.X, 4.0f);
	LUDUS_TEST_ASSERT_EQ(result.Y, 6.0f);
}

LUDUS_TEST(Vector2_DotProduct)
{
	Vector2<float> v1(1.0f, 0.0f);
	Vector2<float> v2(0.0f, 1.0f);
	float dot = v1.Dot(v2);

	LUDUS_TEST_ASSERT_EQ(dot, 0.0f);

	Vector2<float> v3(3.0f, 4.0f);
	Vector2<float> v4(1.0f, 2.0f);
	float dot2 = v3.Dot(v4);

	LUDUS_TEST_ASSERT_EQ(dot2, 11.0f); // 3*1 + 4*2 = 11
}

LUDUS_TEST(Vector2_Length)
{
	Vector2<float> v(3.0f, 4.0f);
	float length = v.Length();

	LUDUS_TEST_ASSERT_NEAR(length, 5.0f, 0.0001f);
}

LUDUS_TEST(Vector2_LengthSquared)
{
	Vector2<float> v(3.0f, 4.0f);
	float lengthSq = v.LengthSquared();

	LUDUS_TEST_ASSERT_EQ(lengthSq, 25.0f);
}

LUDUS_TEST(Vector2_Normalize)
{
	Vector2<float> v(3.0f, 4.0f);
	Vector2<float> normalized = v.Normalized();

	LUDUS_TEST_ASSERT_NEAR(normalized.X, 0.6f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(normalized.Y, 0.8f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(normalized.Length(), 1.0f, 0.0001f);
}

LUDUS_TEST(Vector2_NormalizeZero)
{
	Vector2<float> v;
	Vector2<float> normalized = v.Normalized();

	LUDUS_TEST_ASSERT_EQ(normalized.X, 0.0f);
	LUDUS_TEST_ASSERT_EQ(normalized.Y, 0.0f);
}

LUDUS_TEST(Vector2_CompoundOps)
{
	Vector2<float> v(1.0f, 2.0f);
	v += Vector2<float>(3.0f, 4.0f);
	LUDUS_TEST_ASSERT_EQ(v.X, 4.0f);
	LUDUS_TEST_ASSERT_EQ(v.Y, 6.0f);

	v -= Vector2<float>(1.0f, 1.0f);
	LUDUS_TEST_ASSERT_EQ(v.X, 3.0f);
	LUDUS_TEST_ASSERT_EQ(v.Y, 5.0f);

	v *= Vector2<float>(2.0f, 3.0f);
	LUDUS_TEST_ASSERT_EQ(v.X, 6.0f);
	LUDUS_TEST_ASSERT_EQ(v.Y, 15.0f);

	v /= Vector2<float>(2.0f, 5.0f);
	LUDUS_TEST_ASSERT_EQ(v.X, 3.0f);
	LUDUS_TEST_ASSERT_EQ(v.Y, 3.0f);
}

// ========================================
// Vector3 Tests
// ========================================

LUDUS_TEST(Vector3_Construction)
{
	Vector3<float> v1;
	LUDUS_TEST_ASSERT_EQ(v1.X, 0.0f);
	LUDUS_TEST_ASSERT_EQ(v1.Y, 0.0f);
	LUDUS_TEST_ASSERT_EQ(v1.Z, 0.0f);

	Vector3<float> v2(1.0f, 2.0f, 3.0f);
	LUDUS_TEST_ASSERT_EQ(v2.X, 1.0f);
	LUDUS_TEST_ASSERT_EQ(v2.Y, 2.0f);
	LUDUS_TEST_ASSERT_EQ(v2.Z, 3.0f);
}

LUDUS_TEST(Vector3_Addition)
{
	Vector3<float> v1(1.0f, 2.0f, 3.0f);
	Vector3<float> v2(4.0f, 5.0f, 6.0f);
	Vector3<float> result = v1 + v2;

	LUDUS_TEST_ASSERT_EQ(result.X, 5.0f);
	LUDUS_TEST_ASSERT_EQ(result.Y, 7.0f);
	LUDUS_TEST_ASSERT_EQ(result.Z, 9.0f);
}

LUDUS_TEST(Vector3_CrossProduct)
{
	Vector3<float> v1(1.0f, 0.0f, 0.0f);
	Vector3<float> v2(0.0f, 1.0f, 0.0f);
	Vector3<float> cross = v1.Cross(v2);

	LUDUS_TEST_ASSERT_EQ(cross.X, 0.0f);
	LUDUS_TEST_ASSERT_EQ(cross.Y, 0.0f);
	LUDUS_TEST_ASSERT_EQ(cross.Z, 1.0f);
}

LUDUS_TEST(Vector3_LengthAndNormalizeZero)
{
	Vector3<float> v(1.0f, 2.0f, 2.0f);
	LUDUS_TEST_ASSERT_NEAR(v.Length(), 3.0f, 0.0001f);

	Vector3<float> zero;
	Vector3<float> normalized = zero.Normalized();
	LUDUS_TEST_ASSERT_EQ(normalized.X, 0.0f);
	LUDUS_TEST_ASSERT_EQ(normalized.Y, 0.0f);
	LUDUS_TEST_ASSERT_EQ(normalized.Z, 0.0f);
}

// ========================================
// Vector4 Tests
// ========================================

LUDUS_TEST(Vector4_BasicOps)
{
	Vector4<float> v1(1.0f, 2.0f, 3.0f, 4.0f);
	Vector4<float> v2(4.0f, 3.0f, 2.0f, 1.0f);

	Vector4<float> sum = v1 + v2;
	LUDUS_TEST_ASSERT_EQ(sum.X, 5.0f);
	LUDUS_TEST_ASSERT_EQ(sum.Y, 5.0f);
	LUDUS_TEST_ASSERT_EQ(sum.Z, 5.0f);
	LUDUS_TEST_ASSERT_EQ(sum.W, 5.0f);

	Vector4<float> scaled = 2.0f * v1;
	LUDUS_TEST_ASSERT_EQ(scaled.X, 2.0f);
	LUDUS_TEST_ASSERT_EQ(scaled.Y, 4.0f);
	LUDUS_TEST_ASSERT_EQ(scaled.Z, 6.0f);
	LUDUS_TEST_ASSERT_EQ(scaled.W, 8.0f);
}

LUDUS_TEST(Vector4_DotLength)
{
	Vector4<float> v(1.0f, 2.0f, 2.0f, 1.0f);
	LUDUS_TEST_ASSERT_EQ(v.Dot(v), 10.0f);
	LUDUS_TEST_ASSERT_NEAR(v.Length(), 3.1622776f, 0.0001f);
	LUDUS_TEST_ASSERT_EQ(v.LengthSquared(), 10.0f);
}

// ========================================
// Matrix3 Tests
// ========================================

LUDUS_TEST(Matrix3_IdentityVector)
{
	const Matrix3<float> identity = Matrix3<float>::Identity();
	const Vector3<float> v(1.0f, 2.0f, 3.0f);
	const Vector3<float> result = identity * v;

	LUDUS_TEST_ASSERT_EQ(result.X, 1.0f);
	LUDUS_TEST_ASSERT_EQ(result.Y, 2.0f);
	LUDUS_TEST_ASSERT_EQ(result.Z, 3.0f);
}

LUDUS_TEST(Matrix3_Multiplication)
{
	const Matrix3<float> a(
		1.0f, 2.0f, 3.0f,
		4.0f, 5.0f, 6.0f,
		7.0f, 8.0f, 9.0f);
	const Matrix3<float> b(
		9.0f, 8.0f, 7.0f,
		6.0f, 5.0f, 4.0f,
		3.0f, 2.0f, 1.0f);
	const Matrix3<float> result = a * b;

	LUDUS_TEST_ASSERT_EQ(result.M00, 30.0f);
	LUDUS_TEST_ASSERT_EQ(result.M01, 24.0f);
	LUDUS_TEST_ASSERT_EQ(result.M02, 18.0f);
	LUDUS_TEST_ASSERT_EQ(result.M10, 84.0f);
	LUDUS_TEST_ASSERT_EQ(result.M11, 69.0f);
	LUDUS_TEST_ASSERT_EQ(result.M12, 54.0f);
	LUDUS_TEST_ASSERT_EQ(result.M20, 138.0f);
	LUDUS_TEST_ASSERT_EQ(result.M21, 114.0f);
	LUDUS_TEST_ASSERT_EQ(result.M22, 90.0f);
}

LUDUS_TEST(Matrix3_Transpose)
{
	const Matrix3<float> m(
		1.0f, 2.0f, 3.0f,
		4.0f, 5.0f, 6.0f,
		7.0f, 8.0f, 9.0f);
	const Matrix3<float> t = m.Transposed();

	LUDUS_TEST_ASSERT_EQ(t.M00, 1.0f);
	LUDUS_TEST_ASSERT_EQ(t.M01, 4.0f);
	LUDUS_TEST_ASSERT_EQ(t.M02, 7.0f);
	LUDUS_TEST_ASSERT_EQ(t.M10, 2.0f);
	LUDUS_TEST_ASSERT_EQ(t.M11, 5.0f);
	LUDUS_TEST_ASSERT_EQ(t.M12, 8.0f);
	LUDUS_TEST_ASSERT_EQ(t.M20, 3.0f);
	LUDUS_TEST_ASSERT_EQ(t.M21, 6.0f);
	LUDUS_TEST_ASSERT_EQ(t.M22, 9.0f);
}

// ========================================
// Matrix4 Tests
// ========================================

LUDUS_TEST(Matrix4_TransformPointVector)
{
	Matrix4<float> m = Matrix4<float>::Identity();
	m.M03 = 10.0f;
	m.M13 = 20.0f;
	m.M23 = 30.0f;

	const Vector3<float> point(1.0f, 2.0f, 3.0f);
	const Vector3<float> vector(1.0f, 2.0f, 3.0f);

	const Vector3<float> transformedPoint = m.TransformPoint(point);
	const Vector3<float> transformedVector = m.TransformVector(vector);

	LUDUS_TEST_ASSERT_EQ(transformedPoint.X, 11.0f);
	LUDUS_TEST_ASSERT_EQ(transformedPoint.Y, 22.0f);
	LUDUS_TEST_ASSERT_EQ(transformedPoint.Z, 33.0f);

	LUDUS_TEST_ASSERT_EQ(transformedVector.X, 1.0f);
	LUDUS_TEST_ASSERT_EQ(transformedVector.Y, 2.0f);
	LUDUS_TEST_ASSERT_EQ(transformedVector.Z, 3.0f);
}

// ========================================
// Quaternion Tests
// ========================================

LUDUS_TEST(Quaternion_IdentityRotate)
{
	const Quaternion<float> q = Quaternion<float>::Identity();
	const Vector3<float> v(1.0f, 2.0f, 3.0f);
	const Vector3<float> result = q.Rotate(v);

	LUDUS_TEST_ASSERT_EQ(result.X, 1.0f);
	LUDUS_TEST_ASSERT_EQ(result.Y, 2.0f);
	LUDUS_TEST_ASSERT_EQ(result.Z, 3.0f);
}

LUDUS_TEST(Quaternion_AxisAngleRotate)
{
	const Vector3<float> axis(0.0f, 0.0f, 1.0f);
	const float angle = Pi<float>() * 0.5f;
	const Quaternion<float> q = Quaternion<float>::FromAxisAngle(axis, angle);

	const Vector3<float> v(1.0f, 0.0f, 0.0f);
	const Vector3<float> result = q.Rotate(v);

	LUDUS_TEST_ASSERT_NEAR(result.X, 0.0f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(result.Y, 1.0f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(result.Z, 0.0f, 0.0001f);
}

LUDUS_TEST(Quaternion_Inverse)
{
	const Vector3<float> axis(0.0f, 1.0f, 0.0f);
	const float angle = Pi<float>() * 0.25f;
	const Quaternion<float> q = Quaternion<float>::FromAxisAngle(axis, angle);
	const Quaternion<float> inv = q.Inverse();
	const Quaternion<float> identity = q * inv;

	LUDUS_TEST_ASSERT_NEAR(identity.X, 0.0f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(identity.Y, 0.0f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(identity.Z, 0.0f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(identity.W, 1.0f, 0.0001f);
}

LUDUS_TEST(Quaternion_MatrixRoundTrip)
{
	const Vector3<float> axis(1.0f, 0.0f, 0.0f);
	const float angle = Pi<float>() * 0.75f;
	const Quaternion<float> q = Quaternion<float>::FromAxisAngle(axis, angle);
	const Matrix3<float> matrix = q.ToRotationMatrix();
	Quaternion<float> q2 = Quaternion<float>::FromRotationMatrix(matrix);

	if (q.Dot(q2) < 0.0f)
	{
		q2 = -q2;
	}

	LUDUS_TEST_ASSERT_NEAR(q.X, q2.X, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(q.Y, q2.Y, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(q.Z, q2.Z, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(q.W, q2.W, 0.0001f);
}

LUDUS_TEST(Quaternion_NlerpShortestPath)
{
	const Vector3<float> axis(0.0f, 0.0f, 1.0f);
	const float angle = Pi<float>() * 0.5f;
	const Quaternion<float> q = Quaternion<float>::FromAxisAngle(axis, angle);
	const Quaternion<float> qNeg = -q;
	const Quaternion<float> blended = Quaternion<float>::Nlerp(q, qNeg, 0.5f);

	LUDUS_TEST_ASSERT_NEAR(blended.X, q.X, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(blended.Y, q.Y, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(blended.Z, q.Z, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(blended.W, q.W, 0.0001f);
}

// ========================================
// Trigonometry Tests
// ========================================

LUDUS_TEST(Trigonometry_Pi)
{
	float pi = Pi<float>();
	LUDUS_TEST_ASSERT_NEAR(pi, 3.14159265f, 0.00001f);
}

LUDUS_TEST(Trigonometry_Sin)
{
	float angle = Pi<float>() / 6.0f; // 30 degrees
	float sine = Sin(angle);
	LUDUS_TEST_ASSERT_NEAR(sine, 0.5f, 0.0001f);
}

LUDUS_TEST(Trigonometry_Cos)
{
	float angle = Pi<float>() / 3.0f; // 60 degrees
	float cosine = Cos(angle);
	LUDUS_TEST_ASSERT_NEAR(cosine, 0.5f, 0.0001f);
}

LUDUS_TEST(Trigonometry_SinCos)
{
	float angle = Pi<float>() / 4.0f; // 45 degrees
	SinCosPair<float> result = SinCos(angle);
	
	LUDUS_TEST_ASSERT_NEAR(result.Sin, 0.7071067f, 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(result.Cos, 0.7071067f, 0.0001f);
}

LUDUS_TEST(Trigonometry_WrapRanges)
{
	const float pi = Pi<float>();
	const float twoPi = TwoPi<float>();

	float result = WrapRadiansPi(3.0f * pi);
	LUDUS_TEST_ASSERT((std::abs(result - pi) < 0.0001f) || (std::abs(result + pi) < 0.0001f));

	result = WrapRadiansPi(-3.0f * pi);
	LUDUS_TEST_ASSERT((std::abs(result - pi) < 0.0001f) || (std::abs(result + pi) < 0.0001f));

	LUDUS_TEST_ASSERT_NEAR(WrapRadiansTwoPi(twoPi + (pi * 0.5f)), (pi * 0.5f), 0.0001f);
	LUDUS_TEST_ASSERT_NEAR(WrapRadiansTwoPi(-(pi * 0.5f)), (twoPi - (pi * 0.5f)), 0.0001f);
}

LUDUS_TEST(Trigonometry_FastSinCosAccuracy)
{
	const float angle = Pi<float>() / 3.0f;
	const SinCosPair<float> fast = FastSinCos(angle);
	const SinCosPair<float> exact = SinCos(angle);

	LUDUS_TEST_ASSERT_NEAR(fast.Sin, exact.Sin, 0.01f);
	LUDUS_TEST_ASSERT_NEAR(fast.Cos, exact.Cos, 0.01f);
}

// ========================================
// Rect Tests
// ========================================

LUDUS_TEST(Rect_BoundsAndContains)
{
	Rect<int> rect(10, 20, 30, 40);
	LUDUS_TEST_ASSERT_EQ(rect.GetLeft(), 10);
	LUDUS_TEST_ASSERT_EQ(rect.GetRight(), 40);
	LUDUS_TEST_ASSERT_EQ(rect.GetTop(), 20);
	LUDUS_TEST_ASSERT_EQ(rect.GetBottom(), 60);

	LUDUS_TEST_ASSERT(rect.Contains(10, 20));
	LUDUS_TEST_ASSERT(rect.Contains(39, 59));
	LUDUS_TEST_ASSERT(!rect.Contains(40, 20));
	LUDUS_TEST_ASSERT(!rect.Contains(10, 60));
}

LUDUS_TEST(Rect_Intersects)
{
	Rect<int> a(0, 0, 10, 10);
	Rect<int> b(5, 5, 10, 10);
	Rect<int> c(10, 0, 5, 5);

	LUDUS_TEST_ASSERT(a.Intersects(b));
	LUDUS_TEST_ASSERT(b.Intersects(a));
	LUDUS_TEST_ASSERT(!a.Intersects(c));
}

// ========================================
// Interpolation Tests
// ========================================

LUDUS_TEST(Interpolation_ClampAndLerp)
{
	LUDUS_TEST_ASSERT_EQ(Clamp(5.0f, 0.0f, 10.0f), 5.0f);
	LUDUS_TEST_ASSERT_EQ(Clamp(-2.0f, 0.0f, 10.0f), 0.0f);
	LUDUS_TEST_ASSERT_EQ(Clamp(12.0f, 0.0f, 10.0f), 10.0f);
	LUDUS_TEST_ASSERT_EQ(Clamp(2.0f, 5.0f, 1.0f), 2.0f); // min > max: passthrough

	LUDUS_TEST_ASSERT_EQ(Lerp(0.0f, 10.0f, 0.5f), 5.0f);
	LUDUS_TEST_ASSERT_EQ(LerpClamped(0.0f, 10.0f, 2.0f), 10.0f);
}

LUDUS_TEST(Interpolation_InverseLerpMoveTowards)
{
	LUDUS_TEST_ASSERT_EQ(InverseLerp(0.0f, 10.0f, 5.0f), 0.5f);
	LUDUS_TEST_ASSERT_EQ(InverseLerp(1.0f, 1.0f, 5.0f), 0.0f);

	LUDUS_TEST_ASSERT_EQ(MoveTowards(0.0f, 10.0f, 3.0f), 3.0f);
	LUDUS_TEST_ASSERT_EQ(MoveTowards(0.0f, 10.0f, 30.0f), 10.0f);
	LUDUS_TEST_ASSERT_EQ(MoveTowards(5.0f, -5.0f, 2.0f), 3.0f);
}

LUDUS_TEST(Interpolation_SmoothSteps)
{
	LUDUS_TEST_ASSERT_EQ(SmoothStep(0.0f, 1.0f, -1.0f), 0.0f);
	LUDUS_TEST_ASSERT_EQ(SmoothStep(0.0f, 1.0f, 2.0f), 1.0f);
	LUDUS_TEST_ASSERT_EQ(SmootherStep(0.0f, 1.0f, -1.0f), 0.0f);
	LUDUS_TEST_ASSERT_EQ(SmootherStep(0.0f, 1.0f, 2.0f), 1.0f);
}

LUDUS_TEST(Interpolation_ExpDecay)
{
	LUDUS_TEST_ASSERT_EQ(ExpDecay(0.0f, 10.0f, 0.0f, 1.0f), 0.0f);
	LUDUS_TEST_ASSERT_EQ(ExpDecay(0.0f, 10.0f, 1.0f, 0.0f), 10.0f);

	const float value = ExpDecay(0.0f, 10.0f, 1.0f, 1.0f);
	LUDUS_TEST_ASSERT_NEAR(value, 5.0f, 0.0001f);

	LUDUS_TEST_ASSERT_EQ(ExpDecayRate(0.0f, 10.0f, 0.0f, 2.0f), 0.0f);
	LUDUS_TEST_ASSERT_EQ(ExpDecayRate(0.0f, 10.0f, 1.0f, 0.0f), 10.0f);
}

LUDUS_TEST(Interpolation_EaseOutShift)
{
	LUDUS_TEST_ASSERT_EQ(EaseOutShift(0, 100, 0), 100);
	LUDUS_TEST_ASSERT_EQ(EaseOutShift(0, 100, 100), 100);
	LUDUS_TEST_ASSERT_EQ(EaseOutShift(0, 100, 2), 25);
}

// ========================================
// Integration Tests
// ========================================

LUDUS_TEST(Integration_ImplicitEuler1D)
{
	IntegratorState1D state{.Position = 10.0f, .Velocity = 0.0f};
	const float resultSame = IntegrateImplicitEuler(state, 1.0f, 0.5f, 0.0f).Position;
	LUDUS_TEST_ASSERT_EQ(resultSame, 10.0f);

	IntegratorState1D moving{.Position = 0.0f, .Velocity = 5.0f};
	const IntegratorState1D result = IntegrateImplicitEuler(moving, 0.0f, 0.0f, 2.0f);
	LUDUS_TEST_ASSERT_EQ(result.Velocity, 5.0f);
	LUDUS_TEST_ASSERT_EQ(result.Position, 10.0f);
}

LUDUS_TEST(Integration_ImplicitEulerVector)
{
	IntegratorState<float> state{.Position = Vector3<float>(1.0f, 0.0f, 0.0f), .Velocity = Vector3<float>(2.0f, 0.0f, 0.0f)};
	const IntegratorState<float> result = IntegrateImplicitEuler(state, 0.0f, 0.0f, 3.0f);

	LUDUS_TEST_ASSERT_EQ(result.Velocity.X, 2.0f);
	LUDUS_TEST_ASSERT_EQ(result.Position.X, 7.0f);
	LUDUS_TEST_ASSERT_EQ(result.Position.Y, 0.0f);
	LUDUS_TEST_ASSERT_EQ(result.Position.Z, 0.0f);
}

// ========================================
// Bit Tests
// ========================================

LUDUS_TEST(Bit_IsPowerOfTwo)
{
	LUDUS_TEST_ASSERT(!IsPowerOfTwo(0u));
	LUDUS_TEST_ASSERT(IsPowerOfTwo(1u));
	LUDUS_TEST_ASSERT(IsPowerOfTwo(2u));
	LUDUS_TEST_ASSERT(!IsPowerOfTwo(3u));
}

LUDUS_TEST(Bit_GetNextPowerOfTwo)
{
	LUDUS_TEST_ASSERT_EQ(GetNextPowerOfTwo(0u), 1u);
	LUDUS_TEST_ASSERT_EQ(GetNextPowerOfTwo(1u), 1u);
	LUDUS_TEST_ASSERT_EQ(GetNextPowerOfTwo(2u), 2u);
	LUDUS_TEST_ASSERT_EQ(GetNextPowerOfTwo(3u), 4u);
	LUDUS_TEST_ASSERT_EQ(GetNextPowerOfTwo(9u), 16u);
}

// ========================================
// Container Tests
// ========================================

LUDUS_TEST(DynamicArray_PushBack)
{
	DynamicArray<int> arr;
	arr.PushBack(1);
	arr.PushBack(2);
	arr.PushBack(3);

	LUDUS_TEST_ASSERT_EQ(arr.GetSize(), 3u);
	LUDUS_TEST_ASSERT_EQ(arr[0], 1);
	LUDUS_TEST_ASSERT_EQ(arr[1], 2);
	LUDUS_TEST_ASSERT_EQ(arr[2], 3);
}

LUDUS_TEST(DynamicArray_PopBack)
{
	DynamicArray<int> arr;
	arr.PushBack(1);
	arr.PushBack(2);
	arr.PushBack(3);
	arr.PopBack();

	LUDUS_TEST_ASSERT_EQ(arr.GetSize(), 2u);
	LUDUS_TEST_ASSERT_EQ(arr[0], 1);
	LUDUS_TEST_ASSERT_EQ(arr[1], 2);
}

LUDUS_TEST(String_Construction)
{
	String str("Hello");
	LUDUS_TEST_ASSERT_EQ(str.GetSize(), 5u);
	
	const char* data = str.GetData();
	LUDUS_TEST_ASSERT(data[0] == 'H');
	LUDUS_TEST_ASSERT(data[4] == 'o');
}

LUDUS_TEST(String_Append)
{
	String str("Hello");
	str.Append(" World", 6);
	
	LUDUS_TEST_ASSERT_EQ(str.GetSize(), 11u);
}

LUDUS_TEST(String_Equality)
{
	String str1("Test");
	String str2("Test");
	String str3("Different");

	LUDUS_TEST_ASSERT(str1 == str2);
	LUDUS_TEST_ASSERT(str1 != str3);
}

// ========================================
// CommandLineManager Tests
// ========================================

LUDUS_TEST(CommandLineManager_BasicParsing)
{
	// Simple space-separated arguments
	char cmdLine[] = "program arg1 arg2 arg3";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 4u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("arg1"));
	LUDUS_TEST_ASSERT(args[2] == String("arg2"));
	LUDUS_TEST_ASSERT(args[3] == String("arg3"));
}

LUDUS_TEST(CommandLineManager_DoubleQuotes)
{
	// Arguments with spaces inside double quotes
	char cmdLine[] = "program \"hello world\" arg2";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 3u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("hello world"));
	LUDUS_TEST_ASSERT(args[2] == String("arg2"));
}

LUDUS_TEST(CommandLineManager_SingleQuotes)
{
	// Arguments with spaces inside single quotes
	char cmdLine[] = "program 'single quoted' arg2";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 3u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("single quoted"));
	LUDUS_TEST_ASSERT(args[2] == String("arg2"));
}

LUDUS_TEST(CommandLineManager_EscapeSequences)
{
	// Test escape sequences
	// NOLINTNEXTLINE(modernize-raw-string-literal) - Testing parser's escape handling, not C++ escapes
	char cmdLine[] = "program \"hello\\nworld\" \"tab\\there\"";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 3u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("hello\nworld"));
	LUDUS_TEST_ASSERT(args[2] == String("tab\there"));
}

LUDUS_TEST(CommandLineManager_EscapeQuotes)
{
	// Test escaping quotes inside quoted strings
	// NOLINTNEXTLINE(modernize-raw-string-literal) - Testing parser's escape handling, not C++ escapes
	char cmdLine[] = "program \"say \\\"hello\\\"\" 'don\\'t'";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 3u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("say \"hello\""));
	LUDUS_TEST_ASSERT(args[2] == String("don't"));
}

LUDUS_TEST(CommandLineManager_EscapeSpace)
{
	// Test escaping spaces outside of quotes
	char cmdLine[] = "program hello\\ world arg2";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 3u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("hello world"));
	LUDUS_TEST_ASSERT(args[2] == String("arg2"));
}

LUDUS_TEST(CommandLineManager_MixedQuotesAndEscapes)
{
	// Complex case mixing quotes and escapes
	// NOLINTNEXTLINE(modernize-raw-string-literal) - Testing parser's escape handling, not C++ escapes
	char cmdLine[] = "program \"path\\nwith spaces\" unquoted\\ value 'another arg'";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 4u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("path\nwith spaces"));
	LUDUS_TEST_ASSERT(args[2] == String("unquoted value"));
	LUDUS_TEST_ASSERT(args[3] == String("another arg"));
}

LUDUS_TEST(CommandLineManager_MultipleSpaces)
{
	// Multiple consecutive spaces should be treated as single separator
	char cmdLine[] = "program    arg1     arg2";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 3u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("arg1"));
	LUDUS_TEST_ASSERT(args[2] == String("arg2"));
}

LUDUS_TEST(CommandLineManager_EmptyQuotes)
{
	// Empty quoted strings should produce empty arguments
	char cmdLine[] = "program \"\" arg2";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 3u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String(""));
	LUDUS_TEST_ASSERT(args[2] == String("arg2"));
}

LUDUS_TEST(CommandLineManager_BackslashAtEnd)
{
	// Trailing backslash should be preserved
	char cmdLine[] = "program test\\\\";
	CommandLineManager<char> manager = CommandLineManager<char>::Create(cmdLine);
	const DynamicArray<String>& args = manager.GetArguments();

	LUDUS_TEST_ASSERT_EQ(args.GetSize(), 2u);
	LUDUS_TEST_ASSERT(args[0] == String("program"));
	LUDUS_TEST_ASSERT(args[1] == String("test\\"));
}

// ========================================
// Logger Tests
// ========================================

LUDUS_TEST(Logger_FormatsAndDispatchesToCustomSink)
{
	ResetLogger();
	SetStandardLogSinkEnabled(false);
	SetDebuggerLogSinkEnabled(false);

	CapturedLog captured;
	LUDUS_TEST_ASSERT(AddLogSink(CaptureLogSink, &captured));

	LUDUS_LOG_INFO("CoreTest", "Frame {} ready in {} ms", 7, 3.5f);

	LUDUS_TEST_ASSERT_EQ(captured.Count, 1u);
	LUDUS_TEST_ASSERT(captured.Category == String("CoreTest"));
	LUDUS_TEST_ASSERT(captured.Text == String("Frame 7 ready in 3.5 ms"));
	LUDUS_TEST_ASSERT(captured.Level == LogLevel::Info);
	LUDUS_TEST_ASSERT(RemoveLogSink(CaptureLogSink, &captured));
}

LUDUS_TEST(Logger_RespectsRuntimeLevelFiltering)
{
	ResetLogger();
	SetStandardLogSinkEnabled(false);
	SetDebuggerLogSinkEnabled(false);
	SetLogLevel(LogLevel::Warning);

	CapturedLog captured;
	LUDUS_TEST_ASSERT(AddLogSink(CaptureLogSink, &captured));

	LUDUS_LOG_INFO("CoreTest", "This should be filtered");
	LUDUS_TEST_ASSERT_EQ(captured.Count, 0u);

	LUDUS_LOG_ERROR("CoreTest", "This should pass");
	LUDUS_TEST_ASSERT_EQ(captured.Count, 1u);
	LUDUS_TEST_ASSERT(captured.Text == String("This should pass"));
	LUDUS_TEST_ASSERT(RemoveLogSink(CaptureLogSink, &captured));
}

LUDUS_TEST(Logger_ResetClearsCustomSinksAndRestoresDefaults)
{
	ResetLogger();
	SetStandardLogSinkEnabled(false);
	SetDebuggerLogSinkEnabled(false);
	SetLogLevel(LogLevel::Fatal);

	CapturedLog captured;
	LUDUS_TEST_ASSERT(AddLogSink(CaptureLogSink, &captured));

	ResetLogger();
	SetStandardLogSinkEnabled(false);
	SetDebuggerLogSinkEnabled(false);

	LUDUS_TEST_ASSERT(GetLogLevel() == GetDefaultLogLevel());
	LUDUS_LOG_INFO("CoreTest", "Reset should remove prior test sink");
	LUDUS_TEST_ASSERT_EQ(captured.Count, 0u);
}

LUDUS_TEST(Logger_SupportsIntegerBaseFormatting)
{
	ResetLogger();
	SetStandardLogSinkEnabled(false);
	SetDebuggerLogSinkEnabled(false);

	CapturedLog captured;
	LUDUS_TEST_ASSERT(AddLogSink(CaptureLogSink, &captured));

	const uint32_t value = 42u;
	LUDUS_LOG_INFO("CoreTest", "bin={:#010b} oct={:#06o} hex={:#06x}", value, value, value);

	LUDUS_TEST_ASSERT_EQ(captured.Count, 1u);
	LUDUS_TEST_ASSERT(captured.Text == String("bin=0b00101010 oct=00052 hex=0x002a"));
	LUDUS_TEST_ASSERT(RemoveLogSink(CaptureLogSink, &captured));
}

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
