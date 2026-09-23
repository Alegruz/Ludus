"""Test-only connected datagram transport; sanitizer stderr remains separate."""
import os
import socket
import subprocess
import threading


def run_child(arguments, timeout=10):
    receiver, sender = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
    receiver.settimeout(0.05)
    packets = []
    done = threading.Event()

    def drain():
        while True:
            try:
                packets.append(receiver.recv(4096))
            except socket.timeout:
                if done.is_set():
                    return

    worker = threading.Thread(target=drain)
    worker.start()
    try:
        result = subprocess.run(arguments, capture_output=True, text=True, timeout=timeout,
                                pass_fds=(sender.fileno(),),
                                env={**os.environ, 'LUDUS_TEST_DIAGNOSTIC_FD': str(sender.fileno())})
    finally:
        done.set()
        worker.join()
        sender.close()
        receiver.close()
    result.stderr += b''.join(packets).decode('utf-8', errors='replace')
    return result
