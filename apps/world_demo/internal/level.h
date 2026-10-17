#pragma once
#include <ludus/foundation/base/types.h>
#include <ludus/foundation/containers/array.hpp>
#include <ludus/foundation/identity/uuid.hpp>

#include <string_view>
namespace ludus::world_demo
{
using namespace foundation;
struct Vec2 final
{
    float32 X = 0;
    float32 Y = 0;
};
struct Name final
{
    char Text[32] = {};
    [[nodiscard]] std::string_view View() const noexcept
    {
        usize size = 0;
        while (size < sizeof(Text) && Text[size] != 0)
        {
            ++size;
        }
        return {Text, size};
    }
    [[nodiscard]] bool operator==(const Name& other) const noexcept
    {
        return View() == other.View();
    }
};
struct Box final
{
    Vec2 Center{};
    Vec2 HalfExtent{};
    Name Id{};
    /// Durable identity from a version-2 World record; nil for version-1 and compiled content.
    foundation::identity::SourceObjectId Object{};
};
enum class Kind : uint8
{
    Player,
    Enemy,
    Exit
};
struct Recipe final
{
    Name Id{};
    Kind Type = Kind::Player;
    Vec2 Position{};
    float32 Rotation = 0;
    Vec2 Scale{1, 1};
    float32 Speed = 0;
    uint32 Health = 0;
    Name Sprite{};
    Name Target{};
    Vec2 HalfExtent{0.4F, 0.4F};
    Name NextLevel{};
    /// Durable identity from a version-2 World record; nil for version-1 and compiled content.
    foundation::identity::SourceObjectId Object{};
};
// Bounded owned content, independent of the input buffer and runtime handles.
struct Level final
{
    Name Id{};
    /// Document identity from a version-2 World record; nil for version-1 and compiled content.
    foundation::identity::AssetId Document{};
    /// Durable identity of the version-2 settings entity; nil otherwise.
    foundation::identity::SourceObjectId SettingsObject{};
    uint64 Seed = 0;
    Vec2 Minimum{};
    Vec2 Maximum{};
    Vec2 Camera{};
    float32 VerticalExtent = 0;
    Box Collision[16] = {};
    usize CollisionCount = 0;
    Recipe Entities[32] = {};
    usize EntityCount = 0;
};
enum class LevelError : uint8
{
    None,
    Syntax,
    UnknownField,
    DuplicateField,
    MissingField,
    InvalidValue,
    Capacity,
    InvalidReference,
    MissingAsset
};
struct LevelResult final
{
    LevelError Error = LevelError::None;
    usize Offset = 0;
};
// Read the bounded World adapter (envelope version 1, or version 2 with UUID records); failure
// preserves output. Version 1 never mints identities. No project code is executed.
[[nodiscard]] LevelResult ReadWorldLevel(std::string_view source, Level& output) noexcept;
[[nodiscard]] LevelResult ReadLevel(std::string_view source, Level& output) noexcept;
[[nodiscard]] LevelError ValidateLevel(const Level& level) noexcept;
[[nodiscard]] bool WriteLevel(const Level& level, foundation::core::Array<char>& output) noexcept;
[[nodiscard]] std::string_view ExampleLevel() noexcept;
[[nodiscard]] std::string_view BuiltinLevel(std::string_view id) noexcept;
} // namespace ludus::world_demo
