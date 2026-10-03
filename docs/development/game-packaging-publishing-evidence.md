# Game packaging and publishing implementation evidence

This ledger records the first implementation of
[the architecture](../architecture/game-packaging-publishing.md). Starting
revision: `f8b00760c18b803cc904f8134462f060941ea60e`. Branch:
`codex/game-packaging-publishing`. Results below were collected on 2026-10-02.
This is a first implementation PR, not completion of R0 through R5.

## Implemented scope

Release schema version 1, optional release files inside atomic project creation,
native Release build/install staging, CMake File API artifact validation, ELF
runtime checks, canonical archives, bounded clean extraction, payload/metadata
verification, atomic no-replace package publication and offline itch.io planning
are implemented in the installed Python tooling. Usage is documented in
[the release guide](game-packaging-publishing.md).

Existing native project creation stored SDK overrides under the SDK's
`x86_64-linux-gnu` identity, while unresolved-lock resolution defaulted to
`linux-x64`. The default now matches the SDK identity. The installed wheel also
ships the existing standalone `cmake_targets` resolver, and artifact resolution
uses a lightweight error shim instead of importing checkout-only `engine`.
The generated minimal template is version 2 and queries the public engine version
instead of using printf diagnostics. Its CMake project identifier uses the
validated target rather than the human name.

`BUTLER_API_KEY` is excluded from project build environments, compiler probes,
source-state probes and ELF inspection. No live upload, account login, network
request or butler installation occurs in these operations. There is no generated
itch.io destination unless the project creator supplies one explicitly.

## Local validation

Baseline before edits: the four existing host-tooling suites ran 96 tests and
passed. After implementation, this command ran 117 tests and passed, including
21 release tests and their parameterized rejection cases:

```bash
PATH=/home/alegruz/workspace/Ludus/out/host-tools/venv/bin:$PATH \
  python3 -m unittest discover -s scripts/python -p 'test_ludus*.py' -q
```

The host Python was 3.12.3; managed CMake was 3.29.6 and Ninja 1.11.1. The release
roundtrip compiler was Ubuntu Clang 18.1.3. Tests compile real ELF binaries with
C++23, exceptions disabled and warning flags, build a generated CMake project,
install its release component, validate/extract/run the executable, and inspect
an offline plan after removing the fixture SDK. Separate tests verify a failed
rebuild cannot package an older binary, cancellation/lock contention expose no
partial package, SDK mutation fails, executable mode survives ZIP extraction,
corrupt payloads/metadata fail and explicitly bundled relative dependencies work. A CMake install-time loader
path rewrite changes the executable bytes but preserves its program-section
identity; the package validator accepts this while rejecting a different program.
No test writes to itch.io.

Archive/configuration cases include traversal, absolute and malformed paths,
symlinks, unknown schema fields, duplicate JSON fields, non-finite JSON numbers,
JSON bounds, case and parent-directory collisions, size/entry bounds, missing
notices/licenses, debug sidecars, unknown runtime dependencies and absolute loader
paths. The native dependency checker uses readelf, never ldd or game execution.
Only the explicit clean-extraction acceptance test runs the known fixture binary.

A real host-tool wheel was built and installed under a new temporary directory.
From `/tmp`, the installed package and its File API resolver imported without
`engine`, Qt or a producer checkout on their module paths. The release suite also
ran all 21 release tests against that installed backend. The existing Editor
adapter suite also passed all 35 tests. CI repeats this with a fresh venv and the
pinned CMake/Ninja versions, Clang 18 and binutils.

`./scripts/check --format` and the foundational include-boundary gate passed.
Python compileall and `git diff --check` passed. New production logic is Python;
no runtime engine C++ implementation or public header changed. The generated
C++ application is compiled by the release tests.

## Pending acceptance and limitations

R0 and R1 remain partially validated. Tests use a deliberately small fixture SDK,
not an accepted production SDK archive. They do not prove full real SDK dependency
relocation, complete third-party license closure, windowed/GPU runtime acceptance,
itch-app installation, or every supported OS ABI. The external ELF policy starts
with explicitly listed Linux C/C++ ABI libraries; other runtime libraries need
explicit installation and notices or a reviewed policy extension. Presence of
notice files is not proof that every license obligation has been satisfied.

The existing synchronous CLI process runner is reused. The cancellation test
proves staging cleanup when the operation is interrupted; it does not certify
all descendant-process cleanup semantics. Full process-supervision acceptance
remains part of the project tooling gates before shipping unattended deployment.
Cooperative locks do not protect against raw CMake invocations or malicious
same-user filesystem mutations.

An attempt to run `./scripts/check linux-clang-development --all` stopped because
the isolated checkout lacked its bootstrap CMake/Ninja/Conan artifacts and Conan
Development toolchain. Native engine unit tests, tidy, ASan/UBSan and full SDK
consumer gates were not run locally for this PR. Existing repository CI must
validate the affected baseline; keep the PR draft until the required gates and
production SDK release acceptance are recorded. Do not mark these checks passed
from the fixture's native compilation.

R2 live uploading, pinned butler setup, credential/login UI, per-destination
upload locks and remote outcome receipts are not implemented. R3 generated CI,
R4 Editor controls and R5 browser project packaging are also pending. No changes
were made to an external game repository. This PR provides the package and
inspection boundary those phases consume.

## Initial CI result

The host-tooling job in [project-sdk run 37091095268](https://github.com/Alegruz/Ludus/actions/runs/37091095268)
passed for the first PR commit, including the installed release backend. The SDK
candidate relocation job was skipped by the pre-existing feature-branch policy.
This result predates the additional loader-rewrite regression; CI must validate
the updated PR head independently. The initial [ci run 37091095312](https://github.com/Alegruz/Ludus/actions/runs/37091095312)
also passed source formatting, Development/SDK validation, Clang static analysis,
ASan/UBSan, Optional Editor, build budget, PCH and assertion-policy variants.
These are results for the initial commit, not the later loader-rewrite fix.

## Editor/browser/upload follow-up

The follow-up adds no-overwrite existing-project setup, Editor controls, browser
Release packaging and a generated two-job GitHub upload workflow. Local acceptance
built a full Release browser SDK, compiled Sandbox with pinned Slang/SPIRV-Tools/
SPIRV-Cross, and packaged/verified its current JS/Wasm plus licenses. Tests cover
setup conflicts/cancellation, archive/policy checks, failed builds, SDK mismatch,
Editor request validation and an isolated upload stub verifying exact private
snapshot bytes, credential scope, failed/interrupted receipts and no automatic retries.

Validation: 132 host-tool tests, 36 release tests against an installed wheel outside
the checkout, 35 adapter regression tests and 37 C++ Editor cases (185 assertions).
The Editor built warning-clean, passed ASan/UBSan and leak checks outside the
sandbox, and all four changed Editor translation units passed pinned clang-tidy 18.
Formatting and the foundational include gate passed. A real Qt controller loaded
Sandbox's descriptor and packaged through its real adapter with cleanup confirmed
and zero dropped output. Extracted JavaScript passed Node syntax checking and the
WebAssembly compiled in Node without instantiating or running game code.

Butler 15.31.0 was acquired from the official versioned broth endpoint and its
archive/executable SHA256s are recorded in `ludus_tools.itch`. No itch.io account
was authenticated and no live upload/page change was performed. GitHub environment
secrets/destination and itch.io HTML page configuration remain user-owned setup.
Browser verification is static and does not establish dynamic JavaScript URL
closure or hosted runtime behavior. A submitted-upload receipt deliberately does
not assert processing completion. Native runtime dependency closure is not
asserted for Sandbox.
