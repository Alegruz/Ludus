#pragma once

// Allocation injection exists only in a separate unexported test archive.
namespace ludus::foundation::filesystem::test
{
bool PackAllocationAllowed() noexcept;
}
