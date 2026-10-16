#pragma once

// Fixture vocabulary for the generated L0 corpus header. Private to the probe;
// see docs/architecture/localization-l0.md for the authoring schemas.

#include <ludus/foundation/base/types.h>

#include <string_view>

namespace ludus::icu_probe::corpus
{
using namespace foundation;

struct Bytes
{
    const char* Data = "";
    usize Size = 0;

    [[nodiscard]] constexpr std::string_view GetView() const noexcept
    {
        return {Data, Size};
    }
};

enum class ArgumentType : uint8
{
    Text,
    Count,
    Number,
    Enum,
};

enum class TextOrigin : uint8
{
    Game,
    User,
};

enum class UnknownPolicy : uint8
{
    Other,
    Reject,
};

struct ArgumentSchema
{
    Bytes Name;
    ArgumentType Type = ArgumentType::Text;
    TextOrigin Origin = TextOrigin::Game;
    usize MaxBytes = 0;
    int64 Minimum = 0;
    int64 Maximum = 0;
    const Bytes* ValuesOrNull = nullptr;
    usize ValueCount = 0;
    UnknownPolicy Unknown = UnknownPolicy::Reject;
};

struct MessageSchema
{
    Bytes Key;
    const ArgumentSchema* ArgumentsOrNull = nullptr;
    usize ArgumentCount = 0;
};

/// One resolved (locale, message) pattern and the catalog locale that provides it.
struct Pattern
{
    usize Locale = 0;
    usize Message = 0;
    Bytes Text;
    usize Provider = 0;
};

enum class ValueKind : uint8
{
    Text,
    Integer,
    Number,
};

struct ArgumentValue
{
    Bytes Name;
    ValueKind Kind = ValueKind::Text;
    int64 Integer = 0;
    float64 Number = 0.0;
    Bytes Text;
};

enum class Outcome : uint8
{
    Formatted,
    InvalidArguments,
};

struct Case
{
    usize Locale = 0;
    usize Message = 0;
    const ArgumentValue* ValuesOrNull = nullptr;
    usize ValueCount = 0;
    Outcome Expected = Outcome::Formatted;
    Bytes Output;
};

enum class Rejection : uint8
{
    Accepted,
    SyntaxError,
    NumberedArgument,
    UndeclaredArgument,
    UnsupportedArgumentType,
    ArgumentTypeMismatch,
    UnapprovedNumberStyle,
    DeadPluralKeyword,
    ExactValueOutOfRange,
    NonIntegerExactValue,
    InvalidPluralOffset,
    UndeclaredEnumValue,
    NestingTooDeep,
    TooManyBranches,
    PatternTooLarge,
};

struct RejectionCase
{
    usize Locale = 0;
    usize Message = 0;
    Bytes Pattern;
    Rejection Reason = Rejection::Accepted;
};

struct LinguisticData
{
    Bytes Icu;
    Bytes Cldr;
    Bytes Unicode;
};
} // namespace ludus::icu_probe::corpus
