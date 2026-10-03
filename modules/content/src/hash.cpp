#include <ludus/content/content.h>

namespace ludus::content
{
namespace
{
constexpr uint32 K[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5, 0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3, 0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc, 0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7, 0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13, 0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3, 0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5, 0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208, 0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2};
uint32 Rotate(uint32 value, uint32 amount) noexcept
{
    return (value >> amount) | (value << (32 - amount));
}
void Block(const uint8* input, uint32* state) noexcept
{
    uint32 w[64]{};
    for (usize i = 0; i < 16; ++i)
    {
        w[i] = (static_cast<uint32>(input[i * 4]) << 24) | (static_cast<uint32>(input[i * 4 + 1]) << 16) |
               (static_cast<uint32>(input[i * 4 + 2]) << 8) | input[i * 4 + 3];
    }
    for (usize i = 16; i < 64; ++i)
    {
        const auto a = w[i - 15], b = w[i - 2];
        w[i] = w[i - 16] + (Rotate(a, 7) ^ Rotate(a, 18) ^ (a >> 3)) + w[i - 7] +
               (Rotate(b, 17) ^ Rotate(b, 19) ^ (b >> 10));
    }
    auto a = state[0], b = state[1], c = state[2], d = state[3], e = state[4], f = state[5], g = state[6], h = state[7];
    for (usize i = 0; i < 64; ++i)
    {
        const auto t1 = h + (Rotate(e, 6) ^ Rotate(e, 11) ^ Rotate(e, 25)) + ((e & f) ^ ((~e) & g)) + K[i] + w[i];
        const auto t2 = (Rotate(a, 2) ^ Rotate(a, 13) ^ Rotate(a, 22)) + ((a & b) ^ (a & c) ^ (b & c));
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state[0] += a;
    state[1] += b;
    state[2] += c;
    state[3] += d;
    state[4] += e;
    state[5] += f;
    state[6] += g;
    state[7] += h;
}
} // namespace
bool Hasher::Add(std::span<const uint8> data) noexcept
{
    if (data.size() > static_cast<uint64>(-1) / 8 - mBytes)
    {
        return false;
    }
    mBytes += data.size();
    for (const auto byte : data)
    {
        mTail[mCount++] = byte;
        if (mCount == 64)
        {
            Block(mTail, mState);
            mCount = 0;
        }
    }
    return true;
}
Digest Hasher::Finish() const noexcept
{
    uint32 state[8];
    for (usize i = 0; i < 8; ++i)
    {
        state[i] = mState[i];
    }
    uint8 tail[128]{};
    for (usize i = 0; i < mCount; ++i)
    {
        tail[i] = mTail[i];
    }
    tail[mCount] = 0x80;
    const usize bytes = mCount < 56 ? 64 : 128;
    const uint64 bits = mBytes * 8;
    for (usize i = 0; i < 8; ++i)
    {
        tail[bytes - 1 - i] = static_cast<uint8>(bits >> (i * 8));
    }
    Block(tail, state);
    if (bytes == 128)
    {
        Block(tail + 64, state);
    }
    Digest result;
    for (usize i = 0; i < 32; ++i)
    {
        result.Data[i] = static_cast<uint8>(state[i / 4] >> (24 - (i % 4) * 8));
    }
    return result;
}
Digest Hash(std::span<const uint8> data) noexcept
{
    Hasher hash;
    (void)hash.Add(data);
    return hash.Finish();
}
} // namespace ludus::content
