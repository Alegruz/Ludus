#pragma once
#include <ludus/foundation/base/types.h>
namespace consumer
{
struct Settings final
{
    ludus::foundation::uint64 Seed = 18446744073709551615ULL;
    ludus::foundation::float32 Speed = 1.0F;
};
} // namespace consumer
