"""Run the editor's bundled-sample collection from checkouts below out/ and build/.

The Qt resource list for `:/samples/...` must exclude generated folders inside
each sample, matched relative to that sample's root. A filter that matches the
absolute path drops every sample file whenever the checkout itself lives below
an `out/` or `build/` directory (for example an agent worktree under
`<repo>/out/worktrees/`), which leaves the editor without its sample library.
"""

from pathlib import Path
import subprocess
import sys
import tempfile
import unittest


SOURCE = Path(sys.argv[1]).resolve()
CMAKE = sys.argv[2]
del sys.argv[1:]

MODULE = SOURCE / "apps/editor/cmake/EditorSampleResources.cmake"
EDITOR_LISTS = SOURCE / "apps/editor/CMakeLists.txt"

KEPT = (
    "CMakeLists.txt",
    "ludus.project.json",
    "src/main.cpp",
    "src/output/kept.txt",
    "outline.txt",
    "build.json",
    "assets/rebuild/mesh.json",
)
EXCLUDED = (
    "out/build/linux/CMakeCache.txt",
    "build/game.o",
    ".ludus/state.json",
    "CMakeUserPresets.json",
    "src/out/generated.h",
    "nested/build/cache.txt",
    "nested/CMakeUserPresets.json",
)


class EditorSampleResourcesTests(unittest.TestCase):
    def collect(self, checkout: Path, samples):
        examples = checkout / "examples"
        for sample in samples:
            for relative in KEPT + EXCLUDED:
                path = examples / sample / relative
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(relative, encoding="utf-8")
        result_path = checkout.parent / "collected.txt"
        driver = checkout.parent / "collect.cmake"
        driver.write_text(
            f'include("{MODULE.as_posix()}")\n'
            f'ludus_collect_editor_sample_files(files "{examples.as_posix()}" {" ".join(samples)})\n'
            'string(JOIN "\\n" text ${files})\n'
            f'file(WRITE "{result_path.as_posix()}" "${{text}}")\n',
            encoding="utf-8")
        result = subprocess.run([CMAKE, "-P", str(driver)], text=True,
                                stdout=subprocess.PIPE, stderr=subprocess.STDOUT, timeout=30)
        self.assertEqual(result.returncode, 0, result.stdout)
        text = result_path.read_text(encoding="utf-8")
        return sorted(Path(line).relative_to(examples).as_posix() for line in text.splitlines() if line)

    def test_checkouts_below_generated_folder_names_keep_sample_files(self):
        samples = ("cornell-box", "live-edit-game")
        expected = sorted(f"{sample}/{relative}" for sample in samples for relative in KEPT)
        for checkout in ("out/worktrees/task", "build/Ludus", ".ludus/Ludus", "plain/Ludus (copy)"):
            with self.subTest(checkout=checkout), tempfile.TemporaryDirectory() as directory:
                root = Path(directory) / checkout
                root.mkdir(parents=True)
                self.assertEqual(self.collect(root, samples), expected)

    def test_editor_targets_use_the_shared_collection(self):
        text = EDITOR_LISTS.read_text(encoding="utf-8")
        self.assertIn("ludus_collect_editor_sample_files(editor_sample_files", text)
        self.assertNotIn("list(FILTER", text,
                         "filter sample files through ludus_collect_editor_sample_files, "
                         "relative to each sample root")


if __name__ == "__main__":
    unittest.main()
