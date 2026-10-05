#pragma once
#include <ludus/foundation/base/types.h>
namespace ludus::reflection_test
{
using namespace foundation;
struct Record final
{
    bool Enabled = true;
    int32 SmallSigned = -7;
    uint32 SmallUnsigned = 9;
    int64 Signed = -9223372036854775807LL - 1LL;
    uint64 Unsigned = 18446744073709551615ULL;
    float32 Rate = 1.25F;
    float64 Time = 0.125;
    int32 Runtime = 11;
    uint64 Unreflected = 42;
};
} // namespace ludus::reflection_test
