"""Uncertain cleanup must not reap a live or ambiguously owned process group."""
import unittest
from unittest.mock import Mock, patch

from editor_tool import OwnedProcess, _UNKNOWN_PROCESS_OWNERS


class ProcessOwnershipTests(unittest.TestCase):
    def owner(self):
        child = object.__new__(OwnedProcess)
        child.pid = 123
        child._signal_group = Mock()
        child._close_pipes = Mock()
        child._reap_leader = Mock(side_effect=AssertionError("uncertain leader was reaped"))
        self.addCleanup(lambda: _UNKNOWN_PROCESS_OWNERS.remove(child)
                        if child in _UNKNOWN_PROCESS_OWNERS else None)
        return child

    def test_unobserved_leader_never_enters_blocking_waitpid(self):
        child = self.owner()
        child._observe_exit_nowait = Mock(return_value=None)
        child._group_members = Mock(return_value=[])
        ticks = iter(range(100))
        result = child.cleanup(cancelled=True, now=lambda: next(ticks), sleep=lambda _: None)
        self.assertFalse(result.confirmed)
        self.assertTrue(result.forced)
        child._reap_leader.assert_not_called()
        self.assertIn(child, _UNKNOWN_PROCESS_OWNERS)

    def test_unknown_descendants_do_not_count_as_empty_group(self):
        child = self.owner()
        child._observe_exit_nowait = Mock(return_value=(0, None))
        child._group_members = Mock(return_value=None)
        ticks = iter(range(100))
        result = child.cleanup(cancelled=True, now=lambda: next(ticks), sleep=lambda _: None)
        self.assertFalse(result.confirmed)
        child._reap_leader.assert_not_called()

    def test_normal_exit_with_unknown_members_keeps_leader_waitable(self):
        child = self.owner()
        child._observe_exit_nowait = Mock(return_value=(0, None))
        child._group_members = Mock(return_value=None)
        ticks = iter(range(100))
        with patch("editor_tool.time.monotonic", side_effect=lambda: next(ticks)), \
                patch("editor_tool.time.sleep"):
            result = child.finalize_normal()
        self.assertFalse(result.confirmed)
        self.assertEqual(result.exit_code, 0)
        child._reap_leader.assert_not_called()


if __name__ == "__main__":
    unittest.main()
