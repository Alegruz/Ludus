# Browser smoke packaging and itch.io upload

Prepare the pinned web toolchain once, then produce a Release archive with one
command from the repository root:

```bash
./init.sh web-emscripten-release --preset-only
./scripts/package-web
```

The command delegates compilation to the existing web build tooling, which checks
the SDK/port and host-tool pins. It requires a prepared checkout and does not use
Conan/native graphics dependencies. It always builds before staging, then validates
and extracts the ZIP into an empty temporary directory. Missing runtime files or
notices, non-local HTML references, debug data, machine paths and oversized entries
fail packaging. Tests: `python3 -m unittest discover -s scripts/python -p
'test_web_package.py' -v`.

Outputs are `out/packages/ludus-web-smoke-release.zip` and the corresponding
`out/packages/ludus-web-smoke/` staging directory. The printed SHA256 identifies
the exact ZIP. Entries are sorted, timestamps fixed and permissions normalized;
the same payload with the same Python/zlib produces identical ZIP bytes. This
does not promise byte-identical compiler output across different SDKs or hosts.
Only the explicit file list ships; test providers, source maps, debug symbols,
native libraries and old staging files are excluded.

| Packaged files | Purpose |
| --- | --- |
| `index.html` | Player shell and entry point at ZIP root |
| `index.js`, `index.wasm` | Local generated runtime and application |
| `iframe.html` | Local sandbox QA harness; not the game's entry point |
| `NOTICE.txt`, `licenses/*` | Ludus, Emscripten, C/C++ runtime and WebGPU notices |
| `build-info.json` | Toolchain pins, memory policy and SHA256 of every other entry |

WGSL is embedded in wasm; there are no `.data`, separate shader, audio, texture,
worker or CDN assets in this smoke scope. HTML loads JS, which fetches wasm relative
to its script directory, then calls main; RAF waits for RHI and pipeline readiness.
The Release link uses `-g0`, keeps runtime assertions, and maps diagnostic filenames
to repository-relative paths. Development retains function names for debugging.

The single-threaded smoke app has a 32 MiB initial wasm memory, grows to at most
256 MiB, and uses a 64 KiB stack. The packager verifies the module's actual initial
and maximum unshared memory against that policy. Exhausting memory can abort the
runtime; the shell displays a reload message. These are smoke-app limits, not a
general game memory budget. No pthreads/SharedArrayBuffer, COOP or COEP requirement
is introduced.

## Local clean-extraction test

```bash
python3 -m zipfile -e out/packages/ludus-web-smoke-release.zip /tmp/ludus-web-test
python3 -m http.server 8000 --bind 127.0.0.1 --directory /tmp/ludus-web-test
```

Use a fresh extraction directory. Open `http://127.0.0.1:8000/index.html`, then
`http://127.0.0.1:8000/iframe.html`. `file://` is unsupported. Ordinary localhost
headers suffice; the harness grants scripts, same-origin and fullscreen.
Inspect Network: HTML/JS/wasm must return 200; wasm should be `application/wasm`.
Click the canvas, move with WASD/arrows, drag, resize, background/resume, Stop,
Restart and enter/exit fullscreen. Keep the console free of GPU validation errors.

For network-failure QA, serve disposable copies with `index.js` removed and with
`index.wasm` removed. Both should say “The game could not load. Please reload the
page.” Restore the asset and reload. An unsupported adapter instead gets a
WebGPU-unavailable message. Loading a readable shell does not prove GPU rendering.

## itch.io and HTTPS

Checked 2026-10-01 against [itch.io's HTML5 guide](https://itch.io/docs/creators/html5).
Select HTML Game, upload the ZIP and mark it as playable in the browser. Keep the
page draft/private for acceptance. Use Embed in page with manual 640 × 360 sizing,
or Click to launch in fullscreen. The shell resizes to the actual iframe dimensions.
Leave mobile-friendly off until touch/mobile support is tested; keep click-to-play
for the initial acceptance run. The shell already provides a fullscreen button.

The documented HTML archive limits are 1,000 files, 240 characters per path, 500 MB
total extracted and 200 MB per extracted file. The packager enforces conservative
decimal-byte limits. An account's general upload-size allowance is a separate limit.
Relative, case-matching paths matter because itch.io hosts games in a subdirectory.

For another HTTPS host, upload the ZIP's extracted contents together under one
directory, preserve relative names, serve wasm with its MIME type and avoid HTML
fallback responses for missing assets. Use valid HTTPS in production; localhost
is the local test path. Do not enable experimental GPU flags as an acceptance fix.
Actual itch.io hosting, browser/OS/GPU compatibility and flag-free rendering remain
W8 gates. Packaging the smoke demo does not export a complete game's systems.
