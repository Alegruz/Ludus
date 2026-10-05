# Public SDK API reference

The API reference is generated automatically from the public header file sets,
with signatures, types, enum values, constants, aliases and structured comments.

**[Open the generated C++ API reference](https://alegruz.github.io/Ludus/api/index.html)**

It has its own symbol search. Wiki search finds guides and architecture pages;
it does not index the separate Doxygen HTML tree. Use
[engine architecture](../architecture/index.md) to understand system ownership
and [platform compatibility](../guides/platform-targets.md) to choose a supported
target. The reference inventories native/browser declarations without selecting
one preprocessor profile; it is not a platform availability guarantee.

Some legacy declarations still lack descriptions. They remain visible rather
than disappearing from the reference. CI tracks that backlog and rejects new
undocumented public symbols. See [documentation conventions](../contribute/api-reference.md).

The published reference follows `main`. Match your SDK revision when inspecting
contracts; generated SDK configuration values come from your configured/installed
headers, not from the template values displayed in this reference.
