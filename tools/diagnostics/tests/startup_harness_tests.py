"""Regression for the diagnostic receiver while a sanitized child starts slowly."""
import importlib.util
import os
import socket
from pathlib import Path
import time
import threading
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

    def test_queued_report_survives_timeout_handled_after_child_exit(self):
        report_recv, report_send = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        completed = threading.Event()

        class HeldTimeout:
            first = True

            def __getattr__(self, name):
                return getattr(report_recv, name)

            def recv(self, size):
                if self.first:
                    self.first = False
                    if not completed.wait(2):
                        raise RuntimeError("fixture child did not complete")
                    raise socket.timeout("timeout handler resumed after child exit")
                return report_recv.recv(size)

        def child(_child, mode, *, report_fd, timeout):
            os.write(report_fd, b"stderr-closed")
            return SimpleNamespace(returncode=0, stdout="CLOSED=delivered", stderr="")

        receiver_threading = SimpleNamespace(Event=lambda: completed, Thread=threading.Thread)
        with patch.object(HARNESS, "threading", receiver_threading), \
                patch.object(HARNESS.socket, "socketpair", return_value=(HeldTimeout(), report_send)), \
                patch.object(HARNESS, "run_direct", child):
            HARNESS.test_stderr_closed("queued fixture")


if __name__ == "__main__":
    unittest.main()
