"""Independent negative tests for the declared crash-state oracle."""
import unittest
from recovery import OLD, boundaries, check_directory, check_process, check_seeded_revision, check_storage, payload


class RecoveryOracleTests(unittest.TestCase):
    def test_every_policy_and_shape_has_distinct_acknowledgment_boundaries(self):
        for policy in ("publish", "file", "directory"):
            for shape in ("empty", "multi"):
                points = boundaries(policy, shape)
                self.assertEqual(len(points), len(set(points)))
                self.assertEqual("file-synced" in points, policy != "publish")
                self.assertEqual("directory-synced" in points, policy == "directory")
                self.assertEqual(sum(point.startswith("write-") for point in points), 4 if shape == "multi" else 0)

    def test_process_rename_visibility_is_not_weak_durability(self):
        for replace in (False, True):
            for shape in ("empty", "multi"):
                check_process(OLD if replace else None, replace, shape, "file-synced")
                check_process(payload(shape), replace, shape, "replaced")
                with self.assertRaises(AssertionError):
                    check_process(OLD if replace else None, replace, shape, "replaced")

    def test_acknowledged_directory_sync_requires_exact_new_bytes(self):
        for replace in (False, True):
            for shape in ("empty", "multi"):
                for boundary in ("directory-synced", "complete"):
                    check_storage(payload(shape), replace, shape, "directory", boundary)
                    for invalid in (None, OLD, b"torn"):
                        with self.assertRaises(AssertionError):
                            check_storage(invalid, replace, shape, "directory", boundary)

    def test_durable_directory_cannot_keep_a_temporary_alias(self):
        entries = [{"name": ".ludus-write-leftover"}, {"name": "data"}]
        check_directory(entries, "file", "complete", "storage")
        for model, boundary in (("process", "replaced"), ("storage", "directory-synced")):
            with self.assertRaises(AssertionError):
                check_directory(entries, "directory", boundary, model)
            check_directory([{"name": "data"}], "directory", boundary, model)

    def test_publish_only_cannot_hide_corruption_of_the_seeded_inode(self):
        check_seeded_revision(OLD, 42, 42)
        check_seeded_revision(b"unsynced new data", 42, 43)
        check_seeded_revision(None, 42, None)
        with self.assertRaises(AssertionError):
            check_seeded_revision(b"corrupt old data", 42, 42)

    def test_weak_policies_do_not_hide_corruption_of_synced_data(self):
        for policy in ("file", "directory"):
            for value in (None, OLD, payload("multi")):
                check_storage(value, True, "multi", policy, "replaced")
            with self.assertRaises(AssertionError):
                check_storage(b"torn", True, "multi", policy, "replaced")
        check_storage(b"unsynced arbitrary data", True, "multi", "publish", "replaced")
        with self.assertRaises(AssertionError):
            check_storage(payload("multi"), True, "multi", "publish", "temporary-created")

# The Linux-only orchestration is checked without privileges or block devices.
# The fake copy/replay layer deliberately poisons each recovered candidate, so a
# missing reset would leak that candidate into the next prefix.
class BlockReplayIsolationTests(unittest.TestCase):
    def test_each_prefix_starts_from_seed_and_failure_keeps_images(self):
        from contextlib import contextmanager
        from pathlib import Path
        import tempfile
        from types import SimpleNamespace
        from unittest.mock import patch
        import recovery_block

        @contextmanager
        def fake_loop(image):
            yield str(image)

        @contextmanager
        def fake_mount(device, destination):
            yield destination

        with tempfile.TemporaryDirectory() as temporary:
            directory = Path(temporary)
            events = []
            marks = {"start": 0, "temporary-created": 1, "file-synced": 2, "temporary-closed": 3, "replaced": 4,
                     "directory-synced": 5, "complete": 6}

            def fake_copy(source, destination):
                destination.write_bytes(source.read_bytes())

            def fake_command(*arguments):
                if arguments[0] == "blockdev":
                    return "1024"
                if arguments[0] == "replay-log":
                    replay = directory / "replay.img"
                    self.assertEqual(replay.read_bytes(), b"seed")
                    events.append(int(arguments[-1]))
                    replay.write_bytes(b"previous recovered candidate")
                return "fake"

            def fake_drive(probe, root, policy, shape, callback):
                for boundary in boundaries(policy, shape):
                    callback(boundary)

            args = SimpleNamespace(probe=Path("probe"), replay_log="replay-log", max_prefixes=16)
            with patch.object(recovery_block, "command", side_effect=fake_command), \
                 patch.object(recovery_block, "loop", fake_loop), \
                 patch.object(recovery_block, "mounted", fake_mount), \
                 patch.object(recovery_block, "make_image", side_effect=lambda path, size: path.write_bytes(b"seed")), \
                 patch.object(recovery_block, "copy_image", side_effect=fake_copy), \
                 patch.object(recovery_block, "seed"), \
                 patch.object(recovery_block, "read_destination_inode", return_value=42), \
                 patch.object(recovery_block, "drive", side_effect=fake_drive), \
                 patch.object(recovery_block, "find_mark", side_effect=lambda tool, log, mark: marks[mark]), \
                 patch.object(recovery_block, "inspect_directory", return_value=[{"name": "data"}]), \
                 patch.object(recovery_block, "observe", side_effect=lambda root: OLD if events[-1] < 6 else b"corrupt"):
                with self.assertRaises(AssertionError):
                    recovery_block.storage_case(args, directory, "ext4", True, "empty", "directory")
            self.assertEqual(events, [1, 2, 3, 4, 5, 6])
            self.assertTrue((directory / "baseline.img").exists())
            self.assertTrue((directory / "writes.log").exists())
            self.assertTrue((directory / "replay.img").exists())


class RecoveryMountCleanupTests(unittest.TestCase):
    def test_transient_busy_unmount_is_retried_without_force(self):
        from subprocess import CalledProcessError
        from unittest.mock import patch
        import recovery_block
        busy = CalledProcessError(32, ["umount", "owned"], stderr="target is busy")
        with patch.object(recovery_block, "command", side_effect=[busy, busy, ""]) as native, \
             patch.object(recovery_block.time, "sleep") as pause:
            recovery_block.unmount_owned("owned")
        self.assertEqual(native.call_count, 3)
        self.assertTrue(all(call.args == ("umount", "owned") for call in native.call_args_list))
        self.assertEqual(pause.call_count, 2)

    def test_persistent_busy_and_other_errors_remain_failures(self):
        from subprocess import CalledProcessError
        from unittest.mock import patch
        import recovery_block
        for status, message, attempts in ((32, "target is busy", 20), (32, "not mounted", 1), (1, "denied", 1)):
            error = CalledProcessError(status, ["umount", "owned"], stderr=message)
            with patch.object(recovery_block, "command", side_effect=error) as native, \
                 patch.object(recovery_block.time, "sleep"):
                with self.assertRaises(CalledProcessError):
                    recovery_block.unmount_owned("owned")
            self.assertEqual(native.call_count, attempts)


if __name__ == "__main__":
    unittest.main()
