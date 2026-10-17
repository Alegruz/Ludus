#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/foundation/containers/static_array.hpp>
#include <ludus/foundation/identity/uuid.hpp>

#include <QString>

namespace ludus::editor
{
// The Editor mints World, entity and level identities only through
// FoundationIdentity's TryGenerateUuidV4(), the single UUIDv4 construction path
// (docs/architecture/object-handles-uuid.md, "Persistent UUID representation and
// generation"). Workspaces pass platform::GetSystemEntropySource() unless a test
// injects a controlled or failing source. There is no fallback source: a failure
// leaves the document, the workspace and every output unchanged.

/// Status text shown when no identity could be generated.
[[nodiscard]] inline QString DescribeEntropyUnavailable()
{
    return QStringLiteral("Could not generate a new identity: the system entropy source is unavailable. "
                          "Nothing was changed.");
}

/// Generate canonical lowercase `8-4-4-4-12` UUIDv4 text from `entropy`.
/// @return false with `outText` unchanged when the source cannot supply entropy.
[[nodiscard]] inline bool TryGenerateUuidText(const foundation::identity::EntropySource& entropy, QString& outText)
{
    namespace identity = foundation::identity;
    identity::Uuid value;
    if (identity::TryGenerateUuidV4(entropy, value) != identity::UuidStatus::Success)
    {
        return false;
    }
    foundation::StaticArray<char, identity::UUID_TEXT_BUFFER_BYTES> text{};
    if (identity::FormatUuid(value, text.GetData(), text.GetSize()) != identity::UuidStatus::Success)
    {
        return false;
    }
    outText = QString::fromLatin1(text.GetData(), static_cast<qsizetype>(identity::UUID_TEXT_LENGTH));
    return true;
}
} // namespace ludus::editor
