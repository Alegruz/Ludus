#include <ludus/text/status.h>

namespace ludus::text
{
const char* ToString(Status status) noexcept
{
    switch (status)
    {
        case Status::Ok:
            return "Ok";
        case Status::Pending:
            return "Pending";
        case Status::NotReady:
            return "NotReady";
        case Status::InvalidArgument:
            return "InvalidArgument";
        case Status::InvalidHandle:
            return "InvalidHandle";
        case Status::InvalidUtf8:
            return "InvalidUtf8";
        case Status::UnsupportedText:
            return "UnsupportedText";
        case Status::UnsupportedFont:
            return "UnsupportedFont";
        case Status::UnsupportedGlyphFormat:
            return "UnsupportedGlyphFormat";
        case Status::UnsupportedPresentation:
            return "UnsupportedPresentation";
        case Status::NeedsPreparation:
            return "NeedsPreparation";
        case Status::Busy:
            return "Busy";
        case Status::ResourceLimit:
            return "ResourceLimit";
        case Status::OutOfMemory:
            return "OutOfMemory";
        case Status::BackendFailure:
            return "BackendFailure";
        case Status::DeviceLost:
            return "DeviceLost";
    }
    return "Unknown";
}
} // namespace ludus::text
