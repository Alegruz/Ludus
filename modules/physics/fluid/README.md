# PhysicsFluid

CPU shallow-water field, exported as `Ludus::PhysicsFluid`.

Include `<ludus/physics/fluid/field.h>` and initialize a `Field` with a `Config`.
Handle status from forcing and stepping; `WorkLimit` carries partial progress.
Sources and rendering remain with the caller. See the
[architecture and API contracts](../../../docs/architecture/fluid-field.md) and
[reference review](../../../docs/architecture/fluid-field-reference-review.md).
