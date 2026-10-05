#pragma once

#include <ludus/foundation/base/types.h>

namespace ludus::sample
{
using foundation::float32;
using foundation::float64;
using foundation::int32;
using foundation::uint64;

// Native simulation storage. Only fields selected in body.schema.json are
// reflected; checkpoint state remains owned by the game's existing codec.
struct Body final
{
    float32 PositionX = 0.0F;
    float32 Velocity = 0.5F;
    float32 Speed = 1.0F;
    int32 Bounces = 0;
    uint64 Rng = 0x243F6A8885A308D3ULL;
    float64 SimTime = 0.0;
};
} // namespace ludus::sample
