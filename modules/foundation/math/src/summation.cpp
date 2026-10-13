// Neumaier compensated summation (see summation.hpp).

#include <ludus/foundation/math/summation.hpp>

#include <ludus/foundation/math/scalar.hpp>

namespace ludus::foundation::math
{
void CompensatedSum::Add(float64 value) noexcept
{
    const float64 total = mTotal + value;
    if (Abs(mTotal) >= Abs(value))
    {
        mCompensation += (mTotal - total) + value;
    }
    else
    {
        mCompensation += (value - total) + mTotal;
    }
    mTotal = total;
}

float64 CompensatedSum::GetResult() const noexcept
{
    return mCompensation != 0.0 && IsFinite(mCompensation) == true ? mTotal + mCompensation : mTotal;
}

float64 CompensatedDot(Vector3d a, Vector3d b) noexcept
{
    CompensatedSum sum;
    sum.Add(a.X * b.X);
    sum.Add(a.Y * b.Y);
    sum.Add(a.Z * b.Z);
    return sum.GetResult();
}
} // namespace ludus::foundation::math
