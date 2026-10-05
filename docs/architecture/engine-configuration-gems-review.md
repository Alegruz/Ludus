# Configuration literature review

The original architecture favored plain typed options, explicit layers, prepared
transactions, module ownership, and bounded control-plane work. The local
[game-dev-gems-toc.md](../../references/game-dev-gems-toc.md) identifies relevant
chapters. The complete selected chapters, rather than only their titles, informed
the refinements below. Thank you to their authors. No source code was copied.

| Consulted source | Printed / physical PDF pages | Adopted or adapted idea |
| --- | --- | --- |
| Wessam Bahnassi, “Game Tuning Infrastructure,” Game Engine Gems 2, ch.16 | 263–277 / [279–293](../../references/Game%20Engine%20Gems%202.pdf#page=279) | Share typed metadata and validation between authoring and runtime; distinguish inspection from application and resource lifecycle |
| Lasse Staff Jensen, “A Generic Tweaker,” Game Programming Gems 2, §1.18 | 118–126 / [115–123](../../references/Game%20Programming%20Gems%202.pdf#page=115) | Transparent use of ordinary options; attach type/range/help metadata outside hot values |
| Peter Dalton, “Registered Variables,” Game Programming Gems 8, §4.2 | 363–372 / [378–387](../../references/Game%20Programming%20Gems%208.pdf#page=378) | Skip dependent work when semantic values do not change; replace shared mutable dirty bits with per-owner generations |
| James Boer, “A Flexible Text Parsing System,” Game Programming Gems 2, §1.17 | 112–117 / [109–114](../../references/Game%20Programming%20Gems%202.pdf#page=109) | Separate authoring, schema validation, and runtime representation; retain source information |
| Charles Cafrelli, “A Property Class for Generic C++ Member Access,” Game Programming Gems 2, §1.7 | 46–50 / [43–47](../../references/Game%20Programming%20Gems%202.pdf#page=43) | Keep inspection metadata and ordinary member access separate; avoid generic property access in engine loops |
| Jason Gregory, Game Engine Architecture, 3rd edition, §6.5 | 470–479 / physical PDF 489–498 | Save deliberate overrides, preserving inheritance from future defaults |

Bahnassi improves the baseline by making application status a distinct concern:
a knob accepted by tools is not necessarily applied by the renderer. The core
has requested/active/pending state, but requires owner-selected publication and
resource preparation. GameHost's first schema deliberately uses NextLaunch
rather than claiming live device changes without a lifecycle contract.

Jensen supports the choice to keep `HostOptions` and future subsystem options
ordinary data. His historical hierarchical tweaker implementation is inspiration,
not a requirement to create a mutable singleton database or expose direct member
pointers. Ludus descriptors are immutable borrowed metadata with explicit module
lifetime. Local bindings carry context identity to reject accidental reuse.

Dalton's redirector chains solve broader cross-system variable wiring. Ludus
adopts semantic change observation rather than those chains: options snapshots
stay plain and per-group generations let consumers remember their own progress.
Provenance-only edits still advance the context revision for conflict detection.

Boer's separate preprocessing/compiled-token phases strengthen the authoring
boundary. Ludus uses TOML in an optional tool and validates cooked typed records
with the engine's actual schema. V1 uses the existing bounded JSON backend at
startup instead of introducing generic token replay, macro expansion, or another
binary codec without measurements. File/source labels survive parser destruction;
unknown line locations remain explicitly zero.

Cafrelli motivates reusable inspection while also exposing the cost and ownership
questions of general member wrappers. Ludus exposes finite typed descriptors and
copies owner options. It does not use property proxy reads in gameplay loops,
raw offsets, dynamic casts, or writable object addresses.

Gregory's configuration discussion makes sparse persistence a requirement rather
than an optimization. Saving merged effective state would pin platform/project
values and defeat default evolution. Reset therefore removes the preference
assignment; it does not copy the inherited value into the preference file.

Modern primary references reinforce these choices:

- Epic Games, [Console Variables and Commands](https://dev.epicgames.com/documentation/en-us/unreal-engine/console-variables-cplusplus-in-unreal-engine): source priorities, explicit typed usage, batch change observation, and safe change handling. Ludus uses fixed ranks and prepared owner transactions instead of hidden callbacks or a global console manager.
- O3DE, [Settings Registry](https://www.docs.o3de.org/docs/user-guide/settings/): explicit merge and application settings separation. Ludus keeps a finite exact schema and typed module options rather than a universal JSON tree in hot code.
- [TOML 1.0 specification](https://toml.io/en/v1.0.0): tables, comments, strict duplicate-key handling, and finite authoring syntax. This is a tool format only.
- Python, [tomllib](https://docs.python.org/3/library/tomllib.html): read-only TOML parsing available from Python 3.11; no source-location or preserving writer API is assumed.

The chapters improve metadata sharing, debugging, change observation, sparse
persistence, and separation of authoring from shipping. They do not establish a
need for generic reflection, XML, scripting, live RPC, raw member pointers, or
variable redirector chains. Historical benchmark figures are not Ludus results.
