#include <Ludus/Engine/Core/UnitTest.hpp>
#include <Ludus/Engine/Core/Math/Vector.hpp>
#include <Ludus/Engine/Core/Math/Trigonometry.hpp>
#include <Ludus/Engine/Core/Container/Array.hpp>
#include <Ludus/Engine/Core/Container/String.hpp>

using namespace ludus::core;

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

// NOLINTEND(cppcoreguidelines-avoid-magic-numbers, readability-magic-numbers)
