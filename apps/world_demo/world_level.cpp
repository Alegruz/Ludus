#include "internal/level.h"

#include <ludus/foundation/base/parse_number.hpp>
#include <ludus/foundation/math/scalar.hpp>
#include <ludus/foundation/parsing/json.hpp>

namespace ludus::world_demo
{
namespace
{
using foundation::parsing::JsonValue;
bool NameValue(JsonValue value, Name& output, bool empty = false) noexcept
{
    std::string_view text;
    if (!value.String(text) || text.size() >= sizeof(output.Text) || (!empty && text.empty()))
    {
        return false;
    }
    for (usize i = 0; i < text.size(); ++i)
    {
        const auto ch = text[i];
        if ((ch < 'a' || ch > 'z') && (ch < '0' || ch > '9') && ch != '-')
        {
            return false;
        }
        output.Text[i] = ch;
    }
    return true;
}
bool Number(JsonValue value, float32& output) noexcept
{
    float64 number = 0;
    if (!value.Number(number) || !foundation::math::IsFinite(number) || number < -100000 || number > 100000)
    {
        return false;
    }
    output = static_cast<float32>(number);
    return true;
}
bool Transform(JsonValue value, Recipe& recipe) noexcept
{
    return value.Fields({"x", "y", "rotation", "scale_x", "scale_y"}) && Number(value.Get("x"), recipe.Position.X) &&
           Number(value.Get("y"), recipe.Position.Y) && Number(value.Get("rotation"), recipe.Rotation) &&
           Number(value.Get("scale_x"), recipe.Scale.X) && Number(value.Get("scale_y"), recipe.Scale.Y);
}
bool Actor(JsonValue value, Recipe& recipe, bool enemy) noexcept
{
    uint64 health = 0;
    if (!(enemy ? value.Fields({"speed", "health", "sprite", "target"})
                : value.Fields({"speed", "health", "sprite"})) ||
        !Number(value.Get("speed"), recipe.Speed) || !value.Get("health").Integer(health) || health > 100000 ||
        !NameValue(value.Get("sprite"), recipe.Sprite) || (enemy && !NameValue(value.Get("target"), recipe.Target)))
    {
        return false;
    }
    recipe.Type = enemy ? Kind::Enemy : Kind::Player;
    recipe.Health = static_cast<uint32>(health);
    return true;
}
bool Settings(JsonValue value, Level& level) noexcept
{
    std::string_view seed;
    return value.Fields(
               {"id", "seed", "min_x", "min_y", "max_x", "max_y", "camera_x", "camera_y", "vertical_extent"}) &&
           NameValue(value.Get("id"), level.Id) && value.Get("seed").String(seed) &&
           ParseUint64(seed.data(), seed.size(), level.Seed) == NumberParseStatus::Success &&
           Number(value.Get("min_x"), level.Minimum.X) && Number(value.Get("min_y"), level.Minimum.Y) &&
           Number(value.Get("max_x"), level.Maximum.X) && Number(value.Get("max_y"), level.Maximum.Y) &&
           Number(value.Get("camera_x"), level.Camera.X) && Number(value.Get("camera_y"), level.Camera.Y) &&
           Number(value.Get("vertical_extent"), level.VerticalExtent);
}
} // namespace
LevelResult ReadWorldLevel(std::string_view source, Level& output) noexcept
{
    namespace parsing = foundation::parsing;
    parsing::JsonDocument document;
    parsing::ParseError error;
    parsing::JsonLimits limits;
    limits.MaxInputBytes = 65536;
    limits.MaxArrayElements = 49;
    limits.MaxValues = 4096;
    limits.MaxStringBytes = 128;
    if (source.size() > limits.MaxInputBytes)
    {
        return {LevelError::Capacity, 0};
    }
    if (document.Read(source, error, limits) != parsing::ParseStatus::Ok)
    {
        return {LevelError::Syntax, 0};
    }
    const auto root = document.Root();
    uint64 version = 0;
    std::string_view schema;
    const auto entities = root.Get("entities");
    if (!root.Fields({"version", "schema", "entities"}) || !root.Get("version").Integer(version) || version != 1 ||
        !root.Get("schema").String(schema) || schema != "ludus.world-demo.v1" || !entities.Array())
    {
        return {LevelError::InvalidValue, 0};
    }
    Level candidate;
    bool settingsSeen = false;
    Name ids[49]{};
    for (usize i = 0; i < entities.Count(); ++i)
    {
        const auto entity = entities.At(i);
        std::string_view displayName;
        const auto components = entity.Get("components");
        if (!entity.Fields({"id", "name", "components"}) || !NameValue(entity.Get("id"), ids[i]) ||
            !entity.Get("name").String(displayName) || displayName.empty() || !components.Object())
        {
            return {LevelError::InvalidValue, 0};
        }
        for (usize prior = 0; prior < i; ++prior)
        {
            if (ids[i] == ids[prior])
            {
                return {LevelError::DuplicateField, 0};
            }
        }
        if (components.Get("settings").Valid())
        {
            if (settingsSeen || !components.Fields({"settings"}) || !Settings(components.Get("settings"), candidate))
            {
                return {LevelError::InvalidValue, 0};
            }
            settingsSeen = true;
        }
        else if (components.Get("collision").Valid())
        {
            if (candidate.CollisionCount == 16)
            {
                return {LevelError::Capacity, 0};
            }
            auto& box = candidate.Collision[candidate.CollisionCount++];
            const auto collision = components.Get("collision");
            box.Id = ids[i];
            if (!components.Fields({"collision"}) || !collision.Fields({"x", "y", "half_x", "half_y"}) ||
                !Number(collision.Get("x"), box.Center.X) || !Number(collision.Get("y"), box.Center.Y) ||
                !Number(collision.Get("half_x"), box.HalfExtent.X) ||
                !Number(collision.Get("half_y"), box.HalfExtent.Y))
            {
                return {LevelError::InvalidValue, 0};
            }
        }
        else
        {
            if (candidate.EntityCount == 32)
            {
                return {LevelError::Capacity, 0};
            }
            auto& recipe = candidate.Entities[candidate.EntityCount++];
            recipe.Id = ids[i];
            if (!Transform(components.Get("transform"), recipe))
            {
                return {LevelError::InvalidValue, 0};
            }
            if (components.Get("exit").Valid())
            {
                const auto exit = components.Get("exit");
                recipe.Type = Kind::Exit;
                if (!components.Fields({"transform", "exit"}) || !exit.Fields({"half_x", "half_y", "next_level"}) ||
                    !Number(exit.Get("half_x"), recipe.HalfExtent.X) ||
                    !Number(exit.Get("half_y"), recipe.HalfExtent.Y) ||
                    !NameValue(exit.Get("next_level"), recipe.NextLevel))
                {
                    return {LevelError::InvalidValue, 0};
                }
            }
            else
            {
                const bool enemy = components.Get("enemy").Valid();
                if (!(enemy ? components.Fields({"transform", "enemy"}) : components.Fields({"transform", "player"})) ||
                    !Actor(components.Get(enemy ? "enemy" : "player"), recipe, enemy))
                {
                    return {LevelError::InvalidValue, 0};
                }
            }
        }
    }
    if (!settingsSeen)
    {
        return {LevelError::MissingField, 0};
    }
    const auto valid = ValidateLevel(candidate);
    if (valid != LevelError::None)
    {
        return {valid, 0};
    }
    output = candidate;
    return {};
}
} // namespace ludus::world_demo
