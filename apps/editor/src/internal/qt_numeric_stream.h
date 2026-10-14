#pragma once

#include <ludus/foundation/base/compiler.h>
#include <ludus/foundation/base/types.h>

#include <QDataStream>

namespace ludus::editor
{
// Qt's quint64 and Ludus's uint64 have different underlying types on LP64
// Linux. Use unambiguous uint32 overloads while preserving the exact wire bytes.
// The caller selects LittleEndian; QDataStream retains its ordinary write status.
LUDUS_INLINE void WriteLittleEndianUint64(QDataStream& inoutStream, foundation::uint64 value) noexcept
{
    inoutStream << static_cast<foundation::uint32>(value) << static_cast<foundation::uint32>(value >> 32);
}
} // namespace ludus::editor
