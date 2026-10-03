#pragma once

#include <ludus/foundation/base/types.h>

#include <ludus/graphics/rhi/rhi.h>

namespace ludus::graphics::rhi::internal
{
// Numeric callback tokens never borrow application memory and never wrap.
bool Current(ludus::foundation::uint32 token) noexcept;
void Complete(ludus::foundation::uint32 token,
              StartupError error,
              ludus::foundation::uint32 maxTextureDimension) noexcept;
void Fail(ludus::foundation::uint32 token, StartupError error) noexcept;
// Records which browser backend the dispatcher has committed to for this token
// so GetStartup reports the actually selected backend, not merely the request.
// Ignored for a stale token.
void SelectBackend(ludus::foundation::uint32 token, Backend backend) noexcept;
// Records a bounded per-backend attempt outcome for Auto diagnostics. Ignored
// for a stale token.
void RecordAttempt(ludus::foundation::uint32 token, Backend backend, StartupError error) noexcept;

// Browser Auto fallback hook. The dispatcher registers a handler that Fail()
// consults before finalizing a Pending session as Failed: returning true means
// the handler has consumed the failure and started another backend attempt, so
// Fail() must not finalize the session. The handler runs only for the current
// session token in the Pending state. A single-backend build registers nothing.
using FallbackHandler = bool (*)(ludus::foundation::uint32 token, StartupError error) noexcept;
void SetFallback(FallbackHandler handler) noexcept;
// Advances the current Pending session to a new generation token and returns it.
// A superseded backend attempt's late callbacks carry the old token and become
// no-ops (Current is false for them), isolating the next attempt. Returns 0 if
// the token is not the current Pending session or the generation is exhausted.
ludus::foundation::uint32 Reissue(ludus::foundation::uint32 token) noexcept;
} // namespace ludus::graphics::rhi::internal
