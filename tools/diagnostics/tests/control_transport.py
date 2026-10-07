"""Native control socket fixture: Linux packets, Darwin framed streams."""
import socket
import struct
import sys

CONTROL_TYPE = socket.SOCK_STREAM if sys.platform == "darwin" else socket.SOCK_SEQPACKET


def pair():
    return socket.socketpair(socket.AF_UNIX, CONTROL_TYPE)


def receive(control):
    if sys.platform != "darwin":
        return control.recv(2064)
    def exact(size):
        data = bytearray()
        while len(data) < size:
            part = control.recv(size - len(data))
            if not part:
                return bytes(data)
            data.extend(part)
        return bytes(data)
    header = exact(16)
    if len(header) != 16:
        return header
    length = struct.unpack("<IHHII", header)[4]
    if length > 2048:
        return header
    return header + exact(length)
