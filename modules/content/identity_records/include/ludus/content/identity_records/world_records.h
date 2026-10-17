#pragma once

// Version-2 World records: the version-1 entity/component envelope plus durable
// UUIDs. See docs/architecture/object-handles-uuid.md#stage-3-uuid-authoring-records.
#include <ludus/foundation/base/core.h>

#include <ludus/content/content.h>
#include <ludus/content/identity_records/migration_manifest.h>
#include <ludus/foundation/containers/array.hpp>

#include <span>
#include <string_view>

namespace ludus::content::identity_records
{
/// Envelope version of a World document whose objects carry only scoped string IDs.
inline constexpr uint64 WORLD_RECORD_VERSION_1 = 1;
/// Envelope version of a World document with a document UUID and one source-object UUID per entity.
inline constexpr uint64 WORLD_RECORD_VERSION_2 = 2;
/// Largest accepted World schema text, in bytes (the Editor's schema limit).
inline constexpr usize MAX_WORLD_SCHEMA_BYTES = usize{64} * 1024;

/// One schema property whose value names another entity of the same World.
struct EntityReferenceProperty final
{
    /// Component ID in the World schema.
    ResourceId Component;
    /// Property ID within that component.
    ResourceId Property;
};

/// @brief Collect the `entity`-typed properties of a World schema.
///
/// Reads the Editor's schema shape (`components[].id` and
/// `components[].properties[]` with `id` and `type`) and ignores the other members,
/// which the Editor validates.
/// @param schemaJson World schema text, at most MAX_WORLD_SCHEMA_BYTES.
/// @param outProperties Replaced on success, in schema order; unchanged on failure.
/// @param outError Receives the failure; reset to Success on success.
/// @return Success, Malformed, Limit or OutOfMemory.
[[nodiscard]] IdentityRecordStatus
ReadEntityReferenceProperties(std::string_view schemaJson,
                              foundation::Array<EntityReferenceProperty>& outProperties,
                              IdentityRecordError& outError) noexcept;

/// @brief Replay a committed manifest to turn one version-1 World document into a version-2 record.
///
/// The output is
/// `{"version": 2, "schema", "document": <AssetId>, "entities": [{"uuid", "label", "name", "components"}]}`.
/// Each entity's version-1 `id` becomes its `label`; `uuid` is the manifest's
/// SourceObjectId for that ID. A nonempty value of a property listed in
/// `references` is rewritten from the target's legacy ID to the target's UUID;
/// an empty value stays empty (no target). Entity order, the display `name` and
/// every other value are preserved; output uses two-space indentation and a
/// trailing newline. Replay is pure: it mints nothing, reads no files, and the
/// same inputs always produce the same bytes.
///
/// The manifest must describe exactly the document's IDs. A World entity missing
/// from the manifest, or a manifest object missing from the World, reports
/// StaleManifest instead of minting or dropping an identity. Documents that use
/// the Editor's `scene` extension, whose parent links are not yet migrated, report
/// Unsupported.
/// @param manifest Committed manifest.
/// @param source The document's path as recorded in the manifest, and its version-1 World JSON
/// (at most MAX_WORLD_DOCUMENT_BYTES).
/// @param references Entity-typed properties, normally from ReadEntityReferenceProperties().
/// @param outWorldJson Replaced on success; unchanged on failure.
/// @param outError Receives the failure; reset to Success on success.
/// @return Success, Malformed, UnsupportedVersion, Unsupported, UnknownDocument, StaleManifest,
/// InvalidLegacyId, UnresolvedReference, Limit or OutOfMemory.
[[nodiscard]] IdentityRecordStatus ApplyWorldMigration(const MigrationManifest& manifest,
                                                       const MigrationSource& source,
                                                       std::span<const EntityReferenceProperty> references,
                                                       Bytes& outWorldJson,
                                                       IdentityRecordError& outError) noexcept;
} // namespace ludus::content::identity_records
