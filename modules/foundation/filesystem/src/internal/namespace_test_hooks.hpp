#pragma once

// Only compiled into a separate, unexported fault-test backend. Production
// allocations use nothrow new directly, with no mutable hook or dispatch.
namespace ludus::foundation::filesystem::test
{
bool NamespaceAllocationAllowed() noexcept;
}
