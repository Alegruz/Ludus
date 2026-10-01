# Hosted acceptance and support record

The smoke export remains acceptance-pending until the exact Release ZIP passes
an itch.io draft/private embed test on real hardware without experimental GPU
flags. A green software-GPU workflow, Node provider test, or loading/error shell
does not close this requirement. W0/W4/W5/W6 hardware gates roll into this record.
Live native Vulkan rendering also remains unverified in the headless environment.

## Artifact and upload

Build `./scripts/package-web`, retain its printed SHA256 and upload that ZIP as the
browser-playable file. Use a draft/private project with manual 640 × 360 embedding
or fullscreen launch. Preserve relative asset paths; leave SharedArrayBuffer off.
See web-packaging.md for preparation, clean extraction and HTTPS details.

Current authorized target: https://alegruz.itch.io/uwaterloo-fall-game-jam
(draft project 5081459). Authorization covers replacing the browser test build,
keeping it unpublished. Do not publish as part of acceptance.

The W8 upload initially could not proceed: Edge's ChatGPT extension lacked local
file access. The user elected to enable that access; no file transfer is claimed
until a successful upload and saved draft are actually verified.

## Required evidence

Record the exact ZIP SHA256, project/embed URL, upload filename, test date, full
browser version, OS/session, GPU/driver and WebGPU adapter/backend. Record whether
experimental flags are enabled; flag-dependent results must be labeled that way.
Keep screenshots and relevant console output with the artifact identity.
Do not store account cookies, authentication tokens or secret draft URL tokens.

Verify loading → playing, changing clear color and visible triangle over time,
WASD/arrows, held pointer repositioning, resize/DPR without stretching, blur,
background/resume, stop/restart and user fullscreen enter/exit. Confirm normal
runtime asset requests succeed and no GPU validation errors occur. Failure/loss
paths additionally have controlled CI tests; do not deliberately destabilize a
user's GPU driver to manufacture a device-loss test.

## Support matrix

| Environment | Evidence | Support status |
| --- | --- | --- |
| Edge 154.0.4258.37, Linux/Wayland, Intel UHD 620 / Mesa 25.2.8 | User's 2026-09-30 GPU report and W0 hosted clear screenshot after enabling Vulkan | W0 flag-dependent feasibility only; current packaged app and flag-free support pending |
| Codex in-app browser on Linux | W6/W7 local extracted shell, controls, fullscreen and asset-failure tests; no adapter | Error UX verified; GPU rendering unsupported in this session |
| CI Chromium 140.0.7339.186 / SwiftShader under Xvfb | Exact-ZIP orange triangle pixels, animated clear pixels, keyboard movement and blur passed; lifecycle test corrections are rerunning | Full suite pending; software rendering only |
| Other desktop/mobile browsers and GPUs | No current artifact evidence | Unverified |

The historical GPU report predates the Vulkan flag change and is not a current
flag inventory. Browser internal settings pages are blocked by the automation
URL policy; the agent will not circumvent that restriction. A current version/
GPU/flag report must therefore come from user-provided diagnostics or an approved
browser surface that exposes it.

On 2026-10-01, the extracted Release ZIP with SHA256
`948946ae86e8fca4d29f9ae54693079a6f5990bc8680583b04b021ccff6f6e9f`
rendered an orange triangle in the user's Edge browser through a localhost HTTP
server. Its frame counter advanced from 73 to 559 during inspection and the
captured warning/error log was empty. This is local rendering evidence only:
the current browser flags and hosted upload remain unverified. The first W8
native CI and web compile/package jobs passed; the software-GPU browser failure
was resolved with Chromium's software Vulkan configuration and an Xvfb display.
The subsequent lifecycle test exposed Playwright's forced focus/visibility;
the runner disables that emulation during suspension and records the actual
freeze/resume boundaries. Full-suite acceptance remains pending, with DOM,
console and screenshot diagnostics retained on failure.

## Closing W8

Require successful native/web/package/browser CI, a saved draft containing the
identified ZIP, and the real-device evidence above. Update this matrix and the
stage tracker only with observed results. Keep W8 in progress while any gate is
missing. This completes a browser smoke demo export, not audio, saving, text,
assets, or other systems needed by a complete game.
