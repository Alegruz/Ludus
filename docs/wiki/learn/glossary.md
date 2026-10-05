# Glossary

| Term | Meaning in Ludus |
| --- | --- |
| Engine checkout | The source repository used to develop Ludus and run its trusted tools |
| SDK | Installed engine headers, libraries and build metadata for a compatible target/flavor/toolchain |
| Project descriptor | `ludus.project.json`, the game's saved engine/build/launch intent |
| Release lock | Exact release-oriented selection; local development may use a separate ignored override |
| Local settings | Machine paths and owned preset data, kept outside committed portable intent |
| CMake preset | Named configure/build/test settings; selectable presets are the ones a user/tool can actually invoke |
| Build profile/flavor | Debug, Development, Profile or Release policy and compatible build/SDK inputs |
| Draft | Edited state that has not yet been successfully saved/published |
| Staging | A private temporary result verified before publication to its destination |
| Source document | Persistent authored data such as an audio definition or preferences layer |
| Runtime snapshot | Copied state from a running host/session, independent of a source-document save |
| Logical resource ID | Stable content identity resolved through a catalog, rather than a transient runtime handle |
| RHI | Rendering Hardware Interface, the engine's graphics backend boundary |
| GameHost | Runtime host for supported game-module and Play workflows |
| S1 / S2 | Editor milestone names: workspace shell, then document interactions/undo |

Continue with [architecture](architecture.md), [project setup](../guides/project-setup.md)
or [the editor workspace](../guides/editor.md).
