# ADR 0019: Bounded renderer-independent runtime UI core

Status: Accepted for U0.

Ludus needs runtime HUD/menu layout and input ownership without a dependency on
its desktop Qt editor or a renderer feature set that does not yet exist.

Introduce `Ludus::Ui`: value document descriptions, persistent stable-ID focus
and pointer capture, transactional bounded layout resolution, clipped ordered
solid-rectangle commands, and callback-free action results. Allocate explicitly
at initialization, never while resolving or routing events. Keep platform,
text/IME, accessibility, authoring and GPU adapters separate. Browser controls
continue to use native DOM semantics; desktop authoring continues to use Qt.

The core is available in native and browser SDKs with a Base-only public header.
General text/image rendering, controller navigation, modal scopes and authoring
remain staged work. Sandbox's bounded fullscreen rectangle adapter proves the
current core and will be replaced by an indexed-quad renderer when RHI resources
permit it. It is not the general rendering strategy.

See [the architecture and Gems review](../architecture/ui.md) for alternatives,
contracts, delivery gates and reasons not to adopt an XML condition hierarchy,
global constraint solver or unmeasured cache graph in the first slice.
