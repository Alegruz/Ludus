"""Regression for the diagnostic receiver while a sanitized child starts slowly."""
import importlib.util
import os
from pathlib import Path
import time
from types import SimpleNamespace
import unittest
from unittest.mock import patch

SPEC = importlib.util.spec_from_file_location("startup_harness", Path(__file__).with_name("startup_tests.py"))
HARNESS = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(HARNESS)


class DelayedReport(unittest.TestCase):
    def test_report_after_receiver_poll_timeout(self):
        def delayed_child(_child, mode, *, report_fd, timeout):
            self.assertEqual(mode, "stderr-closed")
            self.assertEqual(timeout, 5)
            time.sleep(.35)
            os.write(report_fd, b"stderr-closed")
            return SimpleNamespace(returncode=0, stdout="CLOSED=delivered", stderr="")

        with patch.object(HARNESS, "run_direct", delayed_child):
            HARNESS.test_stderr_closed("delayed fixture")


if __name__ == "__main__":
    unittest.main()
