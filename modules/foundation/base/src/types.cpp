// Representation checks compile once for every supported engine build; they do
// not add <limits> or templates to the universal types.h include path.
#include <ludus/foundation/base/types.h>

#include <climits>
#include <limits>
#include <type_traits>

namespace ludus::foundation::core
{
static_assert(CHAR_BIT == 8, "Ludus requires 8-bit bytes");
static_assert(sizeof(usize) == sizeof(void*) && sizeof(isize) == sizeof(usize),
              "Ludus requires native size/difference types with pointer width");

template <typename ValueType, usize Bits, bool Signed>
constexpr bool HasIntegerRepresentation =
    std::numeric_limits<ValueType>::is_integer && std::numeric_limits<ValueType>::is_signed == Signed &&
    std::numeric_limits<ValueType>::radix == 2 && std::numeric_limits<ValueType>::digits == Bits - (Signed ? 1 : 0);

static_assert(HasIntegerRepresentation<uint8, 8, false> && HasIntegerRepresentation<int8, 8, true>);
static_assert(HasIntegerRepresentation<uint16, 16, false> && HasIntegerRepresentation<int16, 16, true>);
static_assert(HasIntegerRepresentation<uint32, 32, false> && HasIntegerRepresentation<int32, 32, true>);
static_assert(HasIntegerRepresentation<uint64, 64, false> && HasIntegerRepresentation<int64, 64, true>);
static_assert(HasIntegerRepresentation<usize, sizeof(usize) * 8, false> &&
              HasIntegerRepresentation<isize, sizeof(isize) * 8, true>);
static_assert(std::is_same_v<std::make_signed_t<usize>, isize>,
              "Ludus requires matching native size and difference ranges");

static_assert(std::numeric_limits<float32>::is_iec559 && std::numeric_limits<float32>::radix == 2 &&
                  std::numeric_limits<float32>::digits == 24 && std::numeric_limits<float32>::min_exponent == -125 &&
                  std::numeric_limits<float32>::max_exponent == 128,
              "Ludus requires IEEE binary32 float32");
static_assert(std::numeric_limits<float64>::is_iec559 && std::numeric_limits<float64>::radix == 2 &&
                  std::numeric_limits<float64>::digits == 53 && std::numeric_limits<float64>::min_exponent == -1021 &&
                  std::numeric_limits<float64>::max_exponent == 1024,
              "Ludus requires IEEE binary64 float64");
} // namespace ludus::foundation::core
