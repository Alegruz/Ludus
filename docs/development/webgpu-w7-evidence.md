# W7: Reproducible browser smoke package

W6 PR #42 is merged, with both native and WebGPU workflows successful. W7 starts
from main including the merged keyboard module; W6 still uses browser snapshots
for its demo input. No public engine API or native compiler options change here.

## Implementation

`./scripts/package-web` builds through the pinned Release web tooling, stages an
explicit file set under out/packages and writes ludus-web-smoke-release.zip with
index.html at root. It includes local JS/wasm, a sandbox harness, license notices
and a manifest with payload hashes, tool pins and memory policy. ZIP ordering,
timestamps and permissions are deterministic. Files and CRCs are checked, then
the archive is extracted into an empty temporary directory and compared bytewise.
Runtime and notice omissions fail instead of silently producing an incomplete ZIP.

The actual Release wasm memory declaration is validated: 32 MiB initial, 256 MiB
maximum, unshared. The smoke link also explicitly sets a 64 KiB stack and retains
assertions. Release removes debug names/symbols; Development keeps function names.
Browser-only macro prefix mapping makes diagnostic source locations relative.
This removes three checkout-absolute literals found in the original W6 wasm.
Packaged runtime files are scanned for local machine paths and test/debug markers.
No C++ source changes or native-option changes are needed.

Notices cover Ludus, Emscripten and its authors, musl, libcxx/libcxxabi, compiler-rt,
Emdawnwebgpu JS/C bridge and the separate BSD WebGPU native C API header notice.
The C++ WebGPU wrapper and native rendering libraries are not linked into the app.
The packaged QA iframe uses allow-scripts/allow-same-origin/fullscreen and ordinary
HTTP headers. Loading instructions now start hidden until the app is playing.

CI runs offline package contract tests, executes the packaging command and uploads
the resulting ZIP. This establishes package production, not a new hardware test.
The upload/HTTPS guide is web-packaging.md, with itch.io requirements checked
2026-10-01 against https://itch.io/docs/creators/html5.

## Validation

- Both web flavors build warning-clean and pass all 14 registered tests, including
  W6's 13 controlled lifecycle/input cases. Format/tidy passes in both web flavors.
- Five offline package tests cover identical repeated archives/manifest/staging,
  missing runtime/notices, missing/non-local/case-mismatched HTML assets, unsafe
  archive paths, developer paths, debug/test data, memory policy and tampering.
- Two complete packaging invocations produce the same ZIP SHA256. The local ZIP
  has 15 entries, 408,116 extracted bytes and 155,353 compressed bytes. Rebuilds can
  change content; use the command's printed digest to identify the current artifact.
- A fresh extraction outside the checkout serves all runtime files correctly.
  All packaged files are fetched bytewise over localhost with status 200; wasm
  is application/wasm, without COOP/COEP headers. Final notice additions do not
  change the three runtime files that underwent browser QA.
- Real in-app browser execution of the extracted Release iframe loads wasm and
  displays the readable unsupported-adapter message. Stop/Restart and entering/
  exiting fullscreen work. Removing JS or wasm in disposable copies produces
  the readable reload message; the wasm network failure also logs expected fetch/
  abort details. Screenshots: /tmp/ludus-w7-embed.jpg,
  /tmp/ludus-w7-missing-js.jpg, /tmp/ludus-w7-missing-wasm.jpg and
  /tmp/ludus-w7-fullscreen.jpg.
- Native suite: 27 registered tests, no failures, two no-display Wayland skips.
  ASan/UBSan: 24 registered, no failures, the same skips. Installed SDK consumer
  links successfully and passes its keyboard tap/rebind checks. Native format/tidy
  passes with the pinned Clang 18 tools.

Browser startup automation timed out initially and recovered after reloading the
created tab. The automation again logs an unattributed MutationObserver error;
adapter-unavailability diagnostics are expected. Neither is claimed as rendered
GPU evidence. Actual GPU animation/input/resume and live native rendering gates
remain open, as do flag-free itch.io hosted acceptance and browser support matrix.
The W5 pinned-port texture-wrapper limitation is unchanged.

## Reproduce and continue

```bash
python3 -m unittest discover -s scripts/python -p 'test_web_package.py' -v
./scripts/package-web
./scripts/test web-emscripten-development
./scripts/check web-emscripten-development --all
./scripts/test web-emscripten-release
./scripts/check web-emscripten-release --all
./scripts/test linux-clang-development
./scripts/check linux-clang-development --all
./scripts/test linux-clang-asan-ubsan
./scripts/install-sdk linux-clang-development
```

Check W7 PR/CI/merge before W8. Test the exact printed-hash ZIP in an itch.io
draft/private embed on a supported GPU without experimental flags; record OS,
browser version, GPU/backend, URL, hash, screenshot and console. The existing
controlled-provider tests are separate from this hardware evidence. Publish only
when separately requested. W7 packages the smoke demo, not a complete game export.
