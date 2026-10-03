"""Optional release files rendered inside atomic project creation."""
from __future__ import annotations

from .release_model import canonical
from .templates import TemplateFile


def release_files(target: str, itch_target: str | None = None) -> list[TemplateFile]:
    config = {"schemaVersion": 1, "profiles": {"linux-release": {
        "targetPlatform": "linux-x64", "buildProfile": "release", "target": target,
        "installComponent": "GameRelease", "entryPoint": f"bin/{target}",
    }}}
    if itch_target is not None:
        from .release_model import ITCH_TARGET, string, fail
        if not ITCH_TARGET.fullmatch(string(itch_target, "itch target", 128)):
            fail("itch target must be lower-case username/game")
        config["itch"] = {"target": itch_target, "channels": {
            "linux": {"packageProfile": "linux-release", "channel": "linux-stable"},
        }}
    helper = '''# Generated native release component. These files belong to the game.
# Declare all runtime assets/libraries explicitly; never install the SDK itself.
install(TARGETS TARGET_PLACEHOLDER RUNTIME DESTINATION bin COMPONENT GameRelease)
install(FILES "${CMAKE_CURRENT_SOURCE_DIR}/NOTICE.txt" DESTINATION . COMPONENT GameRelease)
if(NOT DEFINED Ludus_SDK_MANIFEST)
    message(FATAL_ERROR "Release packaging requires an SDK with Ludus_SDK_MANIFEST")
endif()
get_filename_component(_game_ludus_share "${Ludus_SDK_MANIFEST}" DIRECTORY)
install(DIRECTORY "${_game_ludus_share}/licenses/" DESTINATION licenses/Ludus COMPONENT GameRelease)
# Add your own assets and their license notices here using COMPONENT GameRelease.
# Runtime dependency policy: only the documented Linux ABI baseline is external.
'''.replace("TARGET_PLACEHOLDER", target)
    return [TemplateFile("ludus.release.json", canonical(config).decode("utf-8")),
            TemplateFile("cmake/GameRelease.cmake", helper),
            TemplateFile("NOTICE.txt", "This game uses Ludus. See licenses/Ludus/ for SDK license notices.\n"
                         "Add notices for your game assets and every bundled dependency before distribution.\n"),
            TemplateFile("RELEASING.md", '''# Releasing this game

Build a native package with `ludus project package . --profile linux-release --version 0.1.0`.
Select a Release SDK explicitly if your local SDK is another flavor. Packaging
builds before installing and validates a clean extraction; it never uploads.

Verify the printed directory with `ludus project package verify <directory>`.
Run release tests against extracted game.zip before distributing it. Read the
manifest for external system libraries and SDK/platform requirements. NOTICE.txt
and SDK notices are a starting point; add licenses for authored assets/dependencies.

If itch.io is configured, use `ludus project publish plan . --package <directory>
--destination linux` to inspect the offline plan. Packages from local overrides
or dirty/unknown sources require `--allow-local-inputs`. This command does not
upload, authenticate, install butler or generate CI. Live uploading, deployment
workflows and Editor controls are subsequent milestones. A channel name does
not change itch.io project visibility. Account credentials never belong in Git.
''')]
