#include <ludus/foundation/base/version.hpp>

#include <ludus/foundation/base/build_metadata.hpp>

#include "internal/version_format.hpp"

namespace ludus::foundation {

Version version() noexcept
{
    return Version{
        .major = build_metadata::version_major,
        .minor = build_metadata::version_minor,
        .patch = build_metadata::version_patch,
    };
}

std::string_view version_string() noexcept
{
    return build_metadata::version_string;
}

std::string_view git_revision() noexcept
{
    return internal::normalized_revision(build_metadata::git_revision);
}

std::string_view compiler_identity() noexcept
{
    return build_metadata::compiler_identity;
}

} // namespace ludus::foundation
