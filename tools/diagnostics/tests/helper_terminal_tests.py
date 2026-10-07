"""Exercise the real out-of-process helper prompt through a controlling PTY."""
import errno
import fcntl
import os
import pty
import select
import subprocess
import sys
import termios
import time


def launch(helper, child, answer, report_only=False):
    master, slave = pty.openpty()
    env = {k: v for k, v in os.environ.items()
           if k not in ("CI", "CONTINUOUS_INTEGRATION", "GITHUB_ACTIONS", "GITLAB_CI",
                        "BUILDKITE", "JENKINS_URL", "TEAMCITY_VERSION", "LUDUS_CI",
                        "DISPLAY", "WAYLAND_DISPLAY", "LUDUS_DIAGNOSTIC_INTERACTIVE",
                        "LUDUS_DIAGNOSTIC_REPORT_FD", "LUDUS_DIAGNOSTIC_CONTROL_FD")}
    def own_terminal():
        os.setsid()
        fcntl.ioctl(0, termios.TIOCSCTTY, 0)
    command = [sys.executable, helper]
    if report_only:
        command.append("--report-only")
    command += ["--", child, "single"]
    process = subprocess.Popen(command, stdin=slave, stdout=slave, stderr=slave,
                               env=env, preexec_fn=own_terminal)
    os.close(slave)
    output = bytearray()
    answered = False
    deadline = time.monotonic() + 8
    try:
        while time.monotonic() < deadline:
            ready, _, _ = select.select([master], [], [], 0.1)
            if ready:
                try:
                    data = os.read(master, 8192)
                except OSError as error:
                    if error.errno != errno.EIO:
                        raise
                    break
                if not data:
                    break
                output.extend(data)
                if not answered and b"(default terminate):" in output:
                    os.write(master, answer)
                    answered = True
            elif process.poll() is not None:
                break
        result = process.wait(timeout=1)
    finally:
        if process.poll() is None:
            process.kill()
            process.wait()
        os.close(master)
    text = output.decode("utf-8", "replace")
    if report_only:
        assert not answered, text
    else:
        assert answered, text
    return result, text


def main():
    helper, child = sys.argv[1:]
    result, output = launch(helper, child, b"c\n")
    assert result == 0 and "ASSERT-RETURNED" in output and "decision-probe" in output, (result, output)
    for answer in (b"t\n", b"\n", b"unknown\n"):
        result, output = launch(helper, child, answer)
        assert result == 134 and "ASSERT-RETURNED" not in output, (result, output)
    result, output = launch(helper, child, b"c\n", report_only=True)
    assert result == 134 and "ASSERT-RETURNED" not in output, (result, output)
    print("Real helper PTY: explicit continue, terminate, default, unknown and report-only passed")


if __name__ == "__main__":
    main()
