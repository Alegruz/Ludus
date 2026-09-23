"""Watchdog probes: neither a blocked stderr nor a full socket may stall fatal."""
import fcntl
import os
import resource
import signal
import socket
import subprocess
import sys

resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
binary = sys.argv[1]
r, w = os.pipe2(os.O_NONBLOCK)
try:
    while True:
        os.write(w, b'x' * 4096)
except BlockingIOError:
    pass
fcntl.fcntl(w, fcntl.F_SETFL, fcntl.fcntl(w, fcntl.F_GETFL) & ~os.O_NONBLOCK)
try:
    result = subprocess.run([binary, 'fatal'], stdout=subprocess.PIPE, stderr=w, timeout=5)
    assert result.returncode == -signal.SIGABRT, result.returncode
finally:
    os.close(w)
    os.close(r)
receiver, sender = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
try:
    while True:
        sender.send(b'x' * 2048, socket.MSG_DONTWAIT)
except BlockingIOError:
    pass
for mode, expected in (([], 0), (['fatal'], -signal.SIGABRT)):
    result = subprocess.run([binary, *mode], capture_output=True, timeout=5,
                            pass_fds=(sender.fileno(),),
                            env={**os.environ, 'LUDUS_TEST_DIAGNOSTIC_FD': str(sender.fileno())})
    assert result.returncode == expected, (result.returncode, result.stderr)
sender.close()
receiver.close()

# Close the peer inside the child, after successful healthy configuration.
for mode, expected in (("closed", 0), ("closed-fatal", -signal.SIGABRT)):
    result = subprocess.run([binary, mode], capture_output=True, timeout=5)
    assert result.returncode == expected, (mode, result.returncode, result.stderr)

print('Fatal exits with full blocking stderr; Check/fatal survive full and closed diagnostic endpoints')
